// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
///

#include "common/core_coord.h"
#include "eth_l1_address_map.h"
#include "impl/buffers/buffer.hpp"
#include "impl/kernels/data_types.hpp"
#include "ttnn/operations/ccl/common/types/ccl_types.hpp"
#include "ttnn/tensor/tensor_impl.hpp"
#include "ttnn/operations/ccl/shared_with_host/hetergeneous_data_structs.hpp"
#include "ttnn/operations/ccl/ccl_host_datastructures.hpp"
#include "ttnn/operations/ccl/ccl_common.hpp"
#include "tt_metal/common/constants.hpp"
#include "tt_metal/host_api.hpp"
#include "tt_metal/impl/buffers/circular_buffer_types.hpp"

#include "ttnn/operations/eltwise/binary/common/binary_op_types.hpp"
#include "ttnn/operations/eltwise/binary/common/binary_op_utils.hpp"

#include "ttnn/cpp/ttnn/operations/ccl/reduce_scatter/host/reduce_scatter_worker_builder.hpp"
#include "ttnn/cpp/ttnn/operations/ccl/reduce_scatter/host/reduce_scatter_common.hpp"

// Includes that need to be moved to CCL datastructures header
#include <cstdint>
#include <limits>
#include <vector>
#include <algorithm>
#include <ranges>

using namespace tt::constants;

// Notes on abbreviations:
// cw = clockwise
// ccw = counter-clockwise
// edm = erisc data mover

// How this reduce_scatter op works:
// For each chip, we have a element range of the input tensor shape that will eventually scatter
// out to it. For all other chunks outside that range, the chip will forward the chunk to the next chip.
// While forwarding the data, the chip will also reduce it with the local input tensor chunk corresponding
// with that received chunk. It will forward the partially reduced chunk.
// Reduces along rank

