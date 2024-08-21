// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include "tt_metal/impl/device/device_mesh.hpp"
#include "tt_metal/impl/device/device_mesh_view.hpp"
#include "tt_metal/host_api.hpp"
#include "tt_metal/detail/tt_metal.hpp"

namespace tt::tt_metal {

DeviceMesh::DeviceMesh(
    const DeviceGrid& device_grid, size_t l1_small_size, size_t trace_region_size, size_t num_command_queues, DispatchCoreType dispatch_core_type) :
    device_grid(device_grid) {
    auto [num_rows, num_cols] = device_grid;
    auto num_requested_devices = num_rows * num_cols;
    auto num_available_devices = tt::tt_metal::GetNumAvailableDevices();
    TT_ASSERT(num_requested_devices <= num_available_devices, "Requested more devices than available");

    int max_row = 0;
    int max_col = 0;
    DeviceIds required_devices;
    for (int physical_chip_id : tt::Cluster::instance().get_all_user_chip_ids()) {
        auto eth_coord = tt::Cluster::instance().get_ethernet_coord(physical_chip_id);
        max_col = std::max(max_col, std::get<0>(eth_coord));
        max_row = std::min(max_row, std::get<1>(eth_coord));
        if (std::get<0>(eth_coord) < num_cols and std::get<1>(eth_coord) < num_rows) {
            required_devices.push_back(physical_chip_id);
        }
    }
    TT_FATAL(num_rows <= max_row + 1, "Requested more rows than available");
    TT_FATAL(num_cols <= max_cols + 1, "Requested more rows than available");
    this->device_grid.resize(num_rows);
    for (int i = 0; i < num_rows; i++) {
        this->device_grid[i].resize(num_cols);
    }

    if (tt::Cluster::instance().is_galaxy_cluster()) {
        // Temp solution until we add algorithmic way to determine chip connectivity
        // Map col to tunnel depth and row to tunnel count
        int cluster_tunnel_depth = tt::Cluster::instance().get_mmio_device_max_tunnel_depth(0);
        int cluster_tunnel_count = tt::Cluster::instance().get_mmio_device_tunnel_count(0);
        int num_mmio_devices = tt::Cluster::instance().number_of_pci_devices();
        TT_FATAL(num_cols <= cluster_tunnel_depth and num_rows <= cluster_tunnel_count * num_mmio_devices, "Unsupported Galaxy mesh shape");

        DeviceIds galaxy_device_ids;
        for (int mmio_device_id = 0; mmio_device_id < num_mmio_devices; mmio_device_id++) {
            auto tunnels_from_mmio = tt::Cluster::instance().get_tunnels_from_mmio_device(mmio_device_id);
            for (uint32_t t = 0; t < tunnels_from_mmio.size(); t++) {
                if (galaxy_device_ids.size() == num_requested_devices) {
                    break;
                }
                int col_idx = 0;
                for (uint32_t ts = 1; ts < tunnels_from_mmio[t].size(); ts++) {
                    galaxy_device_ids.push_back(tunnels_from_mmio[t][ts]);
                    col_idx ++;
                    if (col_idx == num_cols) {
                        break;
                    }
                }
            }
        }
        managed_devices = tt::tt_metal::detail::CreateDevices(galaxy_device_ids, num_command_queues, l1_small_size, trace_region_size, dispatch_core_type);
        for (int i = 0; i < num_requested_devices; i++) {
            mesh_devices.emplace_back(i, managed_devices.at(galaxy_device_ids[i]));
        }
    } else {
        managed_devices = tt::tt_metal::detail::CreateDevices(required_device_ids, num_command_queues, l1_small_size, trace_region_size, dispatch_core_type);
        for (int i = 0; i < num_requested_devices; i++) {
            mesh_devices.emplace_back(i, managed_devices.at(i));
        }
        for (int i = 0; i < num_rows; i++) {
            for (int j = 0; j < num_cols; j++) {
                this->device_grid[i][j] =
            }
        }
    }

    for (const auto& [dev_id, dev]: mesh_devices) {
        log_debug(tt::LogMetal, "TTNN Dev {}: Metal Dev {}", dev_id, dev->id());
    }
    this->view = std::make_unique<tt::tt_metal::DeviceMeshView>(*this);
}

DeviceMesh::~DeviceMesh() {
    if (not managed_devices.empty()) {
        close_devices();
    }
}

Device* DeviceMesh::get_device(int logical_device_id) const {
    for (const auto& [device_id, device] : mesh_devices) {
        if (device_id == logical_device_id) {
            return device;
        }
    }
    TT_THROW("User has provided an invalid device index");
}

std::vector<Device*> DeviceMesh::get_devices() const
{
    std::vector<Device*> devices;
    for (const auto& [device_id, device] : mesh_devices) {
        devices.push_back(device);
    }
    return devices;
}

Device* DeviceMesh::get_device(int row_idx, int col_idx) const {
    TT_FATAL(
        this->num_rows() != 0 and this->num_cols() != 0,
        "#10419, Current device mesh does not support indexing by row or col indices.");
    TT_FATAL(row_idx >= 0 and row_idx < this->num_rows(), "Invalid row index.");
    TT_FATAL(col_idx >= 0 and col_idx < this->num_cols(), "Invalid col index.");
    int idx = row_idx * this->num_cols() + col_idx;
    return this->mesh_devices[idx].second;
}

std::vector<Device*> DeviceMesh::get_devices_on_row(int row_idx) const {
    return this->view->get_devices_on_row(row_idx);
}

std::vector<Device*> DeviceMesh::get_devices_on_column(int col_idx) const {
    return this->view->get_devices_on_column(col_idx);
}

std::vector<Device*> DeviceMesh::get_devices_on_ring() const {
    const auto& devices = this->get_devices();
    TT_ASSERT(not devices.empty(), "No devices in the mesh");

    return this->view->get_devices_on_ring(devices, devices[0]->id(), devices.size());
}

void DeviceMesh::reorder_devices_to_ring() {
    // Temporary api to reorder devices to ring
    // We should instead update apis that call get_devices() to use get_devices_on_ring()
    const auto& ring_devices = this->get_devices_on_ring();
    int swap_index = 0;
    for (int i = 0; i < ring_devices.size(); i++) {
        this->mesh_devices[i].second = ring_devices[i];
    }
}

const DeviceIds DeviceMesh::get_device_ids() const
{
    // Return the logical device ids
    DeviceIds device_ids;
    for (const auto& [device_id, device] : mesh_devices) {
        // device_id is not always equal to device->id()
        device_ids.push_back(device_id);
    }
    return device_ids;
}

int DeviceMesh::num_devices() const
{
    return mesh_devices.size();
}

CoreCoord DeviceMesh::compute_with_storage_grid_size() const {
    return mesh_devices.at(0).second->compute_with_storage_grid_size();
}

CoreCoord DeviceMesh::dram_grid_size() const {
    return mesh_devices.at(0).second->dram_grid_size();
}

tt::ARCH DeviceMesh::arch() const {
    return mesh_devices.at(0).second->arch();
}

int DeviceMesh::num_rows() const
{
    return this->device_grid.first;
}

int DeviceMesh::num_cols() const
{
    return this->device_grid.second;
}

DeviceGrid DeviceMesh::shape() const
{
    return this->device_grid;
}

void DeviceMesh::close_devices() {
    tt::tt_metal::detail::CloseDevices(managed_devices);
    mesh_devices.clear();
    managed_devices.clear();
}

std::shared_ptr<const DeviceMeshView> DeviceMesh::get_view() const {
    return this->view;
}

std::shared_ptr<DeviceMeshView> DeviceMesh::get_view() {
    return this->view;
}

bool validate_worker_modes(const std::vector<Device*>& workers) {
    bool worker_modes_match = true;
    auto first_worker_mode = workers.at(0)->get_worker_mode();
    for (auto worker : workers) {
        worker_modes_match &= (worker->get_worker_mode() == first_worker_mode);
    }
    return worker_modes_match;
}

}  // namespace tt::tt_metal
