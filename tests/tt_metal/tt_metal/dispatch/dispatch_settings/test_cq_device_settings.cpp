// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <stdexcept>
#include "gtest/gtest.h"
#include "command_queue_fixture.hpp"
#include "llrt/hal.hpp"
#include "tt_metal/impl/dispatch/util/cq_memory_layout.hpp"
#include "tt_metal/impl/dispatch/util/dispatch_settings.hpp"
#include "tt_metal/impl/dispatch/util/common.hpp"
#include "tt_metal/impl/dispatch/command_queue_interface.hpp"
#include "umd/device/tt_core_coordinates.h"

using namespace tt::tt_metal;

TEST_F(CommandQueueSingleCardFixture, TestDefaultOptionsParity) {
    static constexpr auto core_types_to_test = std::array<CoreType, 2>{CoreType::WORKER, CoreType::ETH};
    static constexpr auto num_hw_cqs_to_test = std::array<uint32_t, 2>{1, 2};

    for (const auto& core_type : core_types_to_test) {
        for (const auto& num_hw_cqs : num_hw_cqs_to_test) {
            auto opts = DispatchSettings::defaults(core_type, tt::Cluster::instance(), hal, num_hw_cqs);

            const auto& old_constants = dispatch_constants::get(core_type, num_hw_cqs);

            ASSERT_EQ(opts.num_hw_cqs_, num_hw_cqs);

            ASSERT_EQ(opts.prefetch_q_entries_, old_constants.prefetch_q_entries());
            ASSERT_EQ(opts.prefetch_q_size_, old_constants.prefetch_q_size());
            ASSERT_EQ(opts.prefetch_max_cmd_size_, old_constants.max_prefetch_command_size());
            ASSERT_EQ(opts.prefetch_cmddat_q_size_, old_constants.cmddat_q_size());
            ASSERT_EQ(opts.prefetch_scratch_db_size_, old_constants.scratch_db_size());

            ASSERT_EQ(opts.prefetch_d_buffer_size_, old_constants.prefetch_d_buffer_size());
            ASSERT_EQ(opts.prefetch_d_pages_, old_constants.prefetch_d_buffer_pages());
            ASSERT_EQ(opts.prefetch_d_blocks_, dispatch_constants::PREFETCH_D_BUFFER_BLOCKS);

            ASSERT_EQ(opts.tunneling_buffer_size_, old_constants.mux_buffer_size(num_hw_cqs));
            ASSERT_EQ(opts.tunneling_buffer_pages_, old_constants.mux_buffer_pages(num_hw_cqs));

            ASSERT_EQ(opts.dispatch_pages_, old_constants.dispatch_buffer_pages());
            ASSERT_EQ(opts.dispatch_pages_per_block_, dispatch_constants::DISPATCH_BUFFER_SIZE_BLOCKS);

            ASSERT_EQ(opts.dispatch_s_buffer_size_, old_constants.dispatch_s_buffer_size());
            ASSERT_EQ(opts.dispatch_s_buffer_pages_, old_constants.dispatch_s_buffer_pages());

            EXPECT_NO_THROW(opts.build());
        }
    }
}

TEST_F(CommandQueueSingleCardFixture, TestDefaultOptionsUnsupportedCoreType) {
    const auto unsupported_core = CoreType::ARC;
    EXPECT_THROW(DispatchSettings::defaults(unsupported_core, tt::Cluster::instance(), hal, 1), std::runtime_error);
}

TEST_F(CommandQueueSingleCardFixture, TestDefaultOptionsMissingArgs) {
    DispatchSettings opts;
    EXPECT_THROW(opts.build(), std::runtime_error);
}

TEST_F(CommandQueueSingleCardFixture, TestCQAddressRange) {
    CQAddrRange first{tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_BUFFER), 0x0, 0x1000};
    CQAddrRange second{tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_BUFFER), 0x1000, 0x1000};
    CQAddrRange third{tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_BUFFER), 0x100, 0x400};

    ASSERT_EQ(first.base, 0x0);
    ASSERT_EQ(second.base, 0x1000);
    ASSERT_EQ(third.base, 0x100);

    ASSERT_EQ(first.size, 0x1000);
    ASSERT_EQ(second.size, 0x1000);
    ASSERT_EQ(third.size, 0x400);

    ASSERT_FALSE(first.overlaps(second));
    ASSERT_FALSE(second.overlaps(first));

    ASSERT_EQ(first.start(), 0x0);
    ASSERT_EQ(first.end(), 0x1000);

    ASSERT_EQ(second.start(), 0x1000);
    ASSERT_EQ(second.end(), 0x2000);

    ASSERT_TRUE(first.overlaps(third));
    ASSERT_TRUE(third.overlaps(first));

    ASSERT_FALSE(third.overlaps(second));
    ASSERT_FALSE(second.overlaps(third));
}

TEST_F(CommandQueueSingleCardFixture, TestCQAddressRangeEq) {
    CQAddrRange first{tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_BUFFER), 0x0, 0x1000};
    CQAddrRange second{tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_BUFFER), 0x0, 0x1000};
    CQAddrRange third{tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_BUFFER), 0x100, 0x400};
    CQAddrRange fourth(tt::utils::underlying_type<CQAddrType>(CQAddrType::CMDDAT_Q), 0x0, 0x1000);
    CQAddrRange fifth(tt::utils::underlying_type<CQAddrType>(CQAddrType::CMDDAT_Q), 0x100, 0x400);

    ASSERT_TRUE(first == second && !(first != second));
    ASSERT_TRUE(second == first && !(second != first));
    ASSERT_FALSE(first == third && !(first != third));
    ASSERT_FALSE(second == third && !(second != third));
    ASSERT_FALSE(third == fourth && !(third != fourth));
    ASSERT_FALSE(third == fifth && !(third != fifth));
}

