// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "moreh_linear.hpp"
#include "ttnn/cpp/ttnn/operations/moreh/moreh_matmul/moreh_matmul.hpp"

namespace ttnn::operations::moreh::moreh_linear {
std::optional<Tensor> MorehLinear::invoke(
    const Tensor& input,
    const Tensor& weight,
    const std::optional<Tensor>& bias,
    std::optional<Tensor> output,
    const std::optional<MemoryConfig>& output_mem_config,
    const DeviceComputeKernelConfig compute_kernel_config) {
    output = ttnn::moreh_matmul(input, weight, false, true, output, bias.value(), output_mem_config, compute_kernel_config);
    return output.value();
}
}  // namespace ttnn::operations::moreh::moreh_linear
