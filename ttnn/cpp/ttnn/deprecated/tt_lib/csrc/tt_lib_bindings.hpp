// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pybind11/pybind_fwd.hpp"

namespace py = pybind11;

namespace tt {

namespace tt_metal {

void DeviceModuleTypes(py::module& m_device);
void DeviceModule(py::module& m_device);
void ProfilerModule(py::module &m_profiler);

}  // namespace tt_metal

}  // namespace tt
