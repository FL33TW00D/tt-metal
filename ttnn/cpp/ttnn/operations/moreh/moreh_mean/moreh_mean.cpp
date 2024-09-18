// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "moreh_mean.hpp"

#include "ttnn/deprecated/tt_dnn/op_library/moreh_helper_functions.hpp"
#include "ttnn/operations/moreh/moreh_mean/device/moreh_mean_device_operation.hpp"

namespace ttnn::operations::moreh::moreh_mean {
Tensor MorehMean::invoke(
    const Tensor& input,
    const std::optional<std::variant<int64_t, std::vector<int64_t>>> dim,
    const bool keep_batch_dim,
    const std::optional<uint32_t>& divisor,
    const std::optional<Tensor>& output,
    const std::optional<MemoryConfig>& output_memory_config,
    const std::optional<DeviceComputeKernelConfig>& compute_kernel_config) {
    std::vector<int64_t> dims = tt::operations::primary::get_dim(dim, input.get_shape().rank());
    std::sort(dims.begin(), dims.end());

    auto temp_input = input;
    for (uint32_t i = dims.size() - 1; i > 0; i--) {
        log_debug(tt::LogOp, "{}:{} dim {} keep_batch_dim {}", __func__, __LINE__, dims[i], keep_batch_dim);
        auto temp_output = ttnn::prim::moreh_mean(
            temp_input, dims[i], keep_batch_dim, divisor, std::nullopt, output_memory_config, compute_kernel_config);
        temp_input = temp_output;
    }
    log_debug(tt::LogOp, "{}:{} dim {} keep_batch_dim {}", __func__, __LINE__, dims.front(), keep_batch_dim);
    return ttnn::prim::moreh_mean(
        temp_input, dims.front(), keep_batch_dim, divisor, output, output_memory_config, compute_kernel_config);
}
}  // namespace ttnn::operations::moreh::moreh_sum
