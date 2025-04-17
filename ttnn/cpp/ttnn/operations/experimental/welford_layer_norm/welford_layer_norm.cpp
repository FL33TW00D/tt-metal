// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device/welford_layer_norm_operation.hpp"  // TODO: not right!
#include "ttnn/run_operation.hpp"
#include "ttnn/operations/core/compute_kernel/compute_kernel_config.hpp"
#include "ttnn/tensor/tensor.hpp"
#include "ttnn/operations/core/core.hpp"
#include "ttnn/operations/experimental/welford_layer_norm/welford_layer_norm.hpp"

namespace ttnn {
namespace operations::experimental::welford_layer_norm {

ttnn::Tensor WelfordLayerNormOperation::invoke(
    const Tensor& cache_tensor,
    const Tensor& input_tensor,
    const std::vector<uint32_t>& update_idxs,
    const std::optional<const Tensor>& update_idxs_tensor = std::nullopt,
    const std::optional<bool> share_cache = std::nullopt,
    const std::optional<const Tensor>& page_table = std::nullopt,
    const uint32_t batch_offset = 0,
    std::optional<const ttnn::DeviceComputeKernelConfig> compute_kernel_config = std::nullopt) {
    auto kernel_config_val = init_device_compute_kernel_config(input_tensor.device()->arch(), compute_kernel_config);
    const bool share_cache_arg = share_cache.has_value() ? share_cache.value() : false;  // Default share cache to false
    tt::tt_metal::operation::run(
        WelfordLayerNormDeviceOperation{
            0, update_idxs, batch_offset, WelfordLayerNormOpType::UPDATE, kernel_config_val, share_cache_arg},
        {cache_tensor, input_tensor},
        {update_idxs_tensor, page_table});

    return cache_tensor;  // Updated cache tensor in-place
}
}  // namespace operations::experimental::welford_layer_norm

}  // namespace ttnn
