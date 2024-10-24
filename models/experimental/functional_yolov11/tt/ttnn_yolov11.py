# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import ttnn
import torch
from ttnn.model_preprocessing import ParameterDict, fold_batch_norm2d_into_conv2d
from torch import nn
from ttnn.model_preprocessing import preprocess_linear_weight, preprocess_linear_bias
import math
import torch.nn.functional as F
from tt_lib.utils import (
    _nearest_y,
)
from tests.ttnn.ttnn_utility_fuction import get_shard_grid_from_num_cores


def input_preprocessing(x, N, C, H, W):
    x = ttnn.to_torch(x)
    x = torch.permute(x, (0, 3, 1, 2))
    x = x.reshape(N, C, H, W)
    return x


class Yolov11_Conv2D:
    def __init__(
        self,
        conv,
        conv_pth,
        bn=None,
        device=None,
        cache={},
        activation="",
        activation_dtype=ttnn.bfloat8_b,
        weights_dtype=ttnn.bfloat8_b,
        use_1d_systolic_array=True,
        shard_layout=ttnn.TensorMemoryLayout.HEIGHT_SHARDED,
    ):
        self.device = device
        self.batch_size = conv.batch_size
        self.input_height = conv.input_height
        self.input_width = conv.input_width
        self.in_channels = conv.in_channels
        self.out_channels = conv.out_channels
        self.kernel_size = conv.kernel_size
        self.padding = conv.padding
        self.stride = conv.stride
        self.groups = conv.groups
        self.use_1d_systolic_array = use_1d_systolic_array
        self.deallocate_activation = True
        self.cache = cache

        self.conv_config = ttnn.Conv2dConfig(
            dtype=activation_dtype,
            weights_dtype=weights_dtype,
            math_fidelity=ttnn.MathFidelity.LoFi,
            shard_layout=shard_layout,
            deallocate_activation=self.deallocate_activation,
            fp32_dest_acc_enabled=True,
            packer_l1_accum_enabled=False,
            enable_act_double_buffer=False,
            enable_split_reader=False,
            enable_subblock_padding=False,
            reshard_if_not_optimal=True if self.use_1d_systolic_array else False,
            activation=activation,
        )
        config_override = conv.conv_blocking_and_parallelization_config_override
        if config_override and "act_block_h" in config_override:
            self.conv_config.act_block_h_override = config_override["act_block_h"]

        if bn is not None:
            weight, bias = fold_batch_norm2d_into_conv2d(conv_pth, bn.module)
        else:
            weight, bias = conv_pth.weight, conv_pth.bias

        weight = weight
        if bias is not None:
            bias = torch.reshape(bias, (1, 1, 1, -1))
            self.bias = ttnn.from_torch(bias, dtype=ttnn.float32)
        else:
            self.bias = None

        self.weight = ttnn.from_torch(weight, dtype=ttnn.float32)

    def __call__(self, x):
        x, output_height, output_width, self.weight, self.bias = ttnn.conv2d(
            input_tensor=x,
            weight_tensor=self.weight,
            bias_tensor=self.bias,
            device=self.device,
            in_channels=self.in_channels,
            out_channels=self.out_channels,
            input_height=self.input_height,
            input_width=self.input_width,
            batch_size=self.batch_size,
            kernel_size=self.kernel_size,
            stride=self.stride,
            padding=self.padding,
            conv_config=self.conv_config,
            conv_op_cache=self.cache,
            groups=self.groups,
        )
        return x


def Yolov11_shard_SiLU(device, x, ncores=64):
    input_2d_height = x.shape.with_tile_padding()[2]
    input_2d_width = x.shape.with_tile_padding()[3]

    input_2d_height_padded = _nearest_y(input_2d_height, ncores * 32)

    shard_height = math.ceil(input_2d_height_padded / ncores)
    shard_grid = get_shard_grid_from_num_cores(ncores, device)
    shard_width = input_2d_width
    shard_orientation = ttnn.ShardOrientation.ROW_MAJOR
    tensor_memory_layout = ttnn.TensorMemoryLayout.HEIGHT_SHARDED

    shard_spec = ttnn.ShardSpec(shard_grid, (shard_height, shard_width), shard_orientation, False)

    in_sharded_mem_config = ttnn.MemoryConfig(tensor_memory_layout, ttnn.BufferType.L1, shard_spec)

    x = ttnn.to_memory_config(x, memory_config=in_sharded_mem_config)

    x = ttnn.silu(x, memory_config=in_sharded_mem_config)
    return x


