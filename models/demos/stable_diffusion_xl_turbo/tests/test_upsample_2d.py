# SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import pytest
import torch
import ttnn
from diffusers import AutoPipelineForText2Image
from tests.ttnn.utils_for_testing import assert_with_pcc
from ttnn.model_preprocessing import preprocess_model_parameters
from models.demos.stable_diffusion_xl_turbo.tt import tt_upsample_2d
from models.demos.stable_diffusion_xl_turbo import custom_preprocessing


@pytest.mark.parametrize("device_params", [{"l1_small_size": 32768}], indirect=True)
@pytest.mark.parametrize(
    "N, C, H, W, index",
    [
        (1, 1280, 32, 32, 0),
        (1, 640, 64, 64, 1),
    ],
)
def test_upsample_1024x1024(device, N, C, H, W, index, reset_seeds):
    pipe = AutoPipelineForText2Image.from_pretrained(
        "stabilityai/sdxl-turbo", torch_dtype=torch.float32, variant="fp16"
    )
    model = pipe.unet
    model.eval()
    config = model.config
    model = model.up_blocks[index].upsamplers[0]
    parameters = preprocess_model_parameters(
        initialize_model=lambda: model, custom_preprocessor=custom_preprocessing.custom_preprocessor
    )
    input_tensor = torch.randn((N, C, H, W), dtype=torch.float32)
    torch_output = model(input_tensor)
    input_tensor = input_tensor.permute(0, 2, 3, 1)
    ttnn_hidden_state = ttnn.from_torch(
        input_tensor, dtype=ttnn.bfloat16, device=device, memory_config=ttnn.L1_MEMORY_CONFIG
    )

    output, [out_height, out_width] = tt_upsample_2d.upsample(ttnn_hidden_state, parameters, device)
    output = ttnn.to_torch(output)
    output = output.reshape(1, out_height, out_width, output.shape[-1])
    output = output.permute(0, 3, 1, 2)

    assert_with_pcc(torch_output, output, 0.99)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 32768}], indirect=True)
@pytest.mark.parametrize(
    "N, C, H, W, index",
    [
        (1, 1280, 16, 16, 0),
        (1, 640, 32, 32, 1),
    ],
)
def test_upsample_512x512(device, N, C, H, W, index, reset_seeds):
    pipe = AutoPipelineForText2Image.from_pretrained(
        "stabilityai/sdxl-turbo", torch_dtype=torch.float32, variant="fp16"
    )
    model = pipe.unet
    model.eval()
    config = model.config
    model = model.up_blocks[index].upsamplers[0]
    parameters = preprocess_model_parameters(
        initialize_model=lambda: model, custom_preprocessor=custom_preprocessing.custom_preprocessor
    )
    input_tensor = torch.randn((N, C, H, W), dtype=torch.float32)
    torch_output = model(input_tensor)
    input_tensor = input_tensor.permute(0, 2, 3, 1)
    ttnn_hidden_state = ttnn.from_torch(
        input_tensor, dtype=ttnn.bfloat16, device=device, memory_config=ttnn.L1_MEMORY_CONFIG
    )

    output, [out_height, out_width] = tt_upsample_2d.upsample(ttnn_hidden_state, parameters, device)
    output = ttnn.to_torch(output)
    output = output.reshape(1, out_height, out_width, output.shape[-1])
    output = output.permute(0, 3, 1, 2)

    assert_with_pcc(torch_output, output, 0.99)
