// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ttnn/core.hpp"
#include "ttnn/types.hpp"
#include "ttnn/run_operation.hpp"
#include "ttnn/operations/core/core.hpp"
#include "ttnn/tensor/tensor.hpp"
#include "ttnn/tensor/tensor_utils.hpp"
#include "ttnn/tensor/host_buffer/functions.hpp"

#include "device/fold_device_op.hpp"

namespace ttnn {
namespace operations::data_movement {

struct FoldOperation {
    static ttnn::Tensor operator()(
        const ttnn::Tensor &input_tensor,
        uint8_t stride_h,
        uint8_t stride_w,
        bool use_transpose_as_fold = false,
        const std::optional<const tt::tt_metal::Shape> &output_shape = std::nullopt,
        uint8_t pad_c = 0,
        uint8_t pad_h = 0,
        uint8_t pad_w = 0);
    static ttnn::Tensor operator()(
        uint8_t queue_id,
        const ttnn::Tensor &input_tensor,
        uint8_t stride_h,
        uint8_t stride_w,
        bool use_transpose_as_fold = false,
        const std::optional<const tt::tt_metal::Shape> &output_shape = std::nullopt,
        uint8_t pad_c = 0,
        uint8_t pad_h = 0,
        uint8_t pad_w = 0);

};

} // namespace operations::data_movement

constexpr auto fold = register_operation<"ttnn::fold", operations::data_movement::FoldOperation>();

} // namespace ttnn
