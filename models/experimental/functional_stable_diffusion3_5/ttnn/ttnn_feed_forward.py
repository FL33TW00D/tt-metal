# SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import ttnn

from models.experimental.functional_stable_diffusion3_5.ttnn.ttnn_gelu import ttnn_GELU


class ttnn_FeedForward:
    def __init__(
        self,
        dim: int,
        dim_out=None,
        mult: int = 4,
        activation_fn: str = "geglu",
        inner_dim=None,
        bias: bool = True,
    ):
        if inner_dim is None:
            inner_dim = int(dim * mult)
        dim_out = dim_out if dim_out is not None else dim

        if activation_fn == "gelu":
            act_fn = ttnn_GELU(dim, inner_dim, bias=bias)
        if activation_fn == "gelu-approximate":
            act_fn = ttnn_GELU(dim, inner_dim, approximate="tanh", bias=bias)

        self.net = []

        self.net.append(act_fn)
        self.net.append(ttnn.linear)

    def __call__(self, hidden_states: ttnn.Tensor, parameters=None) -> ttnn.Tensor:
        hifi2_kernel_config = ttnn.WormholeComputeKernelConfig(
            math_fidelity=ttnn.MathFidelity.HiFi2,
        )
        dtype_ff = ttnn.bfloat8_b
        if hidden_states.shape[-2] < 512:
            mm_a_y = 6
            mm_a_x = 8
            mm_a_x_strategy = ttnn.ShardStrategy.WIDTH
            mm_a_x_memory_config = ttnn.L1_WIDTH_SHARDED_MEMORY_CONFIG
            config_used = mm_a_x_memory_config
            # dtype_ff = ttnn.bfloat16
        for module in self.net:
            if module == ttnn.linear:
                if hidden_states.shape[-2] > 512:
                    mm_a_y = 8
                    mm_a_x = 8
                    config_used = ttnn.DRAM_MEMORY_CONFIG
                hidden_states = module(
                    hidden_states,
                    parameters["net"][2]["weight"],
                    bias=parameters["net"][2]["bias"],
                    memory_config=config_used,
                    core_grid=ttnn.CoreGrid(y=mm_a_y, x=mm_a_x),
                    compute_kernel_config=hifi2_kernel_config,
                    dtype=dtype_ff,
                )
                if hidden_states.shape[-2] > 512:
                    hidden_states = ttnn.to_memory_config(hidden_states, memory_config=ttnn.L1_MEMORY_CONFIG)
            else:
                hidden_states = module(hidden_states, parameters=parameters["net"][0])

        return hidden_states
