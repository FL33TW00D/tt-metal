// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "cq_memory_layout.hpp"
#include "common/assert.hpp"
#include "magic_enum/magic_enum.hpp"

namespace tt::tt_metal {

void CQMemoryLayout::add_region(const uint32_t id, const uint32_t sz) {
    auto last_end_addr = this->l1_base;

    if (!this->layout.empty()) {
        last_end_addr = this->layout.back().end();
    }

    if (!sz) {
        TT_THROW("Cannot add an zero sized region");
    }

    if (this->contains(id)) {
        TT_THROW("Region {} already exists in the layout", id);
    }

    this->layout.emplace_back(id, last_end_addr, sz);
}

void CQMemoryLayout::validate_layout() const {
    if (this->layout.empty()) {
        TT_THROW("layout is empty");
    }

    if (this->layout.back().end() >= this->l1_size) {
        TT_THROW(
            "Address for region {} {:#x} has overflowed L1 size {}B",
            this->layout.back().id,
            this->layout.back().end(),
            this->l1_size);
    }

    if (this->layout[0].size == 0) {
        TT_THROW("layout[0].size is 0 (region {})", this->layout[0].id);
    }

    // This situation should not happen if the the built in add_region interface
    // is always used
    for (int i = 1; i < this->layout.size(); ++i) {
        if (this->layout[i].size == 0) {
            TT_THROW("layout[{}].size is 0 (region {})", i, this->layout[i].id);
        } else if (this->layout[i].overlaps(this->layout[i - 1])) {
            TT_THROW(
                "layout[{}] overlaps with layout[{}] (region {} and {})",
                i,
                i - 1,
                this->layout[i].id,
                this->layout[i - 1].id);
        }
    }
}

bool CQMemoryLayout::contains(const uint32_t id) const {
    for (const auto range : this->layout) {
        if (range.id == id) {
            return true;
        }
    }
    return false;
}

CQAddrRange CQMemoryLayout::get(const uint32_t id) const {
    for (const auto range : this->layout) {
        if (range.id == id) {
            return range;
        }
    }
    TT_THROW("Region {} was not found in the layout", id);
}

}  // namespace tt::tt_metal
