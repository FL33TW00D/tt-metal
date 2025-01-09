// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <memory>
#include <vector>

#include "lightmetal_fixture.hpp"
// #include "dispatch_test_utils.hpp"
#include "detail/tt_metal.hpp"
#include "tt_metal/common/env_lib.hpp"
#include "gtest/gtest.h"
#include "tt_metal/impl/allocator/allocator.hpp"
#include "tt_metal/impl/program/program.hpp"
#include "tt_metal/impl/device/device.hpp"
#include "tt_metal/impl/dispatch/command_queue.hpp"
#include "tt_metal/common/logger.hpp"
#include "tt_metal/common/scoped_timer.hpp"
#include "tt_metal/host_api.hpp"

using std::vector;
using namespace tt;
using namespace tt::tt_metal;

namespace lightmetal_test_helpers {

// Single RISC, no CB's here. Very simple.
Program create_simple_datamovement_program(Buffer& input, Buffer& output, Buffer& l1_buffer) {
    Program program = CreateProgram();
    Device* device = input.device();
    constexpr CoreCoord core = {0, 0};

    KernelHandle dram_copy_kernel_id = CreateKernel(
        program,
        "tt_metal/programming_examples/loopback/kernels/loopback_dram_copy.cpp",
        core,
        DataMovementConfig{.processor = DataMovementProcessor::RISCV_0, .noc = NOC::RISCV_0_default});

    // Since all interleaved buffers have size == page_size, they are entirely contained in the first DRAM bank
    const uint32_t input_bank_id = 0;
    const uint32_t output_bank_id = 0;

    // Handle Runtime Args
    const std::vector<uint32_t> runtime_args = {
        l1_buffer.address(), input.address(), input_bank_id, output.address(), output_bank_id, l1_buffer.size()};

    // Note - this interface doesn't take Buffer, just data.
    SetRuntimeArgs(program, dram_copy_kernel_id, core, runtime_args);

    return program;
}

// Copied from test_EnqueueTrace.cpp
Program create_simple_unary_program(Buffer& input, Buffer& output) {
    Program program = CreateProgram();
    Device* device = input.device();
    CoreCoord worker = {0, 0};
    auto reader_kernel = CreateKernel(
        program,
        "tt_metal/kernels/dataflow/reader_unary.cpp",
        worker,
        DataMovementConfig{.processor = DataMovementProcessor::RISCV_1, .noc = NOC::RISCV_1_default});

    auto writer_kernel = CreateKernel(
        program,
        "tt_metal/kernels/dataflow/writer_unary.cpp",
        worker,
        DataMovementConfig{.processor = DataMovementProcessor::RISCV_0, .noc = NOC::RISCV_0_default});

    auto sfpu_kernel = CreateKernel(
        program,
        "tt_metal/kernels/compute/eltwise_sfpu.cpp",
        worker,
        ComputeConfig{
            .math_approx_mode = true,
            .compile_args = {1, 1},
            .defines = {{"SFPU_OP_EXP_INCLUDE", "1"}, {"SFPU_OP_CHAIN_0", "exp_tile_init(); exp_tile(0);"}}});

    CircularBufferConfig input_cb_config = CircularBufferConfig(2048, {{tt::CBIndex::c_0, tt::DataFormat::Float16_b}})
                                               .set_page_size(tt::CBIndex::c_0, 2048);

    CoreRange core_range({0, 0});
    CreateCircularBuffer(program, core_range, input_cb_config);
    std::shared_ptr<RuntimeArgs> writer_runtime_args = std::make_shared<RuntimeArgs>();
    std::shared_ptr<RuntimeArgs> reader_runtime_args = std::make_shared<RuntimeArgs>();

    *writer_runtime_args = {&output, (uint32_t)0, output.num_pages()};

    *reader_runtime_args = {&input, (uint32_t)0, input.num_pages()};

    SetRuntimeArgs(device, detail::GetKernel(program, writer_kernel), worker, writer_runtime_args);
    SetRuntimeArgs(device, detail::GetKernel(program, reader_kernel), worker, reader_runtime_args);

    CircularBufferConfig output_cb_config = CircularBufferConfig(2048, {{tt::CBIndex::c_16, tt::DataFormat::Float16_b}})
                                                .set_page_size(tt::CBIndex::c_16, 2048);

    CreateCircularBuffer(program, core_range, output_cb_config);
    return program;
}

}  // namespace lightmetal_test_helpers

