# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import ttnn
import timm
import torch
import pytest
from tests.ttnn.utils_for_testing import assert_with_pcc
from ttnn.model_preprocessing import preprocess_model_parameters

from models.experimental.functional_vovnet.tt import tt_vovnet
from loguru import logger


@pytest.mark.parametrize("device_params", [{"l1_small_size": 16384}], indirect=True)
def test_effective_se_module(
    reset_seeds,
    device,
):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True)
    rf_model = rf_model.eval()
    model = rf_model.stages[0].blocks[0].attn

    parameters = preprocess_model_parameters(
        initialize_model=lambda: rf_model,
        convert_to_ttnn=lambda *_: False,
    )

    torch_input = torch.randn((1, 256, 56, 56), dtype=torch.bfloat16)
    ttnn_input = ttnn.from_torch(
        torch_input, dtype=ttnn.bfloat16, device=device, layout=ttnn.TILE_LAYOUT
    )  # , device=device
    torch_output = model(torch_input.float())

    tt_output = tt_vovnet.effective_se_module(
        device=device,
        torch_model=rf_model.state_dict(),
        path="stages.0.blocks.0.attn",
        input_tensor=ttnn_input,
        input_params=torch_input.shape,
        conv_params=(1, 1, 0, 0),
        debug=False,
        bias=True,
        parameters=parameters.stages[0].blocks[0].attn,
    )
    tt_output = ttnn.to_torch(tt_output)

    assert_with_pcc(torch_output, tt_output, 0.9999)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 16384}], indirect=True)
def test_conv_norm_act(
    reset_seeds,
    device,
):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True)
    rf_model = rf_model.eval()
    model = rf_model.stem[0]

    torch_input = torch.randn((1, 224, 224, 3), dtype=torch.bfloat16)
    ttnn_input = ttnn.from_torch(torch_input, dtype=ttnn.bfloat16)
    torch_input = torch_input.permute(0, 3, 1, 2).float()
    torch_output = model(torch_input)

    tt_output, tt_output_h, tt_output_w = tt_vovnet.conv_norm_act(
        device=device,
        x=ttnn_input,
        torch_model=rf_model.state_dict(),
        path="stem.0",
        input_params=ttnn_input.shape,
        conv_params=[2, 2, 1, 1],
        debug=False,
        bias=False,
        activation="relu",
    )

    tt_output = ttnn.to_torch(tt_output)
    torch_output = torch_output.permute(0, 2, 3, 1)
    tt_output = tt_output.reshape(torch_output.shape)

    assert_with_pcc(torch_output, tt_output, 0.9997)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 16384}], indirect=True)
def test_seperable_conv_norm_act(
    reset_seeds,
    device,
):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True)
    rf_model = rf_model.eval()

    model = rf_model.stages[0].blocks[0].conv_mid[0]
    print("the model is", model)
    torch_input = torch.randn((1, 56, 56, 128), dtype=torch.bfloat16)
    ttnn_input = ttnn.from_torch(torch_input, dtype=ttnn.bfloat16)
    torch_input = torch_input.permute(0, 3, 1, 2)
    torch_output = model(torch_input.float())

    parameters = preprocess_model_parameters(
        initialize_model=lambda: rf_model,
        convert_to_ttnn=lambda *_: False,
    )

    tt_output, conv_h, conv_w = tt_vovnet.seperable_conv_norm_act(
        0,
        device=device,
        x=ttnn_input,
        torch_model=rf_model.state_dict(),
        parameters=parameters,
        model=model,
        path="stages.0.blocks.0.conv_mid.0",
        input_params=ttnn_input.shape,
        conv_params1=[1, 1, 1, 1],
        conv_params2=[1, 1, 0, 0],
        debug=False,
        groups=128,
        bias=False,
        act_block_h=None,
    )

    tt_output = ttnn.to_torch(tt_output)
    tt_output = tt_output.reshape(1, conv_h, conv_w, tt_output.shape[-1])
    tt_output = tt_output.permute(0, 3, 1, 2)

    assert_with_pcc(torch_output, tt_output, 1)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 16384}], indirect=True)
def test_sequential_append_list(reset_seeds, device, imagenet_sample_input):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True).eval()
    model = rf_model.stages[1].blocks[0].conv_mid

    torch_input = torch.randn((1, 28, 28, 160), dtype=torch.bfloat16)
    ttnn_input = ttnn.from_torch(torch_input, dtype=ttnn.bfloat16)

    inputs = torch.randn((1, 28, 28, 256), dtype=torch.bfloat16)
    ttnn_inputs = ttnn.from_torch(inputs, dtype=ttnn.bfloat16)

    torch_input = torch_input.permute(0, 3, 1, 2).float()
    inputs = inputs.permute(0, 3, 1, 2).float()
    torch_output = model(torch_input, [inputs])

    tt_output, h, w = tt_vovnet.sequential_append_list(
        device=device,
        input_tensor=ttnn_input,
        torch_model=rf_model.state_dict(),
        path="stages.1.blocks.0.conv_mid",
        concat_list=[ttnn_inputs],
        conv_params1=[1, 1, 1, 1],
        conv_params2=[1, 1, 0, 0],
        debug=False,
        groups=160,
        layers_per_block=3,
        bias=False,
    )
    tt_output = ttnn.to_torch(tt_output)
    torch_output = torch_output.permute(0, 2, 3, 1)
    tt_output = tt_output.reshape(torch_output.shape)

    assert_with_pcc(torch_output, tt_output, 0.9955)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 32768}], indirect=True)
