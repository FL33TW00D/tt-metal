// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include "llrt/hal.hpp"
#include "llrt/tt_cluster.hpp"
#include "umd/device/tt_core_coordinates.h"
#include "common.hpp"
#include "dispatch_settings.hpp"

namespace tt::tt_metal {

DispatchSettings DispatchSettings::worker_defaults(
    const tt::Cluster& cluster, const tt::tt_metal::Hal& hal, const uint32_t num_hw_cqs) {
    uint32_t prefetch_q_entries;
    if (cluster.is_galaxy_cluster()) {
        prefetch_q_entries = 1532 / num_hw_cqs;
    } else {
        prefetch_q_entries = 1534;
    }

    return DispatchSettings()
        .num_hw_cqs(num_hw_cqs)
        .core_type(CoreType::WORKER)
        .prefetch_q_entries(prefetch_q_entries)
        .prefetch_max_cmd_size(128_KB)
        .prefetch_cmddat_q_size(256_KB)
        .prefetch_scratch_db_size(128_KB)
        .prefetch_d_buffer_size(256_KB)

        .dispatch_pages_per_block(4)
        .dispatch_size(512_KB)
        .dispatch_s_buffer_size(32_KB)
        .prefetch_d_blocks(4)

        .with_alignment(hal.get_alignment(HalMemType::L1))

        .tunneling_buffer_size(256_KB / num_hw_cqs)  // same as prefetch_d_buffer_size

        .device_l1(
            hal.get_dev_addr(tt::tt_metal::HalProgrammableCoreType::TENSIX, tt::tt_metal::HalL1MemAddrType::UNRESERVED),
            hal.get_dev_size(tt::tt_metal::HalProgrammableCoreType::TENSIX, tt::tt_metal::HalL1MemAddrType::BASE))

        .build();
}

DispatchSettings DispatchSettings::eth_defaults(
    const tt::Cluster& cluster, const tt::tt_metal::Hal& hal, const uint32_t num_hw_cqs) {
    return DispatchSettings()
        .num_hw_cqs(num_hw_cqs)
        .core_type(CoreType::ETH)
        .prefetch_q_entries(128)
        .prefetch_max_cmd_size(32_KB)
        .prefetch_cmddat_q_size(64_KB)
        .prefetch_scratch_db_size(19_KB)
        .prefetch_d_buffer_size(128_KB)

        .dispatch_pages_per_block(4)
        .dispatch_size(128_KB)
        .dispatch_s_buffer_size(32_KB)
        .prefetch_d_blocks(4)

        .tunneling_buffer_size(128_KB / num_hw_cqs)  // same as prefetch_d_buffer_size

        .with_alignment(hal.get_alignment(HalMemType::L1))

        .device_l1(
            hal.get_dev_addr(
                tt::tt_metal::HalProgrammableCoreType::IDLE_ETH, tt::tt_metal::HalL1MemAddrType::UNRESERVED),
            hal.get_dev_size(tt::tt_metal::HalProgrammableCoreType::IDLE_ETH, tt::tt_metal::HalL1MemAddrType::BASE))

        .build();
}

DispatchSettings DispatchSettings::defaults(
    const CoreType& core_type, const tt::Cluster& cluster, const tt::tt_metal::Hal& hal, const uint32_t num_hw_cqs) {
    if (core_type != CoreType::WORKER && core_type != CoreType::ETH) {
        TT_THROW("Only WORKER and ETH CoreType's are supported");
    }

    if (!num_hw_cqs) {
        TT_THROW("0 CQs is invalid");
    }

    if (core_type == CoreType::WORKER) {
        return worker_defaults(cluster, hal, num_hw_cqs);
    } else {
        return eth_defaults(cluster, hal, num_hw_cqs);
    }
}

std::vector<std::string> DispatchSettings::validate() const {
    std::vector<std::string> msgs;

    if (!device_l1_base_) {
        msgs.push_back(fmt::format("configuration device_l1_base() is a required\n"));
    }

    if (!prefetch_q_rd_ptr_size_ || !prefetch_q_pcie_rd_ptr_size_ || !dispatch_s_sync_sem_ || !dispatch_message_ ||
        !other_ptrs_size) {
        msgs.push_back(fmt::format("configuration with_alignment() is a required\n"));
    }

    if (!num_hw_cqs_) {
        msgs.push_back(fmt::format("num_hw_cqs must be set to non zero\n"));
    } else if (num_hw_cqs_ > CQDeviceConstants::MAX_NUM_HW_CQS) {
        msgs.push_back(fmt::format(
            "{} CQs specified. the maximum number for num_hw_cqs is {}\n",
            num_hw_cqs_,
            CQDeviceConstants::MAX_NUM_HW_CQS));
    }

    if (prefetch_cmddat_q_size_ < 2 * prefetch_max_cmd_size_) {
        msgs.push_back(fmt::format(
            "prefetch_cmddat_q_size_ {} is too small. It must be >= 2 * prefetch_max_cmd_size_\n",
            prefetch_cmddat_q_size_));
    }
    if (prefetch_scratch_db_size_ % 2) {
        msgs.push_back(fmt::format("prefetch_scratch_db_size_ {} must be even\n", prefetch_scratch_db_size_));
    }
    if ((dispatch_size_ & (dispatch_size_ - 1)) != 0) {
        msgs.push_back(fmt::format("dispatch_block_size_ {} must be power of 2\n", dispatch_size_));
    }

    return msgs;
}

DispatchSettings& DispatchSettings::build() {
    const auto msgs = validate();
    if (msgs.empty()) {
        return *this;
    }
    TT_THROW("Validation errors in dispatch_settings. Call validate() for a list of errors");
}

}  // namespace tt::tt_metal
