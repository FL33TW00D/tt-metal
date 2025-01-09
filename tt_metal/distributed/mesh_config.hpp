// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

namespace tt::tt_metal::distributed {

using DeviceIds = std::vector<int>;
using MeshDeviceID = int;
using chip_id_t = int;

struct MeshOffset {
    size_t row = 0;
    size_t col = 0;
};

struct MeshShape {
    size_t num_rows = 0;
    size_t num_cols = 0;
};

enum class MeshType { RowMajor, Ring, Line };

struct MeshDeviceConfig {
    MeshShape mesh_shape;
    MeshOffset offset;
    std::vector<chip_id_t> physical_device_ids;
    MeshType mesh_type;

    MeshDeviceConfig(const MeshShape& mesh_shape, MeshType mesh_type) :
        mesh_shape(mesh_shape),
        offset(MeshOffset{0, 0}),
        physical_device_ids(std::vector<chip_id_t>()),
        mesh_type(mesh_type) {}

    MeshDeviceConfig(
        const MeshShape& mesh_shape,
        const MeshOffset& offset = MeshOffset{0, 0},
        const std::vector<chip_id_t>& physical_device_ids = {},
        MeshType mesh_type = MeshType::RowMajor) :
        mesh_shape(mesh_shape), offset(offset), physical_device_ids(physical_device_ids), mesh_type(mesh_type) {}
};

}  // namespace tt::tt_metal::distributed