namespace ttnn {

namespace ccl {
namespace reduce_scatter_detail {

static std::size_t decide_number_of_edm_channels(
   ttnn::ccl::CCLOpConfig const& ccl_op_config, std::size_t max_num_workers, bool enable_bidirectional) {
    bool is_linear_topology = ccl_op_config.get_topology() == ttnn::ccl::Topology::Linear;
    TT_ASSERT(!is_linear_topology || max_num_workers > 1);
    if (is_linear_topology) {
        // Workers must be evenly divided for line reduce scatter
        max_num_workers = tt::round_down(max_num_workers, 2);
    }
    return std::min<std::size_t>(max_num_workers, enable_bidirectional || is_linear_topology ? 8 : 4);
}


struct EdmInterfaceAddresses {
    std::unordered_map<int, uint32_t> worker_sender_edm_semaphore_addresses;
    std::unordered_map<int, uint32_t> worker_sender_edm_buffer_addresses;
    std::unordered_map<int, uint32_t> worker_receiver_edm_semaphore_addresses;
    std::unordered_map<int, uint32_t> worker_receiver_edm_buffer_addresses;
};

// Future work: split this up further:
// 1) assign workers to EDM channel (with buffer sharing mode specified too)
// 2) Compute the semaphore and buffer addresses (for each EDM channel and worker)
// For now - the mapping between workers and EDM channels is 1:1
static void add_worker_config_to_edm_builders(
    Device* device,
    RingReduceScatterWrappedTensorSlicer& tensor_slicer,  // TODO: Update to Generic ReduceScatterSlicer when it is implemented
    ccl::CCLOpConfig const& op_config,
    std::vector<CoreCoord> const& worker_cores,
    uint32_t num_channels_per_edm,

    std::vector<ttnn::ccl::EriscDatamoverBuilder>& clockwise_edm_builders,
    std::vector<ttnn::ccl::EriscDatamoverBuilder>& counter_clockwise_edm_builders,

    uint32_t worker_sender_semaphore_id,
    uint32_t worker_receiver_semaphore_id,
    uint32_t link,
    uint32_t ring_size,
    uint32_t ring_index,
    std::function<bool(uint32_t)> is_buffer_in_clockwise_direction_fn,

    EdmInterfaceAddresses& edm_interface_addresses) {
    bool is_linear = op_config.get_topology() == ttnn::ccl::Topology::Linear;
    for (uint32_t c = 0; c < num_channels_per_edm; ++c) {
        log_trace(tt::LogOp, "add_worker_config_to_edm_builders for channel {}", c);
        uint32_t num_workers_per_eth_buffer = 1;

        std::vector<ttnn::ccl::WorkerXY> sender_worker_coords;
        std::vector<ttnn::ccl::WorkerXY> receiver_worker_coords;
        for (uint32_t w = c * num_workers_per_eth_buffer; w < (c + 1) * num_workers_per_eth_buffer; ++w) {
            log_trace(tt::LogOp, "Getting noc coords for worker {}", w);
            sender_worker_coords.push_back(ttnn::ccl::WorkerXY(
                device->worker_core_from_logical_core(worker_cores.at(w)).x,
                device->worker_core_from_logical_core(worker_cores.at(w)).y));
            receiver_worker_coords.push_back(ttnn::ccl::WorkerXY(
                device->worker_core_from_logical_core(worker_cores.at(w)).x,
                device->worker_core_from_logical_core(worker_cores.at(w)).y));
        }

        // Get the maximum message size we'd like to use. Not the actual packet size
        // If linear, then we want to reuse the slicer in both directions
        uint32_t global_worker_idx = c + num_channels_per_edm * link;
        log_trace(tt::LogOp, "get_worker_slice_size_bytes");
        std::size_t worker_tensor_slice_index = !is_linear ? global_worker_idx : (c / 2) + (num_channels_per_edm / 2) * link;
        uint32_t expected_message_size_bytes = tensor_slicer.get_worker_slice_size_bytes(worker_tensor_slice_index);

        bool is_in_clockwise_direction = is_buffer_in_clockwise_direction_fn(c);
        bool is_first_device_in_line = is_linear && ((is_in_clockwise_direction && ring_index == 0) ||
                                                     (!is_in_clockwise_direction && ring_index == ring_size - 1));
        bool is_last_device_in_line = is_linear && ((!is_in_clockwise_direction && ring_index == 0) ||
                                                    (is_in_clockwise_direction && ring_index == ring_size - 1));

        bool sender_enabled = (!is_linear || !is_last_device_in_line); // update for linear
        if (sender_enabled) {
            log_trace(tt::LogOp, "Adding sender EDM channel to {} edm builder", is_buffer_in_clockwise_direction_fn(c) ? "clockwise" : "counter-clockwise");
            auto& sender_edm_builder = is_in_clockwise_direction ? clockwise_edm_builders.at(link)
                                                                              : counter_clockwise_edm_builders.at(link);
            ttnn::ccl::EriscDatamoverBuilder::ChannelBufferInterface const& sender_channel_buffer_info =
                sender_edm_builder.add_sender_channel(
                    worker_sender_semaphore_id,
                    1,  // cw_edm_channel_num_messages_to_send_per_transfer.at(c) * (ring_size - 1),
                    sender_worker_coords,
                    expected_message_size_bytes);
            edm_interface_addresses.worker_sender_edm_semaphore_addresses.insert(
                {global_worker_idx, sender_channel_buffer_info.eth_semaphore_l1_address});
            edm_interface_addresses.worker_sender_edm_buffer_addresses.insert(
                {global_worker_idx, sender_channel_buffer_info.eth_buffer_l1_address});
            log_trace(tt::LogOp, "\tAdded");
        }

        bool receiver_enabled = (!is_linear || !is_first_device_in_line);
        if (receiver_enabled) {
            log_trace(tt::LogOp, "Adding receiver EDM channel to {} edm builder", is_buffer_in_clockwise_direction_fn(c) ? "counter-clockwise" : "clockwise");
            auto& receiver_edm_builder =
                is_in_clockwise_direction ? counter_clockwise_edm_builders.at(link) : clockwise_edm_builders.at(link);
            ttnn::ccl::EriscDatamoverBuilder::ChannelBufferInterface const& receiver_channel_buffer_info =
                receiver_edm_builder.add_receiver_channel(
                    worker_receiver_semaphore_id,
                    // Since we are in worker signal EDM termination mode, we don't need to set the actual number of
                    // messages the EDM must forward as it will receive its finish signal from the worker instead
                    1,
                    receiver_worker_coords,
                    expected_message_size_bytes);
            edm_interface_addresses.worker_receiver_edm_semaphore_addresses.insert(
                {global_worker_idx, receiver_channel_buffer_info.eth_semaphore_l1_address});
            edm_interface_addresses.worker_receiver_edm_buffer_addresses.insert(
                {global_worker_idx, receiver_channel_buffer_info.eth_buffer_l1_address});
            log_trace(tt::LogOp, "\tAdded");
        }
    }
    log_trace(tt::LogOp, "Added EDM channels for link {}", link);
}

static std::tuple<KernelHandle, KernelHandle> build_reduce_scatter_worker(
    tt::tt_metal::Program& program,
    Device const* device,
    ttnn::ccl::RingTopology const& topology_config,
    ttnn::ccl::CCLOpConfig const& op_config,
    ReduceScatterWorkerArgBuilder const& worker_arg_builder,
    EdmInterfaceAddresses const& edm_interface_addresses,
    CoreCoord const& worker_core,
    uint32_t num_edm_channels,
    uint32_t link,
    uint32_t worker_index,
    ttnn::operations::binary::BinaryOpType binary_math_op,
    std::size_t scatter_split_dim,
    std::size_t edm_num_buffers_per_channel,
    std::unordered_map<std::size_t, CoreCoord> const& worker_association_map,
    std::function<bool(uint32_t)> is_buffer_in_clockwise_direction_fn) {

    auto const& worker_defines = op_config.emit_worker_defines();
    TT_ASSERT(worker_defines.size() > 0);
    for (auto const& [key, value] : worker_defines) {
        log_trace(tt::LogOp, "Worker Define: {} = {}", key, value);
    }
    static std::string const& receiver_kernel_path = //topology_config.is_linear ?
        // "ttnn/cpp/ttnn/operations/ccl/reduce_scatter/device/kernels/worker_line_reduce_scatter_reader.cpp" :
        "ttnn/cpp/ttnn/operations/ccl/reduce_scatter/device/kernels/worker_interleaved_ring_reduce_scatter_reader.cpp";

    // This will be configurable by sharded/non-sharded but present the same arg builder
    KernelHandle worker_receiver_kernel_id, worker_sender_kernel_id;
    bool is_in_clockwise_direction = is_buffer_in_clockwise_direction_fn(worker_index);
    uint32_t global_worker_index = link * num_edm_channels + worker_index;
    // std::size_t edm_interface_index =
    log_trace (tt::LogOp, "Worker index: {}, num_edm_channels: {}, global worker index: {}", worker_index, num_edm_channels, global_worker_index);

    bool is_first_device_in_line =
        topology_config.is_linear &&
        ((is_in_clockwise_direction && topology_config.ring_index == 0) || (!is_in_clockwise_direction && topology_config.ring_index == topology_config.ring_size - 1));
    bool is_last_device_in_line =
        topology_config.is_linear &&
        ((is_in_clockwise_direction && topology_config.ring_index == topology_config.ring_size - 1) || (!is_in_clockwise_direction && topology_config.ring_index == topology_config.ring_size - 1));


    log_trace(tt::LogOp, "hh");
    if (!topology_config.is_first_device_in_line(is_in_clockwise_direction)) {
        CoreCoord const& receiver_edm = is_in_clockwise_direction ? topology_config.eth_receiver_cores.at(link)
                                                                  : topology_config.eth_sender_cores.at(link);
        log_trace(tt::LogOp, "looking up EDM noc coords. Before translation x={}, y={}", receiver_edm.x, receiver_edm.y);
        ttnn::ccl::WorkerXY receiver_edm_noc_coord = ttnn::ccl::WorkerXY(
            device->ethernet_core_from_logical_core(receiver_edm).x,
            device->ethernet_core_from_logical_core(receiver_edm).y);
        log_trace(tt::LogOp, "hh__432");

        for (auto const& [key, value] : edm_interface_addresses.worker_receiver_edm_semaphore_addresses) {
            log_trace(tt::LogOp, "Worker {} receiver semaphore address: {}", key, value);
        }
        for (auto const& [key, value] : edm_interface_addresses.worker_sender_edm_semaphore_addresses) {
            log_trace(tt::LogOp, "Worker {} sender semaphore address: {}", key, value);
        }
        for (auto const& [key, value] : edm_interface_addresses.worker_receiver_edm_buffer_addresses) {
            log_trace(tt::LogOp, "Worker {} receiver buffer address: {}", key, value);
        }
        for (auto const& [key, value] : edm_interface_addresses.worker_sender_edm_buffer_addresses) {
            log_trace(tt::LogOp, "Worker {} sender buffer address: {}", key, value);
        }
        // TT_ASSERT(
        //     (is_in_clockwise_direction && edm_interface_addresses.worker_receiver_edm_semaphore_addresses.contains(global_worker_index)) ||
        //     (!is_in_clockwise_direction && edm_interface_addresses.worker_sender_edm_semaphore_addresses.contains(global_worker_index))
        // );
        // TT_ASSERT(
        //     (is_in_clockwise_direction && edm_interface_addresses.worker_receiver_edm_buffer_addresses.contains(global_worker_index)) ||
        //     (!is_in_clockwise_direction && edm_interface_addresses.worker_sender_edm_buffer_addresses.contains(global_worker_index))
        // );
        TT_ASSERT(edm_interface_addresses.worker_receiver_edm_semaphore_addresses.contains(global_worker_index));
        TT_ASSERT(edm_interface_addresses.worker_receiver_edm_buffer_addresses.contains(global_worker_index));
        const uint32_t edm_core_semaphore_address = edm_interface_addresses.worker_receiver_edm_semaphore_addresses.at(global_worker_index);
            // is_in_clockwise_direction ?
                // edm_interface_addresses.worker_receiver_edm_semaphore_addresses.at(global_worker_index)
                // : edm_interface_addresses.worker_sender_edm_semaphore_addresses.at(global_worker_index);
        const uint32_t edm_core_buffer_address = edm_interface_addresses.worker_receiver_edm_buffer_addresses.at(global_worker_index);
            // is_in_clockwise_direction ?
                // edm_interface_addresses.worker_receiver_edm_buffer_addresses.at(global_worker_index);
                // : edm_interface_addresses.worker_sender_edm_buffer_addresses.at(global_worker_index);
        log_trace(tt::LogOp, "hh__");

        worker_receiver_kernel_id = tt::tt_metal::CreateKernel(
            program,
            receiver_kernel_path,
            worker_core,
            tt::tt_metal::ReaderDataMovementConfig(worker_arg_builder.generate_receiver_kernel_ct_args(), worker_defines));
        log_trace(tt::LogOp, "hh__2");

        tt::tt_metal::SetRuntimeArgs(
            program,
            worker_receiver_kernel_id,
            worker_core,
            worker_arg_builder.generate_receiver_kernel_rt_args(
                receiver_edm_noc_coord,
                edm_core_semaphore_address,
                edm_core_buffer_address,
                link,
                worker_index,
                is_in_clockwise_direction));
        log_trace(tt::LogOp, "hh__3");
    }
    log_trace(tt::LogOp, "hh2");

    if (!topology_config.is_first_device_in_line(is_in_clockwise_direction)) {
        vector<uint32_t> compute_kernel_args = {};
        constexpr bool fp32_dest_acc_en = false;
        constexpr bool math_approx_mode = false;
        std::map<string, string> eltwise_defines = ttnn::operations::binary::utils::get_defines(binary_math_op);
        KernelHandle worker_reduce_kernel_id = tt::tt_metal::CreateKernel(
            program,
            "ttnn/cpp/ttnn/operations/eltwise/binary/device/kernels/compute/eltwise_binary_kernel.cpp",
            worker_core,
            tt::tt_metal::ComputeConfig{
                .math_fidelity = MathFidelity::HiFi4,
                .fp32_dest_acc_en = fp32_dest_acc_en,
                .math_approx_mode = math_approx_mode,
                .compile_args = compute_kernel_args,
                .defines = eltwise_defines});

        tt::tt_metal::SetRuntimeArgs(
            program,
            worker_reduce_kernel_id,
            worker_core,
            worker_arg_builder.generate_reduce_op_kernel_rt_args());
    }
    log_trace(tt::LogOp, "hh3");

   if (!topology_config.is_last_device_in_line(is_in_clockwise_direction)) {
        // static std::string const& sender_kernel_path =
        //     !topology_config.is_linear ? "ttnn/cpp/ttnn/operations/ccl/reduce_scatter/device/kernels/worker_line_reduce_scatter_sender.cpp" :
        static std::string const& sender_kernel_path =
                topology_config.is_first_device_in_line(is_in_clockwise_direction)  ? "ttnn/cpp/ttnn/operations/ccl/common/kernels/ccl_send.cpp"
                                       : "ttnn/cpp/ttnn/operations/ccl/reduce_scatter/device/kernels/worker_interleaved_ring_reduce_scatter_sender.cpp";

        log_trace(tt::LogOp, "hh4");
        TT_ASSERT((is_in_clockwise_direction && topology_config.eth_sender_cores.size() > link) || (!is_in_clockwise_direction && topology_config.eth_receiver_cores.size() > link));
        CoreCoord sender_edm = is_in_clockwise_direction ? topology_config.eth_sender_cores.at(link)
                                                         : topology_config.eth_receiver_cores.at(link);
        log_trace(tt::LogOp, "hh5");
        ttnn::ccl::WorkerXY const sender_edm_noc_coord = ttnn::ccl::WorkerXY(
            device->ethernet_core_from_logical_core(sender_edm).x,
            device->ethernet_core_from_logical_core(sender_edm).y);
        TT_ASSERT(sender_edm_noc_coord.y == 0 || sender_edm_noc_coord.y == 6);
        log_trace(tt::LogOp, "hh6");
        TT_ASSERT(edm_interface_addresses.worker_sender_edm_semaphore_addresses.contains(global_worker_index));
        TT_ASSERT(edm_interface_addresses.worker_sender_edm_buffer_addresses.contains(global_worker_index));
        const uint32_t edm_core_semaphore_address = edm_interface_addresses.worker_sender_edm_semaphore_addresses.at(global_worker_index);
        const uint32_t edm_core_buffer_address = edm_interface_addresses.worker_sender_edm_buffer_addresses.at(global_worker_index);
        // const uint32_t edm_core_semaphore_address =
        //     is_in_clockwise_direction
        //         ? edm_interface_addresses.worker_sender_edm_semaphore_addresses.at(global_worker_index)
        //         : edm_interface_addresses.worker_receiver_edm_semaphore_addresses.at(global_worker_index);
        // const uint32_t edm_core_buffer_address =
        //     is_in_clockwise_direction
        //         ? edm_interface_addresses.worker_sender_edm_buffer_addresses.at(global_worker_index)
        //         : edm_interface_addresses.worker_receiver_edm_buffer_addresses.at(global_worker_index);


        WorkerEdmInterfaceArgs edm_interface = {
            sender_edm_noc_coord.x,
            sender_edm_noc_coord.y,
            edm_core_buffer_address,
            edm_core_semaphore_address,
            edm_num_buffers_per_channel};

        log_trace(tt::LogOp, "hh7");
        auto const ct_args = is_first_device_in_line ? worker_arg_builder.generate_line_start_sender_kernel_ct_args()
                                                     : worker_arg_builder.generate_sender_kernel_ct_args();
        log_trace(tt::LogOp, "hh8");
        auto const rt_args = is_first_device_in_line
                                 ? worker_arg_builder.generate_line_start_sender_kernel_rt_args(
                                       edm_interface, scatter_split_dim, link, worker_index)
                                 : worker_arg_builder.generate_sender_kernel_rt_args(
                                       edm_interface, link, worker_index, worker_association_map, is_in_clockwise_direction);
        log_trace(tt::LogOp, "hh9");
        worker_sender_kernel_id = tt::tt_metal::CreateKernel(
            program,
            sender_kernel_path,
            worker_core,
            tt::tt_metal::WriterDataMovementConfig(ct_args, worker_defines));

        tt::tt_metal::SetRuntimeArgs(
            program,
            worker_sender_kernel_id,
            worker_core,
            rt_args);
    }

    return {worker_receiver_kernel_id, worker_sender_kernel_id};
}

static CoreRangeSet select_worker_cores(
    ttnn::ccl::CCLOpConfig const& op_config, std::size_t num_links, std::size_t num_edm_channels) {
    switch (op_config.get_topology()) {
        case ttnn::ccl::Topology::Linear:
            return CoreRangeSet({CoreRange(CoreCoord(0, 0), CoreCoord(num_edm_channels - 1, num_links - 1))});
        case ttnn::ccl::Topology::Ring:
            return CoreRangeSet({CoreRange(CoreCoord(0, 0), CoreCoord(num_edm_channels - 1, num_links - 1))});
        default: TT_ASSERT(false, "Unsupported topology"); return CoreRangeSet({});
    };
}

static WorkerTransferInfo compute_num_edm_messages_per_channel(
    ccl::CCLOpConfig const& op_config,
    RingReduceScatterWrappedTensorSlicer& tensor_slicer,  // TODO: Update to Generic ReduceScatterSlicer when it is implemented
    std::vector<ttnn::ccl::EriscDatamoverBuilder> const& cw_per_link_edm_builders,
    std::vector<ttnn::ccl::EriscDatamoverBuilder> const& ccw_per_link_edm_builders,
    std::size_t const num_edm_channels,
    std::size_t const num_links,
    std::size_t const ring_size) {
    uint32_t const page_size_in_bytes = op_config.get_page_size();
    TT_ASSERT(num_edm_channels > 0);
    TT_ASSERT(num_links > 0);
    TT_ASSERT(page_size_in_bytes > 0);
    log_trace(tt::LogOp, "WorkerTransferInfo");
    std::size_t total_num_workers = num_edm_channels * num_links;

    auto get_iter_begin = [num_edm_channels](auto& vec, std::size_t link) -> auto {
        return vec.begin() + (link * num_edm_channels);
    };

    auto get_iter_end = [num_edm_channels, num_links](auto& vec, std::size_t link) -> auto {
        bool last_link = link == num_links - 1;
        TT_ASSERT(
            (!last_link && ((link + 1) * num_edm_channels < vec.size())) ||
            (last_link && ((link + 1) * num_edm_channels == vec.size())));
        return last_link ? vec.end() : vec.begin() + ((link + 1) * num_edm_channels);
    };

    // Pages per EDM channel
    std::size_t total_num_edm_channels = num_links * num_edm_channels;
    log_trace(tt::LogOp, "total_num_edm_channels: {}", total_num_edm_channels);

    std::vector<uint32_t> num_pages_per_full_chunk(total_num_edm_channels * num_links, 0);

    for (std::size_t link = 0; link < num_links; link++) {
        std::size_t edm_channel_size_in_bytes = cw_per_link_edm_builders.at(link).get_eth_buffer_size_bytes();
        std::size_t num_pages_per_edm_buffer = edm_channel_size_in_bytes / page_size_in_bytes;
        log_trace(
            tt::LogOp,
            "link {}, edm_channel_size_in_bytes: {}, page_size_in_bytes: {}, num_pages_per_edm_buffer: {}",
            link,
            edm_channel_size_in_bytes,
            page_size_in_bytes,
            num_pages_per_edm_buffer);

        std::fill(
            get_iter_begin(num_pages_per_full_chunk, link),
            get_iter_end(num_pages_per_full_chunk, link),
            num_pages_per_edm_buffer);
    }

    log_trace(tt::LogOp, "-- num_pages_per_full_chunk:");
    for (std::size_t l = 0; l < num_links; l++) {
        for (std::size_t w = 0; w < num_edm_channels; w++) {
            log_trace(
                tt::LogOp, "\t\t(link={},worker={}): {}", l, w, num_pages_per_full_chunk.at(l * num_edm_channels + w));
        }
    }

    return WorkerTransferInfo(num_pages_per_full_chunk, num_links, num_edm_channels);
}

static uint32_t compute_maximum_worker_slice_in_bytes(
    ttnn::ccl::Topology topology,
    uint32_t cb_src0_size_pages,
    uint32_t cb_dst0_size_pages,
    uint32_t cb_short_circuit_size_pages,
    std::size_t edm_channel_buffer_size,
    uint32_t page_size) {
    switch (topology) {
        case ttnn::ccl::Topology::Linear:
            // For linear topology, we only want one slice per worker so we don't
            return std::numeric_limits<uint32_t>::max();

        case ttnn::ccl::Topology::Ring:
            return std::min(cb_short_circuit_size_pages, cb_src0_size_pages + cb_dst0_size_pages) * page_size +
                   edm_channel_buffer_size;

        default: TT_ASSERT(false, "Unsupported topology"); return 0;
    };
}

static bool is_cb_buffering_sufficient_to_avoid_deadlock(
    ttnn::ccl::Topology topology,
    ttnn::ccl::InterleavedTensorWorkerSlice const& worker_slice,
    uint32_t cb_src0_size_pages,
    uint32_t cb_dst0_size_pages,
    uint32_t cb_short_circuit_size_pages,
    std::size_t edm_channel_buffer_size,
    uint32_t page_size) {
    uint32_t worker_size_pages_rounded_up =
        tt::round_up(worker_slice.worker_slice_shape.x * worker_slice.worker_slice_shape.y, cb_src0_size_pages / 2);
    uint32_t worker_slice_size_bytes = worker_size_pages_rounded_up * page_size;
    uint32_t available_buffering_capacity = compute_maximum_worker_slice_in_bytes(
        topology, cb_src0_size_pages, cb_dst0_size_pages, cb_short_circuit_size_pages, edm_channel_buffer_size, page_size);
    log_trace(tt::LogOp, "worker_slice.worker_slice_shape.x: {}", worker_slice.worker_slice_shape.x);
    log_trace(tt::LogOp, "worker_slice.worker_slice_shape.y: {}", worker_slice.worker_slice_shape.y);
    log_trace(tt::LogOp, "worker_slice_size_bytes: {}", worker_slice_size_bytes);
    log_trace(tt::LogOp, "worker_size_pages_rounded_up: {}", worker_size_pages_rounded_up);
    log_trace(tt::LogOp, "cb_src0_size_pages: {}", cb_src0_size_pages);
    log_trace(tt::LogOp, "cb_dst0_size_pages: {}", cb_dst0_size_pages);
    log_trace(tt::LogOp, "page_size: {}", page_size);
    log_trace(tt::LogOp, "edm_channel_buffer_size: {}", edm_channel_buffer_size);
    log_trace(tt::LogOp, "available_buffering_capacity: {}", available_buffering_capacity);

    return available_buffering_capacity >= worker_slice_size_bytes;
}

static std::tuple<CBHandle, CBHandle, CBHandle, CBHandle> create_worker_circular_buffers(
    Tensor const& input_tensor,
   ttnn::ccl::CCLOpConfig const& op_config,
    CoreRangeSet const& worker_core_range,
    uint32_t worker_pages_per_transfer,
    tt::tt_metal::Program& program) {
    tt::DataFormat df = tt::tt_metal::datatype_to_dataformat_converter(input_tensor.get_dtype());
    uint32_t page_size_bytes = op_config.get_page_size();

    // Input 0 CB
    uint32_t src0_cb_index = tt::CB::c_in0;
    tt::tt_metal::CircularBufferConfig cb_src0_config =
        tt::tt_metal::CircularBufferConfig(worker_pages_per_transfer * page_size_bytes, {{src0_cb_index, df}})
            .set_page_size(src0_cb_index, page_size_bytes);
    CBHandle cb_src0_workers = CreateCircularBuffer(program, worker_core_range, cb_src0_config);

    // Input 1 CB
    uint32_t src1_cb_index = tt::CB::c_in1;
    tt::tt_metal::CircularBufferConfig cb_src1_config =
        tt::tt_metal::CircularBufferConfig(worker_pages_per_transfer * page_size_bytes, {{src1_cb_index, df}})
            .set_page_size(src1_cb_index, page_size_bytes);
    CBHandle cb_src1_workers = CreateCircularBuffer(program, worker_core_range, cb_src1_config);

    // Dataflow Writer Kernel input CB
    uint32_t cb_dst0_index = tt::CB::c_out0;
    tt::tt_metal::CircularBufferConfig cb_dst0_config =
        tt::tt_metal::CircularBufferConfig(worker_pages_per_transfer * page_size_bytes, {{cb_dst0_index, df}})
            .set_page_size(cb_dst0_index, page_size_bytes);
    CBHandle cb_dst0_sender_workers = CreateCircularBuffer(program, worker_core_range, cb_dst0_config);

    // From reader -> writer kernel (I think I need this because sharing the cb_dst0_sender_workers as output
    // of reader kernel (first output) and math kernel (all subsequent outputs) doesn't seem to work because
    // it seems like the math kernels hold some of the CB state in local variables)
    uint32_t cb_short_circuit_index = tt::CB::c_out1;
    tt::tt_metal::CircularBufferConfig cb_short_circuit_config =
        tt::tt_metal::CircularBufferConfig(
            (worker_pages_per_transfer * page_size_bytes) * 2, {{cb_short_circuit_index, df}})
            .set_page_size(cb_short_circuit_index, page_size_bytes);
    CBHandle cb_short_circuit_sender_workers =
        CreateCircularBuffer(program, worker_core_range, cb_short_circuit_config);

    return {cb_src0_workers, cb_src1_workers, cb_dst0_sender_workers, cb_short_circuit_sender_workers};
}

/*
 * The worker association map tells which other worker (if any) it is logically cooperating with on the local
 * chip. This is really only relevant for line reduce scatter where there will be two "directions" of workers
 * cooperatively producing the result for a part of the tensor. Each of these workers will produce a partial
 * for the given chunk of the tensor but only for inputs from their "direction". The workers must merge (reduce)
 * their partials into a final result in the output tensor. This association map tells the workers which other
 * worker they should be communicating with during the final reduction
 *
 * The current scheme is that one of the worker cores is the designated "master" or "leader" core and the other
 * worker is the follower. The
 */
static std::unordered_map<std::size_t, CoreCoord> build_worker_association_map(std::vector<CoreCoord> const& worker_cores, std::size_t num_edm_channels_per_link, std::size_t num_links, ttnn::ccl::Topology topology) {
    std::unordered_map<std::size_t, CoreCoord> worker_association_map;
    log_trace(tt::LogOp, "building worker_association_map:");
    if (topology == ttnn::ccl::Topology::Linear) {
        TT_ASSERT(num_edm_channels_per_link % 2 == 0);
        TT_ASSERT(worker_cores.size() >= num_links * num_edm_channels_per_link);
        std::size_t workers_per_direction_per_link = num_edm_channels_per_link / 2;
        for (std::size_t l = 0; l < num_links; l++) {
            std::size_t link_workers_offset = l * num_edm_channels_per_link;
            for (std::size_t i = 0; i < workers_per_direction_per_link; i++) {
                worker_association_map[link_workers_offset + i] = worker_cores[link_workers_offset + i + workers_per_direction_per_link];
                worker_association_map[link_workers_offset + i + workers_per_direction_per_link] = worker_cores[link_workers_offset + i];
                log_trace(tt::LogOp, "\tworker {} -> {}", link_workers_offset + i, link_workers_offset + i + workers_per_direction_per_link);
                log_trace(tt::LogOp, "\tworker {} -> {}", link_workers_offset + i + workers_per_direction_per_link, link_workers_offset + i);
            }
        }
    }
    return worker_association_map;
}

operation::ProgramWithCallbacks reduce_scatter_with_workers(
    const Tensor& input_tensor,
    const Tensor& output_tensor,
    ttnn::operations::binary::BinaryOpType reduce_op,
    const uint32_t scatter_split_dim,
    const uint32_t num_links,
    const uint32_t ring_size,
    const uint32_t ring_index,
    const std::optional<chip_id_t> receiver_device_id,
    const std::optional<chip_id_t> sender_device_id,
    ttnn::ccl::Topology topology) {
    log_trace(tt::LogOp, "reduce_scatter_with_workers entry");
    TT_ASSERT(
        input_tensor.get_legacy_shape()[scatter_split_dim] ==
            output_tensor.get_legacy_shape()[scatter_split_dim] * ring_size,
        "Input and output tensor shapes must match");
    TT_ASSERT(
        input_tensor.buffer()->num_pages() % ring_size == 0,
        "Reduce scatter current only supports even divisibility of input tensor(s) across ranks");

    /////////////// Constants/Configuration
    /// Constants/Configuration
    const bool is_linear = topology == ttnn::ccl::Topology::Linear;
    std::vector<Tensor> input_tensors = {input_tensor};
    std::vector<Tensor> output_tensors = {output_tensor};
    ttnn::ccl::EriscDataMoverBufferSharingMode buffer_sharing_mode =ttnn::ccl::EriscDataMoverBufferSharingMode::ROUND_ROBIN;
    auto const& op_config =ttnn::ccl::CCLOpConfig(input_tensors, output_tensors, topology);
    std::unique_ptr<ttnn::ccl::CclOpTensorConfig> input_tensor_config =
        ttnn::ccl::CclOpTensorConfig::build_all_gather_tensor_config(input_tensor);
    std::unique_ptr<ttnn::ccl::CclOpTensorConfig> output_tensor_config =
        ttnn::ccl::CclOpTensorConfig::build_all_gather_tensor_config(output_tensor);
    // // The input tensor is fractured by ring_size so we divi
    std::size_t input_tensor_n_elems_per_slice = input_tensor.volume() / ring_size;
    uint32_t input_tensor_num_units_per_tensor_slice =
        input_tensor_n_elems_per_slice / (tt::constants::TILE_WIDTH * tt::constants::TILE_HEIGHT);

    TT_ASSERT(input_tensor_num_units_per_tensor_slice > 0);
    uint32_t max_num_workers = std::min<std::size_t>(8, input_tensor_num_units_per_tensor_slice);
    bool enable_bidirectional = true;
    auto num_edm_channels_per_link = decide_number_of_edm_channels(op_config, max_num_workers, enable_bidirectional);
    log_trace(tt::LogOp, "num_edm_channels_per_link: {}", num_edm_channels_per_link);
    auto edm_termination_mode = ttnn::ccl::EriscDataMoverTerminationMode::WORKER_INITIATED;

    constexpr std::size_t num_buffers_per_channel = 1; // enable double buffering later
    auto const& edm_builder = create_erisc_datamover_builder(
        num_edm_channels_per_link, op_config.get_page_size(), num_buffers_per_channel, buffer_sharing_mode, edm_termination_mode);
    TT_ASSERT(num_edm_channels_per_link > 0);

    Tensor const& local_chip_tensor = input_tensor;
    Tensor const& local_chip_output_tensor = output_tensor;

    std::map<string, string> worker_defines;
    std::vector<KernelHandle> worker_receiver_kernels;
    std::vector<KernelHandle> worker_sender_kernels;
    std::vector<ttnn::ccl::EriscDatamoverBuilder> cw_per_link_edm_builders(num_links, edm_builder);
    std::vector<ttnn::ccl::EriscDatamoverBuilder> ccw_per_link_edm_builders(num_links, edm_builder);

    //////////////////
    tt::tt_metal::Program program{};
    // Issue #10978: CCLs need to be tagged as having multi-device dependencies, when running on Galaxy.
    program.capture_multi_device_dependencies();
    const auto& device = local_chip_tensor.device();

    auto const& topology_config =
       ttnn::ccl::RingTopology(device, topology, sender_device_id, receiver_device_id, num_links, ring_size, ring_index);

    CoreRangeSet const& worker_core_range = select_worker_cores(op_config, num_links, num_edm_channels_per_link);
    auto const& worker_cores = corerange_to_cores(worker_core_range, std::nullopt, true);

    // Semaphores && CBs
    auto worker_receiver_semaphore_id = tt::tt_metal::CreateSemaphore(program, worker_core_range, 0);
    auto worker_sender_semaphore_id = tt::tt_metal::CreateSemaphore(program, worker_core_range, 0);
    std::optional<uint32_t> receiver_worker_partial_ready_semaphore_id = std::nullopt;

    if (is_linear) {
        receiver_worker_partial_ready_semaphore_id = tt::tt_metal::CreateSemaphore(program, worker_core_range, 0);
    }

    uint32_t cb_num_pages = std::min(input_tensor_num_units_per_tensor_slice,
        (cw_per_link_edm_builders.at(0).get_eth_buffer_size_bytes() / op_config.get_page_size())) * 2;
    uint32_t cb_num_pages_per_packet = cb_num_pages / 2;
    log_trace(tt::LogOp, "cb_num_pages: {}", cb_num_pages);
    auto const& [cb_src0_workers, cb_src1_workers, cb_dst0_sender_workers, cb_short_circuit_sender_workers] =
        create_worker_circular_buffers(local_chip_tensor, op_config, worker_core_range, cb_num_pages, program);

    uint32_t max_worker_slice_in_bytes = compute_maximum_worker_slice_in_bytes(
        topology,
        cb_num_pages,
        cb_num_pages,
        cb_num_pages,
        cw_per_link_edm_builders.at(0).get_eth_buffer_size_bytes(),
        op_config.get_page_size());
    log_trace(tt::LogOp, "max_worker_slice_in_bytes: {}", max_worker_slice_in_bytes);
    std::size_t num_workers = worker_cores.size();
    if (is_linear) {
        num_workers /= 2;
        TT_ASSERT(2 * num_workers == num_edm_channels_per_link * num_links);
    } else {
        TT_ASSERT(num_workers == num_edm_channels_per_link * num_links);
    }
    auto tensor_slicer = ttnn::ccl::RingReduceScatterWrappedTensorSlicer(
        local_chip_tensor,
        local_chip_output_tensor,
        scatter_split_dim,
        ring_index,
        ring_size,
        num_workers,
        max_worker_slice_in_bytes,
        cb_num_pages / 2);

    // Not per buffer because the buffer sharing mode may cause some buffers to share EDM transfers
    WorkerTransferInfo const& worker_transfer_info = compute_num_edm_messages_per_channel(
        op_config,
        tensor_slicer,
        cw_per_link_edm_builders,
        ccw_per_link_edm_builders,
        num_edm_channels_per_link,
        num_links,
        ring_size);

    // Configure the EDM builders
    std::function<bool(uint32_t)> is_worker_in_clockwise_direction_fn =
        [enable_bidirectional, num_edm_channels_per_link, is_linear](uint32_t x) {
            if (is_linear) {
                return x < num_edm_channels_per_link / 2;
            } else {
                return enable_bidirectional ? (x % num_edm_channels_per_link == 0) : true;
            }
        };

    auto const& worker_association_map = build_worker_association_map(worker_cores, num_edm_channels_per_link, num_links, topology);

    EdmInterfaceAddresses edm_interface_addresses;

    // For both linear and ring topologies, we should be able to treat this consistently
    // because the is_worker_in_clockwise_direction_fn is implemented according to the
    // topoology chosen.
    for (std::size_t link = 0; link < num_links; link++) {
        add_worker_config_to_edm_builders(
            device,
            tensor_slicer,
            op_config,
            worker_cores,
            num_edm_channels_per_link,

            cw_per_link_edm_builders,
            ccw_per_link_edm_builders,

            worker_sender_semaphore_id,
            worker_receiver_semaphore_id,
            link,
            ring_size,
            ring_index,
            is_worker_in_clockwise_direction_fn,

            edm_interface_addresses);
    }



    // build the worker kernels
    std::size_t num_duplicate_directions = 1;//is_linear ? 2 : 1;
    for (std::size_t direction = 0; direction < num_duplicate_directions; direction++) {
        for (std::size_t link = 0; link < num_links; link++) {
            log_trace(tt::LogOp, "==============================================");
            log_trace(tt::LogOp, "------------------ Link: {} ------------------", link);
            for (std::size_t worker = 0; worker < num_edm_channels_per_link; worker++) {
                std::size_t global_worker_index = worker + link * num_edm_channels_per_link;

                log_trace(tt::LogOp, "------ Worker: {} (global ID={})", worker, global_worker_index);

                std::size_t worker_tensor_slice_index = !is_linear ?
                    global_worker_index :
                    (worker % num_edm_channels_per_link / 2) + (num_edm_channels_per_link / 2) * link;
                auto const& worker_slice = tensor_slicer.get_worker_slice(worker_tensor_slice_index);
                log_trace(tt::LogOp, "here");
                auto worker_arg_builder = ReduceScatterWorkerArgBuilder(
                    device,
                    op_config,
                    topology_config,
                    worker_slice,
                    worker_transfer_info,
                    edm_termination_mode,
                    worker,
                    link,
                    cb_num_pages_per_packet,
                    worker_sender_semaphore_id,
                    worker_receiver_semaphore_id,
                    receiver_worker_partial_ready_semaphore_id);

                log_trace(tt::LogOp, "worker_cores.at(global_worker_index): {}", worker_cores.at(global_worker_index));
                auto [receiver_kernel_id, sender_kernel_id] = build_reduce_scatter_worker(
                    program,
                    device,
                    topology_config,
                    op_config,
                    worker_arg_builder,
                    edm_interface_addresses,
                    worker_cores.at(global_worker_index),
                    num_edm_channels_per_link,
                    link,
                    worker,
                    reduce_op,
                    scatter_split_dim,
                    num_buffers_per_channel,
                    worker_association_map,
                    is_worker_in_clockwise_direction_fn);
                worker_receiver_kernels.push_back(receiver_kernel_id);
                worker_sender_kernels.push_back(sender_kernel_id);

                TT_FATAL(is_cb_buffering_sufficient_to_avoid_deadlock(
                    topology,
                    worker_slice,
                    cb_num_pages,
                    cb_num_pages,
                    cb_num_pages,
                    cw_per_link_edm_builders.at(0).get_eth_buffer_size_bytes(),
                    op_config.get_page_size()));
            }
        }
    }

    // Generate the EDM kernels
   ttnn::ccl::generate_edm_kernels_for_ring_or_linear_topology(
        program,
        device,
        topology_config,
        cw_per_link_edm_builders,
        ccw_per_link_edm_builders,
        receiver_device_id,
        sender_device_id);

    uint32_t total_num_workers = worker_cores.size();
    auto override_runtime_arguments_callback =
        [topology_config, worker_receiver_kernels, worker_sender_kernels, worker_cores, total_num_workers, ring_index](
            const void* operation,
            Program& program,
            const std::vector<Tensor>& input_tensors,
            const std::vector<std::optional<const Tensor>>& optional_input_tensors,
            const std::vector<Tensor>& output_tensors) {
            const auto& input = input_tensors.at(0);
            const auto& output = output_tensors.at(0);
            TT_ASSERT(worker_sender_kernels.size() == worker_receiver_kernels.size());
            for (uint32_t i = 0; i < worker_sender_kernels.size(); ++i) {
                auto& worker_receiver_runtime_args =
                    GetRuntimeArgs(program, worker_receiver_kernels.at(i), worker_cores.at(i));
                worker_receiver_runtime_args.at(0) = input.buffer()->address();
                worker_receiver_runtime_args.at(1) = output.buffer()->address();

                auto& worker_sender_runtime_args =
                    GetRuntimeArgs(program, worker_sender_kernels.at(i), worker_cores.at(i));
                worker_sender_runtime_args.at(0) = output.buffer()->address();
            }
        };

    return {.program = std::move(program), .override_runtime_arguments_callback = override_runtime_arguments_callback};
}

}  // namespace reduce_scatter_detail
}  // namespace ccl
}  // namespace ttnn
