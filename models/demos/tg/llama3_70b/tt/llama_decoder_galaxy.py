# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from loguru import logger
from typing import List
import torch
import ttnn
from ttnn import ReplicateTensorToMesh, ShardTensorToMesh

from models.demos.tg.llama3_70b.tt.llama_attention_galaxy import TtLlamaAttention_galaxy
from models.demos.tg.llama3_70b.tt.llama_mlp_galaxy import TtLlamaMLP_galaxy
from models.demos.t3000.llama2_70b.tt.llama_common import (
    ShardTensor2dMesh,
    ConcatMesh2DToTensor,
)


class TtLlamaDecoder_galaxy:
    def __init__(
        self,
        device_mesh,
        cluster_shape,
        state_dict,
        base_url,
        layer_num,
        model_config,
        configuration,
        transformation_mats,
        cache_path=None,
        read_cache=False,
    ):
        super().__init__()

        self.state_dict = state_dict
        self.device_mesh = device_mesh
        self.num_devices = device_mesh.get_num_devices()
        self.model_config = model_config
        self.read_cache = read_cache
        self.cluster_shape = cluster_shape

        self.hidden_size = configuration.dim
        self.n_heads = configuration.n_heads
        self.n_local_heads = self.n_heads // self.num_devices
        self.padded_local_heads = 32
        self.head_dim = self.hidden_size // self.n_heads
        self.max_seq_len = configuration.max_seq_len
        self.norm_eps = configuration.norm_eps
        self.rope_theta = configuration.rope_theta

        self.llama3 = configuration.vocab_size == 128256

        self.layer_name = f"{base_url}.{layer_num}"
        self.cache_path = cache_path

        self.attention = TtLlamaAttention_galaxy(
            device_mesh,
            cluster_shape,
            state_dict,
            base_url,
            layer_num,
            model_config,
            configuration,
            transformation_mats,
            cache_path=cache_path,
            read_cache=read_cache,
        )

        self.mlp = TtLlamaMLP_galaxy(
            device_mesh,
            cluster_shape,
            state_dict,
            base_url,
            layer_num,
            self.hidden_size,
            model_config,
            cache_path=cache_path,
            read_cache=read_cache,
        )

        self.get_decoder_config()
        self.load_weights()

    def get_decoder_config(self):
        self.LN_COMPUTE_KERNEL_CONFIG = ttnn.WormholeComputeKernelConfig(
            math_fidelity=ttnn.MathFidelity.HiFi2,
            math_approx_mode=False,
            fp32_dest_acc_en=False,
            packer_l1_acc=False,
        )

        self.LN_PROGCFG = ttnn.LayerNormShardedMultiCoreProgramConfig(
            compute_with_storage_grid_size=[8, 4],
            subblock_w=8,
            block_h=32 // 32,
            block_w=8,
            inplace=False,
        )

        shard_spec_32_cores_grid = ttnn.CoreRangeSet(
            {
                ttnn.CoreRange(
                    ttnn.CoreCoord(0, 0),
                    ttnn.CoreCoord(7, 3),
                ),
            }
        )

        self.LN_OUTPUT_MEMCFG = ttnn.MemoryConfig(
            ttnn.TensorMemoryLayout.WIDTH_SHARDED,
            ttnn.BufferType.L1,
            ttnn.ShardSpec(
                shard_spec_32_cores_grid,
                [
                    32,
                    8192 // 32,
                ],
                ttnn.ShardOrientation.ROW_MAJOR,
                False,
            ),
        )
        self.ATTN_ACT_MEMCFG = ttnn.create_sharded_memory_config(
            shape=(32, 2048 // 32),
            core_grid=ttnn.CoreGrid(y=4, x=8),
            strategy=ttnn.ShardStrategy.WIDTH,
            orientation=ttnn.ShardOrientation.ROW_MAJOR,
            use_height_and_width_as_shard_shape=True,
        )
        self.MLP_ACT_MEMCFG = ttnn.create_sharded_memory_config(
            shape=(32, 2048 // 8),
            core_grid=ttnn.CoreGrid(y=1, x=8),
            strategy=ttnn.ShardStrategy.WIDTH,
            orientation=ttnn.ShardOrientation.ROW_MAJOR,
            use_height_and_width_as_shard_shape=True,
        )

    def set_model_config(self, model_config):
        self.model_config = model_config
        self.attention.set_model_config(model_config)
        self.mlp.set_model_config(model_config)

    def load_weights(self):
        """
        Loads weights that this layer is responsible for.
        Doesn't touch the weights of the submodules.
        """
        assert not hasattr(self, "attn_norm"), "attn_norm_list is already an attribute of this object"
        assert not hasattr(self, "ffn_norm"), "ffn_norm_list is already an attribute of this object"
        attn_norm_str = f"{self.layer_name}.attention_norm.weight"
        ffn_norm_str = f"{self.layer_name}.ffn_norm.weight"

        # attn_norm_cache_str = f"{self.layer_name}.attention_norm_galaxy.weight"
        # ffn_norm_cache_str = f"{self.layer_name}.ffn_norm_galaxy.weight"

        attn_norm_sharded_str = f"{self.layer_name}.attention_norm_sharded_galaxy.weight"
        ffn_norm_sharded_str = f"{self.layer_name}.ffn_norm_sharded_galaxy.weight"

        pt_attn_norm = None
        pt_ffn_norm = None
        if not self.read_cache:
            pt_attn_norm = self.state_dict[attn_norm_str].reshape([1, 1, -1, 32])
            pt_ffn_norm = self.state_dict[ffn_norm_str].reshape([1, 1, -1, 32])

        # self.attn_norm = ttnn.as_tensor(
        #     pt_attn_norm,
        #     dtype=ttnn.bfloat16,
        #     layout=ttnn.ROW_MAJOR_LAYOUT,
        #     device=self.device_mesh,
        #     memory_config=ttnn.DRAM_MEMORY_CONFIG,
        #     mesh_mapper=ReplicateTensorToMesh(self.device_mesh),
        #     cache_file_name=self.cache_path / attn_norm_cache_str,
        # )

        self.attn_norm_sharded = ttnn.as_tensor(
            pt_attn_norm,
            dtype=ttnn.bfloat16,
            layout=ttnn.ROW_MAJOR_LAYOUT,
            device=self.device_mesh,
            memory_config=ttnn.DRAM_MEMORY_CONFIG,
            mesh_mapper=ShardTensor2dMesh(self.device_mesh, (2, None), self.cluster_shape),
            cache_file_name=self.cache_path / attn_norm_sharded_str,
        )

        # self.ffn_norm = ttnn.as_tensor(
        #     pt_ffn_norm,
        #     dtype=ttnn.bfloat16,
        #     layout=ttnn.ROW_MAJOR_LAYOUT,
        #     device=self.device_mesh,
        #     memory_config=ttnn.DRAM_MEMORY_CONFIG,
        #     mesh_mapper=ReplicateTensorToMesh(self.device_mesh),
        #     cache_file_name=self.cache_path / ffn_norm_cache_str,
        # )

        self.ffn_norm_sharded = ttnn.as_tensor(
            pt_ffn_norm,
            dtype=ttnn.bfloat16,
            layout=ttnn.ROW_MAJOR_LAYOUT,
            device=self.device_mesh,
            memory_config=ttnn.DRAM_MEMORY_CONFIG,
            mesh_mapper=ShardTensor2dMesh(self.device_mesh, (2, None), self.cluster_shape),
            cache_file_name=self.cache_path / ffn_norm_sharded_str,
        )

    def __call__(
        self,
        xs: List[ttnn.Tensor],
        rot_mats: List[ttnn.Tensor],
        start_pos: int,
        attn_masks: List[ttnn.Tensor],
        user_id: int = 0,
    ) -> ttnn.Tensor:
        if self.model_config["LLM_MODE"] == "decode":
            return self.decode_forward(xs, rot_mats, start_pos, attn_masks)
        else:
            raise ValueError(f"Unknown llm_mode: {self.model_config['LLM_MODE']}")

    def tt_all_gather(self, input_tensor, dim, cluster_axis, memory_config=None):
        # Ensure the input tensor is in the correct memory configuration
        input_tensor = ttnn.to_memory_config(input_tensor, ttnn.DRAM_MEMORY_CONFIG)

        # Get the full device tensors list from the input tensor
        device_tensors = ttnn.get_device_tensors(input_tensor)

        num_rows, num_cols = self.cluster_shape[1], self.cluster_shape[0]

        def gather_tensors(indices):
            tensors = ttnn.aggregate_as_tensor([device_tensors[i] for i in indices])

            return ttnn.line_all_gather(
                tensors,
                dim,
                num_links=1,  # TODO: Check why we cant use more
                memory_config=ttnn.MemoryConfig(buffer_type=ttnn.experimental.tensor.BufferType.DRAM),
            )

        aggregated_outputs = []

        if cluster_axis == 0:
            # Process row-wise when cluster_axis is 0
            for row in range(num_rows):
                start_idx = row * num_cols
                end_idx = start_idx + num_cols
                indices = range(start_idx, end_idx)
                gathered_tensor = gather_tensors(indices)
                aggregated_outputs.append(gathered_tensor)

        elif cluster_axis == 1:
            # Process column-wise when cluster_axis is 1
            for col in range(num_cols):
                indices = range(col, len(device_tensors), num_cols)
                gathered_tensor = gather_tensors(indices)
                aggregated_outputs.append(gathered_tensor)

        # Flatten device tensors
        flattened_tensors = [tensor for output in aggregated_outputs for tensor in ttnn.get_device_tensors(output)]

        final_output_tensor = ttnn.aggregate_as_tensor(flattened_tensors)

        return final_output_tensor

    def tt_distributed_rmsnorm(self, inp, epsilon, gamma):
        # Run distributed rmsnorm part 1
        tt_stats = ttnn.experimental.operations.primary.rmsnorm_pre_allgather(
            inp, compute_kernel_config=self.LN_COMPUTE_KERNEL_CONFIG, output_dtype=ttnn.bfloat16
        )

        tt_stats = ttnn.reshape(
            tt_stats, ttnn.Shape((1, 1, 32, 32), (1, 1, 32, 32))
        )  # TODO: Figure out why we need this

        tt_stats = self.tt_all_gather(
            tt_stats,
            dim=3,
            cluster_axis=0,
            memory_config=ttnn.DRAM_MEMORY_CONFIG,
        )

        # Run distributed rmsnorm part 2
        tt_out = ttnn.experimental.operations.primary.rmsnorm_post_allgather(
            inp, tt_stats, epsilon, gamma, compute_kernel_config=self.LN_COMPUTE_KERNEL_CONFIG
        )

        tt_stats.deallocate(True)

        return tt_out

    def decode_forward(
        self,
        xs: List[ttnn.Tensor],
        rot_mats: List[ttnn.Tensor],
        start_pos: int,
        attn_masks: List[ttnn.Tensor],
    ) -> List[ttnn.Tensor]:
        xs_interleaved = ttnn.to_memory_config(xs, memory_config=ttnn.DRAM_MEMORY_CONFIG)

        attn_norm_out = self.tt_distributed_rmsnorm(
            xs_interleaved,
            epsilon=self.norm_eps,
            gamma=self.attn_norm_sharded,
        )

        attn_norm_out = ttnn.to_memory_config(attn_norm_out, memory_config=self.ATTN_ACT_MEMCFG)
        attn_outs = self.attention(attn_norm_out, rot_mats, start_pos, attn_masks)
        attn_outs = ttnn.to_memory_config(attn_outs, memory_config=self.MLP_ACT_MEMCFG)

        output = xs
        output = ttnn.add(
            output,
            attn_outs,
            memory_config=ttnn.L1_WIDTH_SHARDED_MEMORY_CONFIG,
        )
        attn_outs.deallocate(True)

        output_interleaved = ttnn.to_memory_config(output, memory_config=ttnn.DRAM_MEMORY_CONFIG)
        ffn_norm_out = self.tt_distributed_rmsnorm(
            output_interleaved,
            epsilon=self.norm_eps,
            gamma=self.ffn_norm_sharded,
        )

        ffn_norm_out = ttnn.to_memory_config(ffn_norm_out, memory_config=self.MLP_ACT_MEMCFG)
        ffn_out = self.mlp(ffn_norm_out)

        ### residual add
        output = ttnn.add(
            output,
            ffn_out,
            memory_config=ttnn.L1_WIDTH_SHARDED_MEMORY_CONFIG,
        )
        ffn_out.deallocate(True)

        return output