def test_osa_block(reset_seeds, device, imagenet_sample_input):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True).eval()
    # tr_out = rf_model(imagenet_sample_input)
    model = rf_model.stages[1].blocks[0]
    # print(f"model: {model}")

    torch_input = torch.randn((1, 28, 28, 256), dtype=torch.bfloat16)
    ttnn_input = ttnn.from_torch(torch_input, dtype=ttnn.bfloat16)
    torch_input = torch_input.permute(0, 3, 1, 2)
    torch_output = model(torch_input.float())

    parameters = preprocess_model_parameters(
        initialize_model=lambda: rf_model,
        convert_to_ttnn=lambda *_: False,
    )

    tt_output = tt_vovnet.osa_block(
        device=device,
        x=ttnn_input,
        torch_model=rf_model.state_dict(),
        path="stages.1.blocks.0",
        parameters=parameters.stages[1].blocks[0],
        model=rf_model.stages[1].blocks[0],
        groups=160,
        conv_norm_act_params=[1, 1, 0, 0],
        conv_params1=[1, 1, 1, 1],
        conv_params2=[1, 1, 0, 0],
        layers_per_block=3,
        residual=False,
        depthwise=True,
        debug=False,
        bias=False,
    )

    tt_output = ttnn.to_torch(tt_output)

    assert_with_pcc(torch_output, tt_output, 0.94402)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 16384}], indirect=True)
def test_osa_stage(
    reset_seeds,
    device,
):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True).eval()
    model = rf_model.stages[0]

    parameters = preprocess_model_parameters(
        initialize_model=lambda: rf_model,
        convert_to_ttnn=lambda *_: False,
    )

    torch_input = torch.randn((1, 56, 56, 64), dtype=torch.bfloat16)
    ttnn_input = ttnn.from_torch(torch_input, dtype=ttnn.bfloat16)
    torch_input = torch_input.permute(0, 3, 1, 2)
    torch_output = model(torch_input.float())

    tt_output = tt_vovnet.osa_stage(
        device=device,
        x=ttnn_input,
        torch_model=rf_model.state_dict(),
        path=f"stages.0",
        parameters=parameters.stages[0],
        model=rf_model.stages[0],
        groups=128,
        block_per_stage=1,
        layer_per_block=3,
        residual=False,
        depthwise=True,
        downsample=False,
        bias=False,
    )
    tt_output = ttnn.to_torch(tt_output)
    assert_with_pcc(torch_output, tt_output, 0.9696)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 16384}], indirect=True)
def test_classifier_head(
    reset_seeds,
    device,
):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True)
    rf_model = rf_model.eval()
    model = rf_model.head

    torch_input = torch.randn((1, 7, 7, 1024), dtype=torch.bfloat16)
    ttnn_input = ttnn.from_torch(torch_input, dtype=ttnn.bfloat16, device=device)
    torch_input = torch_input.permute(0, 3, 1, 2)
    torch_output = model(torch_input.float())

    tt_output = tt_vovnet.classifier_head(device=device, x=ttnn_input, torch_model=rf_model.state_dict(), path="head")
    tt_output = ttnn.to_torch(tt_output)
    tt_output = tt_output.reshape(torch_output.shape)

    assert_with_pcc(torch_output, tt_output, 0.9998)


@pytest.mark.parametrize("device_params", [{"l1_small_size": 32768}], indirect=True)
def test_vovnet(reset_seeds, device, imagenet_sample_input, imagenet_label_dict):
    rf_model = timm.create_model("hf_hub:timm/ese_vovnet19b_dw.ra_in1k", pretrained=True)
    rf_model = rf_model.eval()

    # torch_input = torch.randn((1, 224, 224, 3), dtype=torch.bfloat16)
    torch_input = imagenet_sample_input
    t2 = torch_input.permute(0, 2, 3, 1)
    ttnn_input = ttnn.from_torch(t2, dtype=ttnn.bfloat16)
    # torch_input = torch_input.permute(0, 3, 1, 2)
    torch_output = rf_model(torch_input.float())

    parameters = preprocess_model_parameters(
        initialize_model=lambda: rf_model,
        convert_to_ttnn=lambda *_: False,
    )

    tt_output = tt_vovnet.vovnet(
        device=device,
        x=ttnn_input,
        torch_model=rf_model.state_dict(),
        parameters=parameters,
        model=rf_model,
        block_per_stage=1,
        residual=False,
        depthwise=True,
        debug=False,
        bias=False,
    )

    tt_output = ttnn.to_torch(tt_output)

    assert_with_pcc(torch_output, tt_output.squeeze(0).squeeze(0), 1)