class Conv:
    def __init__(self, device, parameter, conv_pt, enable_act=True):
        self.enable_act = enable_act
        self.conv = Yolov11_Conv2D(parameter.conv, conv_pt.conv, device=device)

    def __call__(self, device, x):
        if self.enable_act:
            x = self.conv(x)
            x = Yolov11_shard_SiLU(device, x)
        else:
            x = self.conv(x)
        return x


class Bottleneck:
    def __init__(self, device, parameter, conv_pt):
        self.cv1 = Conv(device, parameter.cv1, conv_pt.cv1)
        self.cv2 = Conv(device, parameter.cv2, conv_pt.cv2)

    def __call__(self, device, x):
        input = x
        x = self.cv1(device, x)
        x = self.cv2(device, x)
        if x.shape != input.shape:
            input = input[:, :, : x.shape[2], : x.shape[3]]
        return input + x


class SPPF:
    def __init__(self, device, parameter, conv_pt):
        self.parameter = parameter
        self.cv1 = Conv(device, parameter.cv1, conv_pt.cv1)
        self.cv2 = Conv(device, parameter.cv2, conv_pt.cv2)
        self.m = nn.MaxPool2d(kernel_size=5, stride=1, padding=2)

    def __call__(self, device, x):
        x = self.cv1(device, x)
        x1 = x

        x = input_preprocessing(
            x,
            self.parameter.cv2.conv.batch_size,
            self.parameter.cv2.conv.in_channels,
            self.parameter.cv2.conv.input_height,
            self.parameter.cv2.conv.input_width,
        )

        m1 = self.m(x)
        m2 = self.m(m1)
        m3 = self.m(m2)

        m1 = ttnn.from_torch(m1, dtype=ttnn.bfloat16, layout=ttnn.TILE_LAYOUT, device=self.device)
        m1 = ttnn.to_memory_config(m1, ttnn.L1_MEMORY_CONFIG)

        m2 = ttnn.from_torch(m2, dtype=ttnn.bfloat16, layout=ttnn.TILE_LAYOUT, device=self.device)
        m2 = ttnn.to_memory_config(m2, ttnn.L1_MEMORY_CONFIG)

        m3 = ttnn.from_torch(m3, dtype=ttnn.bfloat16, layout=ttnn.TILE_LAYOUT, device=self.device)
        m3 = ttnn.to_memory_config(m3, ttnn.L1_MEMORY_CONFIG)

        y = ttnn.concat([x1, m1, m2, m3], dim=1)
        y = ttnn.permute(y, (0, 2, 3, 1))

        x = self.cv2(device, y)
        return x


class C3K:
    def __init__(self, device, parameter, conv_pt):
        self.cv1 = Conv(device, parameter.cv1, conv_pt.cv1)
        self.cv2 = Conv(device, parameter.cv2, conv_pt.cv2)
        self.cv3 = Conv(device, parameter.cv3, conv_pt.cv3)
        self.k1 = Bottleneck(device, parameter.k1, conv_pt.k1)
        self.k2 = Bottleneck(device, parameter.k2, conv_pt.k2)

    def __call__(self, device, x):
        x1 = self.cv1(device, x)
        x2 = self.cv2(device, x)

        k1 = self.k1(device, x1)
        k2 = self.k2(device, k1)

        if x2.shape != k2.shape:
            x2 = x2[:, :, : k2.shape[2], : k2.shape[3]]

        if x2.is_sharded():
            x2 = ttnn.sharded_to_interleaved(x2, ttnn.L1_MEMORY_CONFIG)
        if k2.is_sharded():
            k2 = ttnn.sharded_to_interleaved(k2, ttnn.L1_MEMORY_CONFIG)

        k2 = ttnn.to_layout(k2, ttnn.ROW_MAJOR_LAYOUT)
        x2 = ttnn.to_layout(x2, ttnn.ROW_MAJOR_LAYOUT)

        x = ttnn.concat((k2, x2), 3)
        x = self.cv3(device, x)
        return x


