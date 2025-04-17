// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ttnn/operations/core/compute_kernel/compute_kernel_config.hpp"
#include "ttnn/operations/core/core.hpp"

namespace ttnn {
namespace operations::experimental::welford_layer_norm {

struct WelfordLayerNormOperation {
    static ttnn::Tensor invoke(
        const Tensor& cache_tensor,
        const Tensor& input_tensor,
        const std::vector<uint32_t>& update_idxs,
        const std::optional<const Tensor>& update_idxs_tensor,
        const std::optional<bool> share_cache,
        const std::optional<const Tensor>& page_table,
        const uint32_t batch_offset,
        std::optional<const ttnn::DeviceComputeKernelConfig> compute_kernel_config);
};

}  // namespace operations::experimental::welford_layer_norm

namespace experimental {

constexpr auto welford_layer_norm = ttnn::register_operation_with_auto_launch_op<
    "ttnn::experimental::welford_layer_norm",
    ttnn::operations::experimental::welford_layer_norm::WelfordLayerNormOperation>();

}  // namespace experimental

}  // namespace ttnn
