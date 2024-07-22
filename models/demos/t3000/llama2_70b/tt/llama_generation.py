# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch
import ttnn
from ttnn import ConcatMeshToTensor

from loguru import logger

import copy
from models.demos.t3000.llama2_70b.tt.llama_model_optimized import TtLlamaModel_optimized as TtLlamaModel
from models.demos.t3000.llama2_70b.tt.llama_common import BASE_URL
from models.demos.t3000.llama2_70b.tt.model_config import (
    get_model_config,
)


class TtLlamaModelForGeneration:
    def __init__(self, configuration, state_dict, model_args, tt_args):
        # Cache Weights setup
        n_layers = model_args.num_layers or 80

        self.params = copy.deepcopy(configuration)

        self.llama_version = model_args.llama_version
        self.max_batch_size = model_args.max_batch_size
        self.max_kv_context_len = model_args.max_kv_context_len

        self.device_mesh = tt_args.device_mesh

        # Initial model_config is set in decode mode
        model_config = get_model_config(
            llama_version=self.llama_version,
            max_batch_size=self.max_batch_size,
            max_context_len=self.max_kv_context_len,
            batch=self.max_batch_size,
            seq_len=1,
        )

        # TT model -------------------------------------------------------------
        self.tt_model = TtLlamaModel(
            self.device_mesh,
            state_dict,
            BASE_URL,
            n_layers,
            model_config,
            self.params,
            cache_path=tt_args.cache_path,
            read_cache=False,
        )

        del state_dict

    def forward(self, tokens: torch.Tensor, start_pos: int):
        if start_pos > 2048 + 128:
            raise ValueError("Exceeding current max context length of 2k+128 tokens")
        _, seq_len = tokens.shape
        if seq_len == 1:
            return self.decode_forward(tokens, start_pos)
        else:
            return self.prefill_forward(tokens, start_pos)

    def decode_forward(self, tokens: torch.Tensor, start_pos: int):
        self._update_model_config("decode", tokens.shape[0], 1)
        tt_inp_emb, start_pos, rot_mat, attn_mask = self.tt_model.prepare_inputs(tokens, start_pos)

        trace = True
        if trace:
            ########### TRACE ###########
            import time

            logger.info(f"Trace with input shape: {tt_inp_emb.shape=}")

            logger.info("Compiling Model")
            c1 = time.time()
            tt_logits = self.tt_model(
                tt_inp_emb,
                rot_mat,
                start_pos,
                attn_mask,
            )
            logits = ttnn.to_torch(
                tt_logits, device=self.device_mesh, mesh_composer=ConcatMeshToTensor(self.device_mesh, dim=3)
            )
            c2 = time.time()
            logger.info(f"Compiling Model took: {c2 - c1} seconds.")

            logger.info("Capturing Trace")
            t1 = time.time()
            trace_id = ttnn.begin_trace_capture(self.device_mesh, cq_id=0)
            tt_logits = self.tt_model(
                tt_inp_emb,
                rot_mat,
                start_pos,
                attn_mask,
            )
            ttnn.end_trace_capture(self.device_mesh, trace_id, cq_id=0)
            t2 = time.time()
            logger.info(f"Capturing Trace took: {t2 - t1} seconds.")

            logger.info("Starting Trace perf test...")
            num_iters = 500

            times = []
            import tqdm

            for i in tqdm.tqdm(range(num_iters)):
                x1 = time.time()
                ttnn.execute_trace(self.device_mesh, trace_id, blocking=False)
                logits = ttnn.to_torch(
                    tt_logits, device=self.device_mesh, mesh_composer=ConcatMeshToTensor(self.device_mesh, dim=3)
                )

                x2 = time.time()

                times.append(x2 - x1)
            logger.info(
                f"Ran Trace for {num_iters} iterations. Avg Trace execution time: {sum(times[1:]) / len(times[1:])} seconds."
            )
            print(times)
            ttnn.release_trace(self.device_mesh, trace_id)

            ########### TRACE ###########
        else:
            tt_logits = self.tt_model(
                tt_inp_emb,
                rot_mat,
                start_pos,
                attn_mask,
            )
            logits = ttnn.to_torch(
                tt_logits, device=self.device_mesh, mesh_composer=ConcatMeshToTensor(self.device_mesh, dim=3)
            )

        del tt_inp_emb
        del rot_mat
        del attn_mask

        logits = self._process_logits(tt_logits)

        logits = logits.permute(2, 1, 0, 3).squeeze().unsqueeze(1)  # [batch, 1, vocab_size]
        del tt_logits

        return logits

    def prefill_forward_single_user(self, tokens: torch.Tensor, start_pos: int, user_id: int):
        batch, seq_len = tokens.shape
        assert batch == 1
        assert start_pos == 0, "start_pos must be 0 for prefill_forward_single_user"
        assert seq_len in [128, 2048], f"Only prefill up to 128 or 2048 tokens is supported, got {seq_len}"

        self._update_model_config("prefill", batch, seq_len)

        tt_inp_emb, start_pos, rot_mat, attn_mask = self.tt_model.prepare_inputs(
            tokens, start_pos=start_pos, valid_seq_len=seq_len
        )

        tt_logits = self.tt_model(
            tt_inp_emb,
            rot_mat,
            start_pos,
            attn_mask,
            user_id=user_id,
        )

        del tt_inp_emb
        del rot_mat
        del attn_mask

        logits = self._process_logits(tt_logits)
        logits = logits.squeeze(1)
        del tt_logits
        return logits

    def prefill_forward(self, tokens: torch.Tensor, start_pos: int):
        batch, seq_len = tokens.shape
        assert seq_len <= 2048, f"Only prefill up to 2048 tokens is supported, got {seq_len}"

        prefill_seq_len = 128 if seq_len <= 128 else 2048
        self._update_model_config("prefill", batch, prefill_seq_len)

        batch, seq_len = tokens.shape
        output_logits = torch.zeros(batch, seq_len, self.params.vocab_size)
        padded_seq_len = 128 if seq_len <= 128 else 2048
        # pad tokens to 128 or 2048
        prefill_ids = torch.cat([tokens, torch.zeros(batch, padded_seq_len - seq_len).long()], dim=-1)

        for user_id in range(batch):
            logger.info(f"Filling kv cache for user {user_id + 1}")

            logits = self.prefill_forward_single_user(prefill_ids[user_id : user_id + 1], start_pos, user_id)

            output_logits[user_id] = logits[:, :seq_len, :]

        logger.info(f"Finished prefill for all users up to {seq_len} tokens, Starting decode...")

        return output_logits

    def _process_logits(self, tt_logits):
        logits = ttnn.to_torch(
            tt_logits, device=self.device_mesh, mesh_composer=ConcatMeshToTensor(self.device_mesh, dim=3)
        )
        return logits[..., : self.params.vocab_size].float()

    def _update_model_config(self, mode, batch, seq_len):
        if self.tt_model.model_config["LLM_MODE"] != mode:
            logger.info(f"Changing mode to {mode}")
            model_config = get_model_config(
                llama_version=self.llama_version,
                max_batch_size=self.max_batch_size,
                max_context_len=self.max_kv_context_len,
                batch=batch,
                seq_len=seq_len,
            )
            self.tt_model.set_model_config(model_config)