TEST_F(CommandQueueSingleCardFixture, TestCQMemoryLayout) {
    static constexpr auto core_type = CoreType::WORKER;
    static constexpr auto num_hw_cqs = 1;

    const auto opts = DispatchSettings::defaults(core_type, tt::Cluster::instance(), hal, num_hw_cqs);

    const auto expected_ranges = std::array<CQAddrRange, 5>{
        CQAddrRange{tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_BUFFER), opts.device_l1_base_, 0x1000},
        CQAddrRange{
            tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_S_BUFFER),
            opts.device_l1_base_ + 0x1000,
            0x2000},
        CQAddrRange{
            tt::utils::underlying_type<CQAddrType>(CQAddrType::DISPATCH_MESSAGE),
            opts.device_l1_base_ + 0x3000,
            0x1000},
        CQAddrRange{
            tt::utils::underlying_type<CQAddrType>(CQAddrType::PREFETCH_Q), opts.device_l1_base_ + 0x4000, 0x50000},
        CQAddrRange{
            tt::utils::underlying_type<CQAddrType>(CQAddrType::CMDDAT_Q), opts.device_l1_base_ + 0x54000, 0x10000}};

    struct FirstSection : CQBaseLayout {
        void add_regions(CQMemoryLayout& layout, const DispatchSettings& options) override {
            layout.add_region(CQAddrType::DISPATCH_BUFFER, 0x1000);
            layout.add_region(CQAddrType::DISPATCH_S_BUFFER, 0x2000);
            layout.add_region(CQAddrType::DISPATCH_MESSAGE, 0x1000);
        }
    };

    struct SecondSection : CQBaseLayout {
        void add_regions(CQMemoryLayout& layout, const DispatchSettings& options) override {
            layout.add_region(CQAddrType::PREFETCH_Q, 0x50000);
            layout.add_region(CQAddrType::CMDDAT_Q, 0x10000);
        }
    };

    const auto layout = CQMemoryLayout::create<FirstSection, SecondSection>(opts);

    ASSERT_NO_THROW(layout.validate_layout());

    for (const auto& expected_range : expected_ranges) {
        ASSERT_TRUE(layout.contains(expected_range.id));
        const auto& actual_range = layout.get(expected_range.id);
        ASSERT_TRUE(expected_range == actual_range);
    }
}

TEST_F(CommandQueueSingleCardFixture, TestCQMemoryLayoutOverflowed) {
    static constexpr auto core_type = CoreType::WORKER;
    static constexpr auto num_hw_cqs = 1;

    const auto opts = DispatchSettings::defaults(core_type, tt::Cluster::instance(), hal, num_hw_cqs);

    struct FirstSection : CQBaseLayout {
        void add_regions(CQMemoryLayout& layout, const DispatchSettings& options) override {
            layout.add_region(CQAddrType::DISPATCH_BUFFER, 0x1000);
            layout.add_region(CQAddrType::DISPATCH_MESSAGE, options.device_l1_size_);
        }
    };

    auto make_layout = [&]() { return CQMemoryLayout::create<FirstSection, FirstSection>(opts); };

    EXPECT_THROW(make_layout(), std::runtime_error);
}

TEST_F(CommandQueueSingleCardFixture, TestCQMemoryLayoutMutations) {
    static constexpr auto core_type = CoreType::WORKER;
    static constexpr auto num_hw_cqs = 1;

    const auto opts = DispatchSettings::defaults(core_type, tt::Cluster::instance(), hal, num_hw_cqs);

    struct FirstSection : CQBaseLayout {
        void add_regions(CQMemoryLayout& layout, const DispatchSettings& options) override {
            layout.add_region(CQAddrType::DISPATCH_BUFFER, 0x1000);
            layout.add_region(CQAddrType::DISPATCH_MESSAGE, 0x1000);
        }
    };

    auto make_layout = [&]() { return CQMemoryLayout::create<FirstSection>(opts); };

    EXPECT_NO_THROW(make_layout());
    auto layout = make_layout();

    ASSERT_TRUE(layout.contains(CQAddrType::DISPATCH_BUFFER));
    ASSERT_TRUE(layout.contains(CQAddrType::DISPATCH_MESSAGE));

    EXPECT_THROW(layout.add_region(CQAddrType::DISPATCH_BUFFER, 0x1000), std::runtime_error);
    EXPECT_THROW(layout.add_region(CQAddrType::DISPATCH_MESSAGE, 0x1000), std::runtime_error);
    EXPECT_THROW(layout.add_region(CQAddrType::SCRATCH_DB, 0), std::runtime_error);

    EXPECT_NO_THROW(layout.add_region(CQAddrType::CMDDAT_Q, 0x1000));
    ASSERT_TRUE(layout.contains(CQAddrType::CMDDAT_Q));
    EXPECT_NO_THROW(layout.validate_layout());
}
