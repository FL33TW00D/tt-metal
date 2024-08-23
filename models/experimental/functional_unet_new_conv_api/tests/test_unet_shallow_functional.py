# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import pytest

import torch
from loguru import logger

from models.utility_functions import (
    skip_for_wormhole_b0,
    skip_for_grayskull,
)
from tests.ttnn.utils_for_testing import assert_with_pcc
import ttnn
from models.experimental.functional_unet_new_conv_api.unet_utils import create_unet_models, create_unet_input_tensors

import tt_lib as ttl


@skip_for_grayskull()
@pytest.mark.parametrize("device_params", [{"l1_small_size": 32768}], indirect=True)
@pytest.mark.parametrize("perf_mode", [True])
@pytest.mark.parametrize("batch", [2])
@pytest.mark.parametrize("groups", [1])
def test_unet_pcc(device, perf_mode, batch, groups):
    with torch.no_grad():
        torch.manual_seed(0)

        # Create initial parameters
        torch_input_tensor, ttnn_input_tensor = create_unet_input_tensors(device, batch, groups)
        torch_model, ttnn_model = create_unet_models(device, batch, groups, torch_input_tensor)

        # Run torch golden result
        torch_output_tensor = torch_model(torch_input_tensor)
        print(torch_output_tensor.shape)

        # Run ttnn output result
        output_tensor = ttnn_model(device, ttnn_input_tensor, list(torch_input_tensor.shape), perf_mode=perf_mode)

        # Tensor postprocessing
        input_shape = torch_input_tensor.shape
        output_tensor = ttnn.to_torch(output_tensor)
        print(output_tensor.shape)
        output_tensor = output_tensor[:, :, :, :1]
        output_tensor = output_tensor.reshape(input_shape[0], input_shape[2], input_shape[3], -1)
        output_tensor = torch.permute(output_tensor, (0, 3, 1, 2))
        # output_tensor = output_tensor.reshape(2, 16, 528, 80)
        output_tensor = output_tensor.to(torch_input_tensor.dtype)
        print(output_tensor)
        # Disable pcc checking due to hang
        if device.arch() == ttl.device.Arch.WORMHOLE_B0:
            assert_with_pcc(torch_output_tensor, output_tensor, pcc=0.99)
        else:
            assert_with_pcc(torch_output_tensor, output_tensor, pcc=0.97)
