// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_metal/common/assert.hpp"
#include "tt_metal/host_api.hpp"
#include "tt_metal/impl/allocator/allocator.hpp"
#include "tt_metal/impl/kernels/data_types.hpp"
#include "tt_metal/impl/device/device.hpp"
#include "tt_metal/impl/sub_device/sub_device.hpp"
#include "tt_metal/impl/sub_device/sub_device_manager.hpp"
#include "tt_metal/tt_stl/span.hpp"

namespace tt::tt_metal {

namespace detail {

SubDeviceManager::SubDeviceManager(tt::stl::Span<const SubDevice> sub_devices, DeviceAddr local_l1_size, Device *device) : sub_devices_(sub_devices.begin(), sub_devices.end()), local_l1_size_(align(local_l1_size, hal.get_alignment(HalMemType::L1))), device_(device) {
    this->validate_sub_devices();
    this->populate_num_cores();
    this->populate_sub_allocators();
    this->populate_noc_data();
}

SubDeviceManager::SubDeviceManager(std::vector<SubDevice>&& sub_devices, DeviceAddr local_l1_size, Device *device) : sub_devices_(std::move(sub_devices)), local_l1_size_(align(local_l1_size, hal.get_alignment(HalMemType::L1))), device_(device) {
    this->validate_sub_devices();
    this->populate_num_cores();
    this->populate_sub_allocators();
    this->populate_noc_data();
}

SubDeviceManager::~SubDeviceManager() {
    for (auto& allocator : this->sub_device_allocators_) {
        if (allocator) {
            allocator::clear(*allocator);
            for (const auto &buf : allocator::get_allocated_buffers(*allocator)) {
                DeallocateBuffer(*buf);
            }
        }
    }
}

uint32_t SubDeviceManager::num_sub_devices() const { return this->sub_devices_.size(); }

const SubDevice& SubDeviceManager::sub_device(uint32_t sub_device_index) const {
    TT_FATAL(sub_device_index < this->sub_devices_.size(), "SubDevice index {} out of bounds {}", sub_device_index, this->sub_devices_.size());
    return sub_devices_[sub_device_index];
}

const vector_memcpy_aligned<uint32_t> &SubDeviceManager::noc_mcast_data(uint32_t sub_device_id) const {
    TT_FATAL(sub_device_id < this->num_sub_devices(), "SubDevice index {} out of bounds {}", sub_device_id, this->num_sub_devices());
    return noc_mcast_data_[sub_device_id];
}

const vector_memcpy_aligned<uint32_t> &SubDeviceManager::noc_unicast_data(uint32_t sub_device_id) const {
    TT_FATAL(sub_device_id < this->num_sub_devices(), "SubDevice index {} out of bounds {}", sub_device_id, this->num_sub_devices());
    return noc_unicast_data_[sub_device_id];
}

const vector_memcpy_aligned<uint32_t> &SubDeviceManager::noc_mcast_unicast_data(uint32_t sub_device_id) const {
    TT_FATAL(sub_device_id < this->num_sub_devices(), "SubDevice index {} out of bounds {}", sub_device_id, this->num_sub_devices());
    return noc_mcast_unicast_data_[sub_device_id];
}

std::unique_ptr<Allocator> &SubDeviceManager::sub_device_allocator(uint32_t sub_device_id) {
    TT_FATAL(sub_device_id < this->num_sub_devices(), "SubDevice index {} out of bounds {}", sub_device_id, this->num_sub_devices());
    return sub_device_allocators_[sub_device_id];
}

const std::unordered_set<uint32_t> &SubDeviceManager::trace_ids() const {
    return this->trace_ids_;
}

void SubDeviceManager::add_trace_id(uint32_t trace_id) {
    this->trace_ids_.insert(trace_id);
}

void SubDeviceManager::remove_trace_id(uint32_t trace_id) {
    this->trace_ids_.erase(trace_id);
}

bool SubDeviceManager::has_allocations() const {
    for (const auto& allocator : this->sub_device_allocators_) {
        if (allocator && allocator->allocated_buffers.size() > 0) {
            return true;
        }
    }
    return false;
}

DeviceAddr SubDeviceManager::local_l1_size() const { return this->local_l1_size_; }

void SubDeviceManager::validate_sub_devices() const {
    // Validate sub device cores fit inside the device grid
    const auto& compute_grid_size = this->device_->compute_with_storage_grid_size();
    CoreRange device_worker_cores = CoreRange({0, 0}, {compute_grid_size.x - 1, compute_grid_size.y - 1});
    const auto& device_eth_cores = this->device_->get_active_ethernet_cores(true);
    for (const auto& sub_device : this->sub_devices_) {
        const auto& worker_cores = sub_device.cores(HalProgrammableCoreType::TENSIX);
        TT_FATAL(device_worker_cores.contains(worker_cores), "Tensix cores {} specified in sub device must be within device grid {}", worker_cores, device_worker_cores);
        const auto& eth_cores = sub_device.cores(HalProgrammableCoreType::ACTIVE_ETH);
        uint32_t num_eth_cores = 0;
        for (const auto& dev_eth_core : device_eth_cores) {
            if (eth_cores.contains(dev_eth_core)) {
                num_eth_cores++;
            }
        }
        TT_FATAL(num_eth_cores == eth_cores.num_cores(), "Ethernet cores {} specified in sub device must be within device grid", eth_cores);
    }
    if (this->sub_devices_.size() < 2) {
        return;
    }
    // Validate no overlap of sub devices
    for (uint32_t i = 0; i < this->sub_devices_.size(); ++i) {
        for (uint32_t j = i + 1; j < this->sub_devices_.size(); ++j) {
            for (uint32_t k = 0; k < NumHalProgrammableCoreTypes; ++k) {
                TT_FATAL(!(this->sub_devices_[i].cores()[k].intersects(this->sub_devices_[j].cores()[k])), "SubDevices specified for SubDeviceManager intersect");
            }
        }
    }
}

void SubDeviceManager::populate_num_cores() {
    for (const auto& sub_device : this->sub_devices_) {
        for (uint32_t i = 0; i < NumHalProgrammableCoreTypes; ++i) {
            num_cores_[i] += sub_device.num_cores(static_cast<HalProgrammableCoreType>(i));
        }
    }
}

void SubDeviceManager::populate_sub_allocators() {
    this->sub_device_allocators_.resize(this->num_sub_devices());
    if (this->local_l1_size_ == 0) {
        return;
    }
    const auto& global_allocator_config = this->device_->get_initialized_allocator()->config;
    // Construct allocator config from soc_desc
    // Take max alignment to satisfy NoC rd/wr constraints
    // Tensix/Eth -> PCIe/DRAM src and dst addrs must be L1_ALIGNMENT aligned
    // PCIe/DRAM -> Tensix/Eth src and dst addrs must be DRAM_ALIGNMENT aligned
    // Tensix/Eth <-> Tensix/Eth src and dst addrs must be L1_ALIGNMENT aligned
    for (uint32_t i = 0; i < this->num_sub_devices(); ++i) {
        const auto& compute_cores = this->sub_devices_[i].cores(HalProgrammableCoreType::TENSIX);
        if (compute_cores.empty()) {
            continue;
        }
        AllocatorConfig config(
            {.num_dram_channels = 0,
            .dram_bank_size = 0,
            .dram_bank_offsets = {},
            .dram_unreserved_base = 0,
            .l1_unreserved_base = global_allocator_config.l1_unreserved_base,
            .worker_grid = compute_cores,
            .worker_l1_size = global_allocator_config.l1_unreserved_base + this->local_l1_size_,
            .storage_core_bank_size = std::nullopt,
            .l1_small_size = 0,
            .trace_region_size = 0,
            .core_type_from_noc_coord_table = {},  // Populated later
            .worker_log_to_physical_routing_x = global_allocator_config.worker_log_to_physical_routing_x,
            .worker_log_to_physical_routing_y = global_allocator_config.worker_log_to_physical_routing_y,
            .l1_bank_remap = {},
            .compute_grid = compute_cores,
            .alignment = global_allocator_config.alignment,
            .disable_interleaved = true});
        TT_FATAL(config.l1_small_size < (config.storage_core_bank_size.has_value() ? config.storage_core_bank_size.value() : config.worker_l1_size - config.l1_unreserved_base),
                "Reserved size must be less than bank size");
        TT_FATAL(
            config.l1_small_size % config.alignment == 0,
            "Reserved size must be aligned to allocator alignment {}",
            config.alignment);

        // sub_devices only have compute cores for allocation
        for (const CoreCoord& core : corerange_to_cores(compute_cores)) {
            const auto noc_coord = this->device_->worker_core_from_logical_core(core);
            config.core_type_from_noc_coord_table.insert({noc_coord, AllocCoreType::ComputeAndStore});
        }

        // L1_BANKING scheme creates 1 bank per DRAM core and splits up L1 such that there are power 2 num L1 banks
        // This is the only allocator scheme supported because kernel APIs assume num L1 banks are power of 2
        TT_ASSERT(this->device_->allocator_scheme_ == MemoryAllocator::L1_BANKING);
        this->sub_device_allocators_[i] = std::make_unique<L1BankingAllocator>(config);
    }
}

void SubDeviceManager::populate_noc_data() {
    uint32_t num_sub_devices = this->num_sub_devices();
    this->noc_mcast_data_.resize(num_sub_devices);
    this->noc_unicast_data_.resize(num_sub_devices);
    this->noc_mcast_unicast_data_.resize(num_sub_devices);

    NOC noc_index = this->device_->dispatch_go_signal_noc();

    for (uint32_t i = 0; i < num_sub_devices; ++i) {
        const auto& tensix_cores = this->sub_devices_[i].cores(HalProgrammableCoreType::TENSIX);
        const auto& eth_cores = this->sub_devices_[i].cores(HalProgrammableCoreType::ACTIVE_ETH);

        auto& noc_mcast_unicast_data = this->noc_mcast_unicast_data_[i];
        noc_mcast_unicast_data.resize(tensix_cores.size() * 2 + eth_cores.size());
        auto& noc_mcast_data = this->noc_mcast_data_[i];
        noc_mcast_data.resize(tensix_cores.size() * 2);
        auto& noc_unicast_data = this->noc_unicast_data_[i];
        noc_unicast_data.resize(eth_cores.size());
        uint32_t idx = 0;
        for (const auto& core_range : tensix_cores.ranges()) {
            auto physical_start = this->device_->physical_core_from_logical_core(core_range.start_coord, CoreType::WORKER);
            auto physical_end = this->device_->physical_core_from_logical_core(core_range.end_coord, CoreType::WORKER);
            auto physical_core_range = CoreRange(physical_start, physical_end);
            noc_mcast_data[idx++] = this->device_->get_noc_multicast_encoding(noc_index, physical_core_range);
            noc_mcast_data[idx++] = core_range.size();
        }
        std::copy(noc_mcast_data.begin(), noc_mcast_data.end(), noc_mcast_unicast_data.begin());

        idx = 0;
        for (const auto& core_range : eth_cores.ranges()) {
            for (const auto& core : core_range) {
                auto physical_core = this->device_->physical_core_from_logical_core(core, CoreType::ETH);
                noc_unicast_data[idx++] = this->device_->get_noc_unicast_encoding(noc_index, physical_core);
            }
        }
        std::copy(noc_unicast_data.begin(), noc_unicast_data.end(), noc_mcast_unicast_data.begin() + noc_mcast_data.size());
    }
}

}  // namespace detail

}  // namespace tt::tt_metal
