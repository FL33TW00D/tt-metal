// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ttnn/cpp/pybind11/decorators.hpp"

#include "ttnn/operations/reduction/plusone/plusone.hpp"

namespace ttnn::operations::reduction::detail {
namespace py = pybind11;
void bind_reduction_plusone_operation(py::module& module) {
    auto doc =
        R"doc(plus_one(input_tensor: ttnn.Tensor) -> ttnn.Tensor

            Returns input tensor elementes increased by 1.
            Input tensor must have UINT32 data type and ROW_MAJOR layout.

            Equivalent pytorch code:

            .. code-block:: python

                return torch.add(input_tensor, 1)

            Args:
                * :attr:`input_tensor`: Input Tensor for plusone.

        )doc";

    using OperationType = decltype(ttnn::plus_one);
    bind_registered_operation(
        module,
        ttnn::plus_one,
        doc,
        ttnn::pybind_overload_t{
            [] (const OperationType& self,
                const ttnn::Tensor& input_tensor
                ) {
                    return self(input_tensor);
                },
                py::arg("input_tensor").noconvert()});
}

}  // namespace ttnn::operations::reduction::detail