namespace lightmetal_basic_tests {

constexpr bool kBlocking = true;
constexpr bool kNonBlocking = false;
vector<bool> blocking_flags = {kBlocking, kNonBlocking};

// Test that create buffer, write, readback, and verify works when traced + replayed.
TEST_F(SingleDeviceLightMetalFixture, CreateBufferEnqueueWriteRead_Sanity) {
    CreateDevice(2048);

    CommandQueue& command_queue = this->device_->command_queue();
    uint32_t num_loops = parse_env<int>("NUM_LOOPS", 1);
    bool keep_buffers_alive = std::getenv("KEEP_BUFFERS_ALIVE");  // For testing, keep buffers alive for longer.
    std::vector<std::shared_ptr<Buffer>> buffers_vec;

    for (uint32_t loop_idx = 0; loop_idx < num_loops; loop_idx++) {
        log_info(tt::LogTest, "Running loop: {}", loop_idx);

        // Switch to use top level CreateBuffer API that has trace support.
        uint32_t size_bytes = 64;  // 16 elements.
        auto buffer = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
        log_info(
            tt::LogTest,
            "created buffer loop: {} with size: {} bytes addr: 0x{:x}",
            loop_idx,
            buffer->size(),
            buffer->address());

        if (keep_buffers_alive) {
            buffers_vec.push_back(buffer);
        }

        // We don't want to capture inputs in binary, but do it to start for testing.
        uint32_t start_val = loop_idx * 100;
        vector<uint32_t> input_data(buffer->size() / sizeof(uint32_t), 0);
        for (uint32_t i = 0; i < input_data.size(); i++) {
            input_data[i] = start_val + i;
        }
        log_info(tt::LogTest, "initialize input_data with {} elements start_val: {}", input_data.size(), start_val);

        vector<uint32_t> readback_data;
        readback_data.resize(input_data.size());  // This is required.

        // Write data to buffer, then readback and verify.
        EnqueueWriteBuffer(command_queue, *buffer, input_data.data(), true);
        EnqueueReadBuffer(command_queue, *buffer, readback_data.data(), true);
        EXPECT_TRUE(input_data == readback_data);

        // For dev/debug go ahead and print the results. Had a replay bug, was seeing wrong data.
        for (size_t i = 0; i < readback_data.size(); i++) {
            log_info(tt::LogMetalTrace, "loop: {} rd_data i: {:3d} => data: {}", loop_idx, i, readback_data[i]);
        }
    }

    Finish(command_queue);
}

// Test simple case of single datamovement program on single RISC works for trace + replay.
TEST_F(SingleDeviceLightMetalFixture, SingleRISCDataMovementSanity) {
    CreateDevice(2048);

    uint32_t size_bytes = 64;  // 16 elements.
    auto input = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
    auto output = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
    auto l1_buffer = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::L1});
    log_info(
        tt::LogTest,
        "Created 3 Buffers. input: 0x{:x} output: 0x{:x} l1_buffer: 0x{:x}",
        input->address(),
        output->address(),
        l1_buffer->address());

    CommandQueue& command_queue = this->device_->command_queue();

    Program simple_program = lightmetal_test_helpers::create_simple_datamovement_program(*input, *output, *l1_buffer);
    vector<uint32_t> input_data(input->size() / sizeof(uint32_t), 0);
    for (uint32_t i = 0; i < input_data.size(); i++) {
        input_data[i] = i;
    }

    vector<uint32_t> eager_output_data;
    eager_output_data.resize(input_data.size());

    // Write data to buffer, enqueue program, then readback and verify.
    EnqueueWriteBuffer(command_queue, *input, input_data.data(), true);
    EnqueueProgram(command_queue, simple_program, true);
    EnqueueReadBuffer(command_queue, *output, eager_output_data.data(), true);
    EXPECT_TRUE(eager_output_data == input_data);

    // For dev/debug go ahead and print the results
    for (size_t i = 0; i < eager_output_data.size(); i++) {
        log_info(tt::LogMetalTrace, "i: {:3d} input: {} output: {}", i, input_data[i], eager_output_data[i]);
    }

    Finish(command_queue);
}