class C3k2:
    def __init__(self, device, parameter, conv_pt, is_bk_enabled=False):
        self.is_bk_enabled = is_bk_enabled

        if is_bk_enabled:
            self.cv1 = Conv(device, parameter.cv1, conv_pt.cv1)
            self.cv2 = Conv(device, parameter.cv2, conv_pt.cv2)
            self.k = Bottleneck(device, parameter.k, conv_pt.k)
        else:
            self.cv1 = Conv(device, parameter.cv1, conv_pt.cv1)
            self.cv2 = Conv(device, parameter.cv2, conv_pt.cv2)
            self.c3k = C3K(device, parameter.c3k, conv_pt.c3k)

    def __call__(self, device, x):
        if self.is_bk_enabled:
            x = self.cv1(device, x)

            if x.is_sharded():
                x = ttnn.sharded_to_interleaved(x, ttnn.L1_MEMORY_CONFIG)
            x = ttnn.to_layout(x, ttnn.ROW_MAJOR_LAYOUT)

            y1, y2 = ttnn.split(x, 2, 3)
            y2 = ttnn.to_layout(y2, ttnn.TILE_LAYOUT)

            y3 = self.k(device, y2)

            y2 = ttnn.to_layout(y2, ttnn.ROW_MAJOR_LAYOUT)
            y3 = ttnn.to_layout(y3, ttnn.ROW_MAJOR_LAYOUT)

            x = ttnn.concat((y1, y2, y3), 3)

            x = ttnn.to_layout(x, ttnn.TILE_LAYOUT)
            x = self.cv2(device, x)
        else:
            x = self.cv1(device, x)

            if x.is_sharded():
                x = ttnn.sharded_to_interleaved(x, ttnn.L1_MEMORY_CONFIG)

            y1, y2 = ttnn.split(x, 2, 3)
            y3 = self.c3k(device, y2)

            if y1.is_sharded():
                y1 = ttnn.sharded_to_interleaved(y1, ttnn.L1_MEMORY_CONFIG)
            if y2.is_sharded():
                y2 = ttnn.sharded_to_interleaved(y2, ttnn.L1_MEMORY_CONFIG)
            if y3.is_sharded():
                y3 = ttnn.sharded_to_interleaved(y3, ttnn.L1_MEMORY_CONFIG)

            x = ttnn.concat((y1, y2, y3), 3)
            x = self.cv2(device, x)
        return x


class YoloV11:
    def __init__(self, device, parameters):
        self.device = device
        self.conv1 = Conv(device, parameters.conv1, parameters.conv1.module)
        self.conv2 = Conv(device, parameters.conv2, parameters.conv2.module)
        self.c3k2_1 = C3k2(device, parameters.c3k2_1, parameters.c3k2_1.module, is_bk_enabled=True)
        self.conv3 = Conv(device, parameters.conv3, parameters.conv3.module)
        self.c3k2_2 = C3k2(device, parameters.c3k2_2, parameters.c3k2_2.module, is_bk_enabled=True)
        self.conv5 = Conv(device, parameters.conv5, parameters.conv5.module)
        self.c3k2_3 = C3k2(device, parameters.c3k2_3, parameters.c3k2_3.module, is_bk_enabled=False)
        # self.conv6 = Conv(device, parameters.conv6, parameters.conv6.module)
        # self.c3k2_4 = C3k2(device, parameters.c3k2_4, parameters.c3k2_4.module, is_bk_enabled=True)

    def __call__(self, x):
        x = self.conv1(self.device, x)
        x = self.conv2(self.device, x)
        x = self.c3k2_1(self.device, x)
        x = self.conv3(self.device, x)  # 3
        x = self.c3k2_2(self.device, x)  # 4
        x4 = x
        x = self.conv5(self.device, x)  # 5
        x = self.c3k2_3(self.device, x)  # 6
        return x
        x6 = x
        x = self.conv6(self.device, x)  # 7
        x = self.c3k2_4(self.device, x)  # 8

        return x
