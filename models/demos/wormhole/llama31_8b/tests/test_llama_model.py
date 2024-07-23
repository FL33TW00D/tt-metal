# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0
import torch
import pytest
from loguru import logger
import os

# Set Llama flags for CI, if CI environment is setup
if os.getenv("CI") == "true":
    os.environ["LLAMA31_8B_CKPT_DIR"] = "/mnt/MLPerf/ttnn/models/demos/llama31_8b/"
    os.environ["LLAMA31_8B_TOKENIZER_PATH"] = "/mnt/MLPerf/ttnn/models/demos/llama31_8b/"
    os.environ["LLAMA31_8B_CACHE_PATH"] = "/mnt/MLPerf/ttnn/models/demos/llama31_8b/"

import ttnn
from models.demos.wormhole.llama31_8b.tt.llama_common import (
    precompute_freqs,
    freqs_to_rotation_matrix,
    prepare_inputs_ttnn,
    sample,
    load_safetensors_state_dict,
)
from models.demos.wormhole.llama31_8b.tt.llama_model import TtTransformer
from models.demos.wormhole.llama31_8b.tt.model_config import TtModelArgs
from transformers.models.llama.modeling_llama import LlamaForCausalLM as RefTransformer
from transformers import AutoTokenizer, AutoConfig
from models.utility_functions import (
    comp_pcc,
    comp_allclose,
)
from models.utility_functions import skip_for_grayskull
from safetensors.torch import load_file


class Emb(torch.nn.Module):
    def __init__(self, model_args):
        super().__init__()
        self.emb = torch.nn.Embedding(model_args.vocab_size, model_args.dim)

    def forward(self, x):
        return self.emb(x)


