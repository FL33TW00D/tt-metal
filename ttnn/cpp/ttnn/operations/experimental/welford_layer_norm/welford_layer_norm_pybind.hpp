// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "pybind11/pybind_fwd.hpp"

namespace ttnn::operations::experimental::welford_layer_norm::detail {

namespace py = pybind11;

void bind_experimental_welford_layer_norm_operations(py::module& module);
}  // namespace ttnn::operations::experimental::welford_layer_norm::detail