// Test simple case of 3 riscs used for datamovement and compute works for trace + replay.
TEST_F(SingleDeviceLightMetalFixture, ThreeRISCDataMovementComputeSanity) {
    CreateDevice(2048);

    uint32_t size_bytes = 64;  // 16 elements.
    auto input = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
    auto output = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
    log_info(tt::LogTest, "Created 2 Buffers. input: 0x{:x} output: 0x{:x}", input->address(), output->address());

    CommandQueue& command_queue = this->device_->command_queue();

    // TODO (kmabee) - There is issue with using make_shared, revisit this.
    // auto simple_program = std::make_shared<Program>(lightmetal_test_helpers::create_simple_unary_program(*input,
    // *output));
    auto simple_program = lightmetal_test_helpers::create_simple_unary_program(*input, *output);

    vector<uint32_t> input_data(input->size() / sizeof(uint32_t), 0);
    for (uint32_t i = 0; i < input_data.size(); i++) {
        input_data[i] = i;
    }

    vector<uint32_t> eager_output_data;
    eager_output_data.resize(input_data.size());

    log_info(tt::LogTest, "About to EnqueueWriteBuffer");

    // Write data to buffer, enqueue program, then readback and verify.
    EnqueueWriteBuffer(command_queue, *input, input_data.data(), true);
    log_info(tt::LogTest, "About to EnqueueProgram");
    EnqueueProgram(command_queue, simple_program, true);
    log_info(tt::LogTest, "Done EnqueueProgram");
    EnqueueReadBuffer(command_queue, *output, eager_output_data.data(), true);

    // For dev/debug go ahead and print the results
    for (size_t i = 0; i < eager_output_data.size(); i++) {
        log_info(tt::LogMetalTrace, "i: {:3d} input: {} output: {}", i, input_data[i], eager_output_data[i]);
    }

    Finish(command_queue);
}

// Test simple compute test with metal trace, but no explicit trace replay (added automatically by light metal trace).
TEST_F(SingleDeviceLightMetalFixture, SingleProgramTraceCapture) {
    CreateDevice(2048);

    // Must use CreateBuffer not Buffer::create()
    uint32_t size_bytes = 64;  // 16 elements. Was 2048 in original test.
    auto input = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
    auto output = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});

    CommandQueue& command_queue = this->device_->command_queue();

    Program simple_program = lightmetal_test_helpers::create_simple_unary_program(*input, *output);

    // Setup input data for program with some simple values.
    vector<uint32_t> input_data(input->size() / sizeof(uint32_t), 0);
    for (uint32_t i = 0; i < input_data.size(); i++) {
        input_data[i] = i;
    }

    vector<uint32_t> eager_output_data, trace_output_data;
    eager_output_data.resize(input_data.size());
    trace_output_data.resize(input_data.size());

    // Initial run w/o trace. Preloads binary cache, and captures golden output.
    EnqueueWriteBuffer(command_queue, *input, input_data.data(), true);
    EnqueueProgram(command_queue, simple_program, true);
    EnqueueReadBuffer(command_queue, *output, eager_output_data.data(), true);

    // Write junk to output buffer to help make sure trace run from standalone binary works.
    bool debug_write_junk_to_output_buffer = true;
    if (debug_write_junk_to_output_buffer) {
        vector<uint32_t> dummy_data(input->size() / sizeof(uint32_t), 0xDEADBEEF);
        EnqueueWriteBuffer(command_queue, *output, dummy_data.data(), true);
        EnqueueReadBuffer(command_queue, *output, trace_output_data.data(), true);
        for (size_t i = 0; i < trace_output_data.size(); i++) {
            log_info(
                tt::LogMetalTrace,
                "i: {:3d} input: {:x} output: {:x} after writing dummy data",
                i,
                input_data[i],
                trace_output_data[i]);
        }
    }

    // Now enable Metal Trace and run program again for capture.
    uint32_t tid = BeginTraceCapture(this->device_, command_queue.id());
    EnqueueProgram(command_queue, simple_program, false);
    EndTraceCapture(this->device_, command_queue.id(), tid);

    // Note: Purposely avoiding EnqueueTrace/ReplayTrace here. The trace will be
    // automatically be replayed from LightMetal Binary w/ LoadTrace+ReplayTrace.
    const bool debug_run_trace_during_capture = parse_env<bool>("RUN_TRACE_DURING_CAPTURE", false);
    if (debug_run_trace_during_capture) {
        EnqueueTrace(command_queue, tid, true);
    }

    // Read the output buffer. Without actually enqueue/replaying the trace in this test during
    // capture, cannot compare to eager/golde output, only visual check during standalone playback.
    EnqueueReadBuffer(command_queue, *output, trace_output_data.data(), true);
    if (debug_run_trace_during_capture) {
        log_info(tt::LogTest, "Comparing eager_output_data == trace_output_data");
        EXPECT_TRUE(eager_output_data == trace_output_data);
    }

    // For dev/debug go ahead and print the results
    for (size_t i = 0; i < trace_output_data.size(); i++) {
        log_info(tt::LogMetalTrace, "i: {:3d} input: {:x} trace_output: {:x}", i, input_data[i], trace_output_data[i]);
    }

    // Done
    Finish(command_queue);
    ReleaseTrace(this->device_, tid);
}