@skip_for_grayskull("Requires wormhole_b0 to run")
@pytest.mark.models_performance_bare_metal
@pytest.mark.parametrize(
    "version",
    (
        # "generative",
        "instruct",
    ),
)
@pytest.mark.parametrize(
    "iterations",
    (17,),
)
def test_llama_model_inference(device, iterations, version, use_program_cache, reset_seeds):
    if version == "generative":
        instruct = False
    elif version == "instruct":
        instruct = True
    else:
        assert "Invalid version. Please use 'generative' or 'instruct'"

    run_ref_pt = True  # Flag to run reference PyTorch model and compare PCC
    cache_pcc = False  # Flag to measure KV cache PCC for all layers

    dtype = ttnn.bfloat8_b

    model_args = TtModelArgs(device)
    model_args.max_batch_size = 32
    model_args.n_layers = 1

    tokenizer = AutoTokenizer.from_pretrained("meta-llama/Meta-Llama-3.1-8B-Instruct", trust_remote_code=True)

    logger.info("Loading weights...")
    state_dict = model_args.load_state_dict()
    logger.info("Finished loading weights...")

    if instruct:
        # The instruct prompts follow the format: <bos> [INST] prompt [/INST]. [INST] are strings. <bos> is the correspoding bos_id token
        prompts = ["[INST] what is the capital of Canada? [/INST]"] * 32
    else:
        prompts = ["This is a test"] * 32

    encoded_prompts = [tokenizer.encode(prompt) for prompt in prompts]

    if run_ref_pt:
        ref_config = AutoConfig.from_pretrained("meta-llama/Meta-Llama-3.1-8B-Instruct")
        ref_config.num_hidden_layers = model_args.n_layers
        reference_model = RefTransformer(config=ref_config)  # FIXME
        reference_model.load_state_dict(state_dict)

    # Embedding on host
    embd = Emb(model_args)
    embd.load_state_dict({"emb.weight": state_dict["model.embed_tokens.weight"]})

    generation_start_pos = 0
    generation_length = iterations

    # pre-compute the rotational embedding matrix and send to device
    cos, sin = precompute_freqs(model_args.head_dim, model_args.max_seq_len * 2)
    rot_emb_matrix = freqs_to_rotation_matrix(cos, sin)

    rot_emb_matrix_list = []
    for i in range(rot_emb_matrix.shape[0]):
        rot_emb_matrix_list.append(
            ttnn.from_torch(
                rot_emb_matrix[i, :, :].unsqueeze(0).unsqueeze(0), device=device, dtype=dtype, layout=ttnn.TILE_LAYOUT
            )
        )  # ttnn.bfloat16

    # Load TTNN model
    tt_model = TtTransformer(
        args=model_args,
        device=device,
        dtype=dtype,
        state_dict=state_dict,
        weight_cache_path=model_args.weight_cache_path(dtype),
        layers=list(range(model_args.n_layers)),
        rot_mat=rot_emb_matrix_list,
        start_pos=generation_start_pos,
    )

    if run_ref_pt:
        all_tests_pass = True

    seqlen = 1  # Generating one token per user at a time
    batch = 32

    if run_ref_pt:
        cos, sin = precompute_freqs(model_args.head_dim, model_args.max_seq_len * 2)
        # freqs_cis = torch.complex(cos, sin)

    # Select the first token from the prompts for initial decoding
    encoded_prompts_tensor = torch.tensor(encoded_prompts)  # [:,0]
    pt_decode_input = embd(encoded_prompts_tensor[:, 0]).view(batch, seqlen, -1)

    tt_decode_input = pt_decode_input
    ref_past_key_values = None

    # Keep track of generated outputs to print out later
    all_outputs = []
    if run_ref_pt:
        all_outputs_ref = []

    for i in range(generation_length):
        current_pos = generation_start_pos + i

        decode_input, pos = prepare_inputs_ttnn(
            tt_decode_input,
            current_pos,
            model_args.dim,
            tt_model.device,
        )

        # Run TT model
        tt_out = tt_model(decode_input, pos)
        # Convert ttnn tensor to torch tensor
        tt_output_torch = ttnn.to_torch(tt_out).permute(2, 1, 0, 3).squeeze(1)  # [seq, batch, hidden_dim]

        if run_ref_pt:  # Run reference model
            # freqs_cis_i = freqs_cis[current_pos, :].unsqueeze(0)
            positions = torch.tensor([[current_pos]] * batch)
            # mask = ttnn.to_torch(attn_mask[0])
            ref_output, ref_past_key_values = reference_model(
                inputs_embeds=pt_decode_input,
                past_key_values=ref_past_key_values,
                position_ids=positions,
                use_cache=True,
                return_dict=False,
            )
            # ref_output = reference_model(pt_decode_input, position_ids=positions)

        # While in "prefill" mode, use the prompt tokens as the output
        if i in range(len(encoded_prompts[0])):
            all_outputs.append(encoded_prompts[0][i])  # Update list of TT outputs
            if run_ref_pt:
                all_outputs_ref.append(encoded_prompts[0][i])  # Update list of ref outputs

            tt_decode_input = embd(encoded_prompts_tensor[:, i]).view(batch, seqlen, -1)
            if run_ref_pt:
                pt_decode_input = embd(encoded_prompts_tensor[:, i]).view(batch, seqlen, -1)
        else:
            # Greedy decode (temperature = 0) the generated token and save it to print out later
            tt_out_tok = sample(tt_output_torch, temperature=0, top_p=0.8)
            tt_decode_input = embd(tt_out_tok)
            all_outputs.append(tt_out_tok.squeeze(1).tolist()[0])  # Update generated token to list of TT outputs
            if run_ref_pt:
                pt_out_tok = sample(ref_output, temperature=0, top_p=0.8)
                pt_decode_input = embd(pt_out_tok)
                all_outputs_ref.append(
                    pt_out_tok.squeeze(1).tolist()[0]
                )  # Update generated token to list of ref outputs

        # TODO Measure only PCC at the end, instead of at every iteration
        # Measure PCC if also running reference model
        if run_ref_pt:
            passing, pcc_message = comp_pcc(ref_output, tt_output_torch)

            logger.info(comp_allclose(ref_output, tt_output_torch))
            logger.info(f"Model output: {pcc_message}")

            if passing:
                logger.info("Llama Model Passed!")
            else:
                logger.warning("Llama Model Failed!")
            if not passing:
                all_tests_pass = False

            # Compare KV caches
            if cache_pcc:
                for i in range(model_args.n_layers):
                    pytorch_layer_present = [
                        reference_model.layers[i]
                        .attention.cache_k.clone()
                        .permute(0, 2, 1, 3),  # [batch, n_kv_heads, seq, head_dim]
                        reference_model.layers[i]
                        .attention.cache_v.clone()
                        .permute(0, 2, 1, 3),  # [batch, n_kv_heads, seq, head_dim]
                    ]

                    tt_layer_present = []
                    for layer_past in tt_model.layers[i].attention.layer_past_list[0]:
                        tt_layer_present.append(ttnn.to_torch(layer_past))

                    for i, (cache_pt, cache_tt) in enumerate(zip(pytorch_layer_present, tt_layer_present)):
                        cache_length_to_check = generation_start_pos + generation_length + 1
                        cache_pt = cache_pt[:, :, generation_start_pos:cache_length_to_check, :]
                        cache_tt = cache_tt[:, :, generation_start_pos:cache_length_to_check, :]
                        does_pass, output_pcc = comp_pcc(cache_pt, cache_tt)
                        if i == 0:
                            logger.info(f"K cache output: {output_pcc}")
                        else:
                            logger.info(f"V cache output: {output_pcc}")

                        if does_pass:
                            logger.info(f"V Cache Passed!")
                        else:
                            logger.warning(f"V Cache Failed! PCC value is lower than {0.99}")
                        # if not does_pass:
                        # all_tests_pass = False

        logger.trace("[ttnn generation User 0] ", "".join(tokenizer.decode(all_outputs)))
        if run_ref_pt:
            logger.trace("[Ref generation User 0] ", "".join(tokenizer.decode(all_outputs_ref)))

    if run_ref_pt:
        if all_tests_pass:
            logger.info(f"All {generation_length} Llama decode iterations Passed!")
        else:
            logger.warning("One or more iterations of Llama decode had bad PCC")
            assert all_tests_pass, f"PCC value is lower than {0.99} for some of the outputs. Check Warnings!"
