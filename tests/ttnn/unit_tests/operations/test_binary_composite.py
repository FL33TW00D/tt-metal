# SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch
import pytest
import ttnn
from tests.ttnn.unit_tests.operations.backward.utility_funcs import data_gen_with_range, compare_pcc
from models.utility_functions import is_grayskull


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_hypot_ttnn(input_shapes, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.hypot(input_tensor1, input_tensor2)
    golden_function = ttnn.get_golden_function(ttnn.hypot)
    golden_tensor = golden_function(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_xlogy_ttnn(input_shapes, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.xlogy(input_tensor1, input_tensor2)
    golden_function = ttnn.get_golden_function(ttnn.xlogy)
    golden_tensor = golden_function(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_nextafter_ttnn(input_shapes, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.nextafter(input_tensor1, input_tensor2)
    golden_function = ttnn.get_golden_function(ttnn.nextafter)
    golden_tensor = golden_function(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
@pytest.mark.parametrize("atol", [1.0, 5.0, 10.0])
@pytest.mark.parametrize("rtol", [1.0, 5.0, 10.0])
@pytest.mark.parametrize("equal_nan", [True, False])
def test_binary_isclose_ttnn(input_shapes, atol, rtol, equal_nan, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.isclose(input_tensor1, input_tensor2, rtol=rtol, atol=atol, equal_nan=equal_nan)
    golden_tensor = torch.isclose(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_minimum_ttnn(input_shapes, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.minimum(input_tensor1, input_tensor2)
    golden_function = ttnn.get_golden_function(ttnn.minimum)
    golden_tensor = golden_function(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_maximum_ttnn(input_shapes, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.maximum(input_tensor1, input_tensor2)
    golden_function = ttnn.get_golden_function(ttnn.maximum)
    golden_tensor = golden_function(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_atan2_ttnn(input_shapes, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.atan2(input_tensor1, input_tensor2)
    golden_function = ttnn.get_golden_function(ttnn.atan2)
    golden_tensor = golden_function(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_logical_xor_ttnn(input_shapes, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.logical_xor(input_tensor1, input_tensor2)
    golden_function = ttnn.get_golden_function(ttnn.logical_xor)
    golden_tensor = golden_function(in_data1, in_data2)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
@pytest.mark.parametrize("alpha", [1.0, 5.0, 10.0])
def test_binary_addalpha_ttnn(input_shapes, alpha, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.addalpha(input_tensor1, input_tensor2, alpha)
    golden_function = ttnn.get_golden_function(ttnn.addalpha)
    golden_tensor = golden_function(in_data1, in_data2, alpha)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
@pytest.mark.parametrize("alpha", [1.0, 5.0, 10.0])
def test_binary_subalpha_ttnn(input_shapes, alpha, device):
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
    in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.subalpha(input_tensor1, input_tensor2, alpha)
    golden_function = ttnn.get_golden_function(ttnn.subalpha)
    golden_tensor = golden_function(in_data1, in_data2, alpha)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize("accurate_mode", [False, True])
@pytest.mark.parametrize("round_mode", ["None", "trunc", "floor"])
@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
def test_binary_div_ttnn(accurate_mode, round_mode, input_shapes, device):
    if is_grayskull():
        if round_mode in ["trunc", "floor"]:
            pytest.skip("does not work for Grayskull -skipping")
    if accurate_mode == False:  # If input_b is non-zero tensor
        in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
        in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, -1, device)
    else:
        in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)
        in_data2, input_tensor2 = data_gen_with_range(input_shapes, -150, 150, device)

    output_tensor = ttnn.div(input_tensor1, input_tensor2, accurate_mode=accurate_mode, round_mode=round_mode)
    golden_function = ttnn.get_golden_function(ttnn.div)
    golden_tensor = golden_function(in_data1, in_data2, round_mode)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass


@pytest.mark.parametrize("accurate_mode", [False, True])
@pytest.mark.parametrize("round_mode", ["None", "trunc", "floor"])
@pytest.mark.parametrize(
    "input_shapes",
    (
        (torch.Size([1, 1, 32, 32])),
        (torch.Size([1, 1, 320, 384])),
        (torch.Size([1, 3, 320, 384])),
    ),
)
@pytest.mark.parametrize("value", [1.0, 5.0, 10.0])
def test_binary_div_overload_ttnn(accurate_mode, round_mode, input_shapes, value, device):
    if is_grayskull():
        if round_mode in ["trunc", "floor"]:
            pytest.skip("does not work for Grayskull -skipping")
    in_data1, input_tensor1 = data_gen_with_range(input_shapes, -100, 100, device)

    output_tensor = ttnn.div(input_tensor1, value, accurate_mode=accurate_mode, round_mode=round_mode)
    golden_function = ttnn.get_golden_function(ttnn.div)
    golden_tensor = golden_function(in_data1, value, round_mode)

    comp_pass = compare_pcc([output_tensor], [golden_tensor])
    assert comp_pass