// Test simple compute test with metal trace, but no explicit trace replay (added automatically by light metal trace).
TEST_F(SingleDeviceLightMetalFixture, TwoProgramTraceCapture) {
    CreateDevice(2048);

    // Must use CreateBuffer not Buffer::create()
    uint32_t size_bytes = 64;  // 16 elements. Was 2048 in original test.
    auto input = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
    auto interm = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});
    auto output = CreateBuffer(InterleavedBufferConfig{this->device_, size_bytes, size_bytes, BufferType::DRAM});

    CommandQueue& command_queue = this->device_->command_queue();

    Program op0 = lightmetal_test_helpers::create_simple_unary_program(*input, *interm);
    Program op1 = lightmetal_test_helpers::create_simple_unary_program(*interm, *output);

    // Setup input data for program with some simple values.
    vector<uint32_t> input_data(input->size() / sizeof(uint32_t), 0);
    for (uint32_t i = 0; i < input_data.size(); i++) {
        input_data[i] = i;
    }

    vector<uint32_t> eager_output_data, trace_output_data;
    eager_output_data.resize(input_data.size());
    trace_output_data.resize(input_data.size());

    // Initial run w/o trace. Preloads binary cache, and captures golden output.
    EnqueueWriteBuffer(command_queue, *input, input_data.data(), true);
    EnqueueProgram(command_queue, op0, true);
    EnqueueProgram(command_queue, op1, true);
    EnqueueReadBuffer(command_queue, *output, eager_output_data.data(), true);
    Finish(command_queue);

    // Write junk to output buffer to help make sure trace run from standalone binary works.
    bool debug_write_junk_to_output_buffer = true;
    if (debug_write_junk_to_output_buffer) {
        vector<uint32_t> dummy_data(input->size() / sizeof(uint32_t), 0xDEADBEEF);
        EnqueueWriteBuffer(command_queue, *output, dummy_data.data(), true);
        EnqueueReadBuffer(command_queue, *output, trace_output_data.data(), true);
        for (size_t i = 0; i < trace_output_data.size(); i++) {
            log_info(
                tt::LogMetalTrace,
                "i: {:3d} input: {:x} output: {:x} after writing dummy data",
                i,
                input_data[i],
                trace_output_data[i]);
        }
    }

    // Now enable Metal Trace and run program again for capture.
    uint32_t tid = BeginTraceCapture(this->device_, command_queue.id());
    EnqueueProgram(command_queue, op0, false);
    EnqueueProgram(command_queue, op1, false);
    EndTraceCapture(this->device_, command_queue.id(), tid);

    // Note: Purposely avoiding EnqueueTrace/ReplayTrace here. The trace will be
    // automatically be replayed from LightMetal Binary w/ LoadTrace+ReplayTrace.
    const bool debug_run_trace_during_capture = parse_env<bool>("RUN_TRACE_DURING_CAPTURE", false);
    if (debug_run_trace_during_capture) {
        EnqueueTrace(command_queue, tid, true);
    }

    // Read the output buffer. Without actually enqueue/replaying the trace in this test during
    // capture, cannot compare to eager/golde output, only visual check during standalone playback.
    EnqueueReadBuffer(command_queue, *output, trace_output_data.data(), true);
    if (debug_run_trace_during_capture) {
        log_info(tt::LogTest, "Comparing eager_output_data == trace_output_data");
        EXPECT_TRUE(eager_output_data == trace_output_data);
    }

    // For dev/debug go ahead and print the results
    for (size_t i = 0; i < trace_output_data.size(); i++) {
        log_info(
            tt::LogMetalTrace,
            "i: {:3d} input: {:x} eager_output_data: {:x} trace_output: {:x}",
            i,
            input_data[i],
            eager_output_data[i],
            trace_output_data[i]);
    }

    // Done
    Finish(command_queue);
    ReleaseTrace(this->device_, tid);
}

}  // namespace lightmetal_basic_tests
