// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "cpp/pybind11/decorators.hpp"

#include "ttnn/operations/experimental/welford_layer_norm/welford_layer_norm.hpp"
#include "ttnn/operations/experimental/welford_layer_norm/welford_layer_norm_pybind.hpp"

namespace ttnn::operations::experimental::welford_layer_norm::detail {

namespace py = pybind11;

void bind_experimental_welford_layer_norm_operations(py::module& module) {
    auto welford_layer_norm_doc =
        R"doc(
         Paged update cache operation. This operation expects the following inputs: cache_tensor of shape [B, 1, kv_len, head_dim] and input_tensor of shape [1, B, 1[32], head_dim] where input_tensor is height sharded on B cores. update_idxs will specify for each batch element which token to update in the cache.
        )doc";

    using PagedUpdateCacheType = decltype(ttnn::experimental::welford_layer_norm);
    ttnn::bind_registered_operation(
        module,
        ttnn::experimental::welford_layer_norm,
        welford_layer_norm_doc,
        ttnn::pybind_overload_t{
            [](const PagedUpdateCacheType& self,
               const ttnn::Tensor& cache_tensor,
               const ttnn::Tensor& input_tensor,
               const std::vector<uint32_t>& update_idxs,
               const std::optional<const ttnn::Tensor>& update_idxs_tensor,
               const std::optional<bool> share_cache,
               const std::optional<const ttnn::Tensor>& page_table,
               const uint32_t batch_offset,
               std::optional<const ttnn::DeviceComputeKernelConfig> compute_kernel_config) {
                return self(
                    cache_tensor,
                    input_tensor,
                    update_idxs,
                    update_idxs_tensor,
                    share_cache,
                    page_table,
                    batch_offset,
                    compute_kernel_config);
            },
            py::arg("cache_tensor").noconvert(),
            py::arg("input_tensor").noconvert(),
            py::kw_only(),
            py::arg("update_idxs").noconvert() = std::vector<uint32_t>(),
            py::arg("update_idxs_tensor").noconvert() = std::nullopt,
            py::arg("share_cache").noconvert() = std::nullopt,
            py::arg("page_table").noconvert() = std::nullopt,
            py::arg("batch_offset") = 0,
            py::arg("compute_kernel_config").noconvert() = std::nullopt,
        });
}

}  // namespace ttnn::operations::experimental::welford_layer_norm::detail
