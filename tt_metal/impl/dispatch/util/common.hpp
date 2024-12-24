// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::tt_metal {

using prefetch_q_ptr_type = uint32_t;
using prefetch_q_entry_type = uint16_t;

constexpr auto operator""_KB(const unsigned long long v) -> uint32_t { return 1024 * v; }

enum class CQAddrType : uint8_t {
    PREFETCH_Q_RD = 0,
    // Used to notify host of how far device has gotten, doesn't need L1 alignment because it's only written locally by
    // prefetch kernel.
    PREFETCH_Q_PCIE_RD = 1,
    COMPLETION_Q_WR = 2,
    COMPLETION_Q_RD = 3,
    // Max of 2 CQs. COMPLETION_Q*_LAST_EVENT_PTR track the last completed event in the respective CQs
    COMPLETION_Q0_LAST_EVENT = 4,
    COMPLETION_Q1_LAST_EVENT = 5,
    DISPATCH_S_SYNC_SEM = 6,
    DISPATCH_MESSAGE = 7,

    DISPATCH_BUFFER,
    DISPATCH_S_BUFFER,

    PREFETCH_Q,
    CMDDAT_Q,

    SCRATCH_DB,

    TUNNELER,  // mux,demux,router,etc.

    UNRESERVED,  // Unreserved region not used by any dispatch kernels
};

}  // namespace tt::tt_metal
