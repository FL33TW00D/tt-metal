# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from loguru import logger

import torch
import pytest

from tests.ttnn.utils_for_testing import assert_with_pcc, check_with_pcc, check_with_pcc_without_tensor_printout
import ttnn
import tt_lib
import math
import os
import torch.nn as nn


def run_conv(
    device,
    math_fidelity,
    activations_dtype,
    weights_dtype,
    batch_size,
    output_channels,
    input_channels,
    input_height,
    input_width,
    filter_height,
    filter_width,
    stride_h,
    stride_w,
    pad_h,
    pad_w,
    use_1d_systolic_array,
    config_override,
    use_shallow_conv_variant=False,
    transpose_mcast=True,
    enable_auto_formatting=False,
    padded_input_channels=None,
    fp32_accum=False,
    packer_l1_acc=False,
    output_layout=ttnn.TILE_LAYOUT,
    deallocate_activation=False,
    debug=False,
    groups=1,
):
    # has_bias = False
    has_bias = True
    torch.manual_seed(0)
    conv_input_shape = [batch_size, input_channels, input_height, input_width]
    conv_weight_shape = [output_channels, input_channels // groups, filter_height, filter_width]
    conv_bias_shape = [1, 1, 1, output_channels]
    torch_input_tensor_nchw = torch.randn(conv_input_shape, dtype=torch.bfloat16)  # .float()
    torch_input_tensor = torch.permute(torch_input_tensor_nchw, (0, 2, 3, 1))
    torch_weight_tensor = torch.randn(conv_weight_shape, dtype=torch.bfloat16)  # .float()

    torch_bias_tensor = torch.randn(conv_bias_shape, dtype=torch.bfloat16)  # .float() if has_bias else None
    torch_out_golden_tensor = torch.nn.functional.conv2d(
        torch_input_tensor_nchw,
        torch_weight_tensor,
        bias=torch_bias_tensor.reshape(-1) if has_bias else None,
        stride=(stride_h, stride_w),
        padding=(pad_h, pad_w),
        groups=groups,
    )

    output_shape_nhwc = [
        torch_out_golden_tensor.shape[0],
        torch_out_golden_tensor.shape[2],
        torch_out_golden_tensor.shape[3],
        torch_out_golden_tensor.shape[1],
    ]

    reader_patterns_cache = {}

    tt_weight_tensor = ttnn.from_torch(
        torch_weight_tensor, dtype=ttnn.bfloat16  # if weights_dtype != ttnn.bfloat8_b else ttnn.bfloat16
    )
    tt_bias_tensor = None
    if has_bias:
        tt_bias_tensor = ttnn.from_torch(
            torch_bias_tensor, dtype=ttnn.bfloat16  # if weights_dtype != ttnn.bfloat8_b else ttnn.bfloat16
        )

    tt_input_tensor = ttnn.from_torch(torch_input_tensor, ttnn.bfloat16)

    conv_config = ttnn.Conv2dConfig(
        dtype=ttnn.bfloat16,
        weights_dtype=ttnn.bfloat16,
        math_fidelity=ttnn.MathFidelity.LoFi,
        activation="",
        height_sharding=True,
        math_approx_mode_enabled=True,
        fp32_dest_acc_enabled=False,
        packer_l1_accum_enabled=False,
        input_channels_alignment=32,  # 16 if h.shape[1] < 16 else 32,
        transpose_shards=False,
        reshard_if_not_optimal=True,
        deallocate_activation=True,
        reallocate_halo_output=True,
    )

    [tt_output_tensor_on_device, out_height, out_width, weights_device, bias_device] = ttnn.conv2d(
        input_tensor=tt_input_tensor,
        weight_tensor=tt_weight_tensor,
        in_channels=input_channels,
        out_channels=output_channels,
        device=device,
        bias_tensor=tt_bias_tensor,
        kernel_size=(filter_height, filter_width),
        stride=(stride_h, stride_w),
        padding=(pad_h, pad_w),
        batch_size=batch_size,
        input_height=input_height,
        input_width=input_width,
        conv_config=conv_config,
        conv_op_cache=reader_patterns_cache,
        groups=groups,
    )

    tt_output_tensor = ttnn.from_device(tt_output_tensor_on_device)

    torch_output_tensor = ttnn.to_torch(tt_output_tensor)

    # NHWC to NCHW
    print("Shapes:", torch_output_tensor.shape, " ", torch_out_golden_tensor.shape)
    torch_output_tensor = torch_output_tensor.reshape(batch_size, out_height, out_width, output_channels)

    torch_output_tensor = torch.permute(torch_output_tensor, (0, 3, 1, 2))
    reader_patterns_cache.clear()

    if not fp32_accum:
        pcc = 0.995
    elif math_fidelity == ttnn.MathFidelity.LoFi and activations_dtype == ttnn.bfloat8_b:
        pcc = 0.9969
    else:
        pcc = 0.998
    passing, pcc_msg = check_with_pcc_without_tensor_printout(torch_output_tensor, torch_out_golden_tensor, pcc=pcc)
    print(pcc_msg)
    assert passing


@pytest.mark.parametrize("device_params", [{"l1_small_size": 16384}], indirect=True)
@pytest.mark.parametrize(
    "batch_size, input_channels, output_channels, input_height, input_width, filter_height, filter_width, stride_h, stride_w, pad_h, pad_w, groups, use_1d_systolic_array, config_override, use_shallow_conv_variant",
    (
        (
            1,
            128,
            2,
            8,
            8,
            1,
            1,
            1,
            1,
            0,
            0,
            1,
            True,
            None,
            False,
        ),  # ttnn output shape: ([1, 32, 1, 64])   Expected shape: ([1, 2, 8, 8])
        (
            1,
            256,
            6,
            8,
            8,
            1,
            1,
            1,
            1,
            0,
            0,
            1,
            True,
            None,
            False,
        ),  # ttnn output shape: ([1, 32, 1, 64])   Expected shape: ([1, 6, 8, 8])
        (
            1,
            128,
            24,
            16,
            16,
            1,
            1,
            1,
            1,
            0,
            0,
            1,
            True,
            None,
            False,
        ),  # ttnn output shape: ([1, 32, 1, 256])  Expected shape: ([1, 24, 16, 16])
        (
            1,
            256,
            72,
            8,
            8,
            1,
            1,
            1,
            1,
            0,
            0,
            1,
            True,
            None,
            False,
        ),  # ttnn output shape: ([1, 96, 1, 64])   Expected shape: ([1, 72, 8, 8])
    ),
)
@pytest.mark.parametrize(
    "weights_dtype",
    [ttnn.bfloat16],
)
@pytest.mark.parametrize(
    "activations_dtype",
    [ttnn.bfloat16],
)
@pytest.mark.parametrize("math_fidelity", [ttnn.MathFidelity.LoFi])
@pytest.mark.parametrize("output_layout", [ttnn.TILE_LAYOUT])
def test_blazepose_conv(
    device,
    use_program_cache,
    math_fidelity,
    activations_dtype,
    weights_dtype,
    batch_size,
    output_channels,
    input_channels,
    input_height,
    input_width,
    filter_height,
    filter_width,
    stride_h,
    stride_w,
    pad_h,
    pad_w,
    use_1d_systolic_array,
    config_override,
    use_shallow_conv_variant,
    groups,
    output_layout,
):
    run_conv(
        device,
        math_fidelity,
        activations_dtype,
        weights_dtype,
        batch_size,
        output_channels,
        input_channels,
        input_height,
        input_width,
        filter_height,
        filter_width,
        stride_h,
        stride_w,
        pad_h,
        pad_w,
        use_1d_systolic_array,
        config_override,
        use_shallow_conv_variant=use_shallow_conv_variant,
        groups=groups,
        output_layout=output_layout,
    )
