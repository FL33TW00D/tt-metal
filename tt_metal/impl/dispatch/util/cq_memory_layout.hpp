// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>
#include "dispatch_settings.hpp"
#include "common.hpp"

namespace tt::tt_metal {

// Address Range
struct CQAddrRange {
    const uint32_t id;    // Identifier
    const uint32_t base;  // Base address
    const uint32_t size;  // Size of this address range

    explicit CQAddrRange(uint32_t id, uint32_t base, uint32_t size) : id{id}, base{base}, size{size} {}

    // Start address
    uint32_t start() const { return this->base; }

    // End address
    uint32_t end() const { return this->base + size; }

    // Returns true if this address range overlaps another
    bool overlaps(const CQAddrRange& other) const {
        return std::max(this->start(), other.start()) < std::min(this->end(), other.end());
    }

    bool operator==(const CQAddrRange& rhs) const {
        return this->id == rhs.id && this->base == rhs.base && this->size == rhs.size;
    }

    bool operator!=(const CQAddrRange& rhs) const { return !operator==(rhs); }
};

class CQMemoryLayout;

struct CQBaseLayout {
    virtual void add_regions(CQMemoryLayout& layout, const DispatchSettings& options) = 0;
    virtual ~CQBaseLayout() = default;
};

class CQMemoryLayout {
private:
    uint32_t l1_base;
    uint32_t l1_size;
    std::vector<CQAddrRange> layout;

    explicit CQMemoryLayout() = default;

public:
    // Create a new memory layout with options. Throws an error if the layout created becomes invalid
    template <typename... LayoutStack>
    static CQMemoryLayout create(const DispatchSettings& options) {
        static_assert(
            (std::is_base_of_v<CQBaseLayout, LayoutStack> && ...),
            "All layout types must inherit from CQDeviceBaseLayout");

        CQMemoryLayout layout;
        layout.l1_base = options.device_l1_base_;
        layout.l1_size = options.device_l1_size_;
        (LayoutStack{}.add_regions(layout, options), ...);
        layout.validate_layout();
        return layout;
    }

    CQMemoryLayout(CQMemoryLayout&& other) noexcept :
        l1_base(other.l1_base), l1_size(other.l1_size), layout(std::move(other.layout)) {
        other.l1_size = 0;
        other.l1_base = 0;
    }

    CQMemoryLayout& operator=(CQMemoryLayout&& other) noexcept {
        if (this != &other) {
            this->l1_base = other.l1_base;
            l1_size = other.l1_size;
            layout = std::move(other.layout);
            other.l1_size = 0;
            other.l1_base = 0;
        }
        return *this;
    }

    // Pushes back a region to the layout
    void add_region(const uint32_t id, const uint32_t sz);

    // Pushes back a region to the layout. T is an enum.
    template <typename T>
    void add_region(const T& id, const uint32_t sz) {
        static_assert(std::is_enum_v<T> && "T must be an enum");
        add_region(static_cast<uint32_t>(id), sz);
    }

    // Throw if the layout has any overlapped or zero regions
    void validate_layout() const;

    // Returns true if this layout contains an address
    bool contains(const uint32_t id) const;

    // Returns true if this layout contains an address. T is an enum.
    template <typename T>
    bool contains(const T& id) const {
        static_assert(std::is_enum_v<T> && "T must be an enum");
        return contains(static_cast<uint32_t>(id));
    }

    // Return the address range for an id
    CQAddrRange get(const uint32_t id) const;

    // Return the address range for an id. T is an enum.
    template <typename T>
    CQAddrRange get(const T& id) const {
        static_assert(std::is_enum_v<T> && "T must be an enum");
        return get(static_cast<uint32_t>(id));
    }
};

}  // namespace tt::tt_metal
