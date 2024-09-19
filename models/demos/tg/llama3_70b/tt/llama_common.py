# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import ttnn
import torch


class PytorchLlamaModel(torch.nn.Module):
    def __init__(self, hf_reference_model):
        super().__init__()
        self.model = hf_reference_model

        # Disable dropout
        self.model.eval()

        configuration = hf_reference_model.params
        self.n_heads = configuration.n_heads
        hidden_dim = configuration.dim
        self.head_dim = hidden_dim // self.n_heads
        self.max_seq_len = configuration.max_seq_len

    def forward(self, x, start_pos):
        """
        x: (batch, seq)
        start_pos: int

        return: (batch, seq, hidden_dim)
        """
        with torch.no_grad():
            return self.model(x, start_pos)


def tt_all_reduce(input_tensor, mesh_device, cluster_axis, dim=0, num_links=2, memory_config=None):
    # Ensure the input tensor is in the correct memory configuration
    input_tensor = ttnn.to_memory_config(input_tensor, ttnn.DRAM_MEMORY_CONFIG)

    gathered_tensor = ttnn.line_all_gather(
        input_tensor, dim, num_links=num_links, cluster_axis=cluster_axis, mesh_device=mesh_device
    )
    reduced_tensors = ttnn.experimental.fast_reduce_nc(
        gathered_tensor, dims=[dim], output=None, compute_kernel_config=None
    )

    return reduced_tensors


def tt_all_gather(input_tensor, mesh_device, cluster_axis, dim, num_links=2, memory_config=None):
    # Ensure the input tensor is in the correct memory configuration
    input_tensor = ttnn.to_memory_config(input_tensor, ttnn.DRAM_MEMORY_CONFIG)

    return ttnn.line_all_gather(
        input_tensor, dim, num_links=num_links, cluster_axis=cluster_axis, mesh_device=mesh_device
    )


def upper_pad_sequence_length(length, padding_size):
    if length % padding_size == 0:
        return length  # No padding needed
    return ((length + padding_size - 1) // padding_size) * padding_size
