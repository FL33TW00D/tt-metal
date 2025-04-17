// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <tt-metalium/constants.hpp>
#include <tt-metalium/util.hpp>
#include "ttnn/operations/cb_utils.hpp"
#include "welford_layer_norm_operation.hpp"
#include <tt-metalium/work_split.hpp>

namespace ttnn::operations::experimental::welford_layer_norm::detail {

tt::tt_metal::operation::ProgramWithCallbacks welford_layer_norm_multi_core(
    const Tensor& cache_tensor,
    const Tensor& input_tensor,
    std::optional<const Tensor> update_idxs_tensor,
    std::optional<const Tensor> page_table,
    const std::vector<uint32_t>& update_idxs,
    const uint32_t batch_offset,
    ttnn::DeviceComputeKernelConfig compute_kernel_config,
    const bool share_cache);
}  // namespace ttnn::operations::experimental::welford_layer_norm::detail
