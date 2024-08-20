// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ttnn/cpp/ttnn/operations/ccl/reduce_scatter/host/reduce_scatter_common.hpp"
#include "ttnn/cpp/ttnn/operations/ccl/ccl_host_datastructures.hpp"
#include "ttnn/cpp/ttnn/operations/ccl/ccl_common.hpp"

#include <cstdint>
#include <ranges>

namespace tt {
namespace tt_metal {

// Forward declarations
class Device;

} // namespace tt_metal
} // namespace tt

namespace ttnn {
namespace ccl {
// Forward declarations
// class RingTopology;
// class InterleavedTensorWorkerSlice;
// class CCLOpConfig;
// class WorkerTransferInfo;
// class WorkerXY;

namespace reduce_scatter_detail {

struct ReduceScatterWorkerArgBuilder {
    ReduceScatterWorkerArgBuilder (
        tt::tt_metal::Device const* device,
        ttnn::ccl::CCLOpConfig const& op_config,
        ttnn::ccl::RingTopology const& topology_config,
        ttnn::ccl::InterleavedTensorWorkerSlice const& worker_input_slice,
        WorkerTransferInfo const& worker_transfer_info,
        uint32_t worker_idx,
        uint32_t link,
        uint32_t cb_num_pages_per_packet,
        uint32_t worker_sender_semaphore_id,
        uint32_t worker_receiver_semaphore_id);

    std::vector<uint32_t> generate_reduce_op_kernel_ct_args() const;

    std::vector<uint32_t> generate_reduce_op_kernel_rt_args(
        uint32_t link, uint32_t worker_index, uint32_t ring_size) const;

    std::vector<uint32_t> generate_receiver_kernel_ct_args() const;

    std::vector<uint32_t> generate_receiver_kernel_rt_args(
        ttnn::ccl::WorkerXY const& edm_core,
        uint32_t edm_core_semaphore_address,
        uint32_t edm_core_buffer_address,
        uint32_t link,
        uint32_t worker_index,
        bool is_in_clockwise_direction) const;

    std::vector<uint32_t> generate_sender_kernel_ct_args() const;

    std::vector<uint32_t> generate_sender_kernel_rt_args(
        ttnn::ccl::WorkerXY edm_core,
        uint32_t edm_core_semaphore_address,
        uint32_t edm_core_buffer_address,
        uint32_t link,
        uint32_t worker_index,
        bool is_clockwise) const;

    std::vector<uint32_t> generate_line_start_sender_kernel_rt_args(
        ttnn::ccl::WorkerXY edm_core,
        uint32_t edm_core_semaphore_address,
        uint32_t edm_core_buffer_address,
        uint32_t link,
        uint32_t worker_index,
        bool is_clockwise) const;

    std::vector<uint32_t> generate_line_start_sender_kernel_ct_args(
        ttnn::ccl::WorkerXY edm_core,
        uint32_t edm_core_semaphore_address,
        uint32_t edm_core_buffer_address,
        uint32_t link,
        uint32_t worker_index,
        bool is_clockwise) const;

    tt::tt_metal::Device const*device;
    ttnn::ccl::RingTopology const topology_config;
    ttnn::ccl::CCLOpConfig const op_config;
    ttnn::ccl::InterleavedTensorWorkerSlice const worker_input_slice;
    WorkerTransferInfo const worker_transfer_info;
    uint32_t cb_num_pages_per_packet;
    uint32_t worker_sender_semaphore_id;
    uint32_t worker_receiver_semaphore_id;

    uint32_t total_num_math_pages;
    bool src_is_dram;
    bool dst_is_dram;
};

} // namespace reduce_scatter_detail
} // namespace ccl
} // namespace ttnn
