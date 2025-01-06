// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdint>
#include <functional>
#include <random>
#include <string>

#include "core_coord.hpp"
#include "logger.hpp"
#include "tt_metal/host_api.hpp"
#include "tt_metal/detail/tt_metal.hpp"
#include "tt_metal/llrt/rtoptions.hpp"
#include "tt_metal/common/metal_soc_descriptor.h"
#include "tt_metal/impl/event/event.hpp"
#include "tt_metal/impl/dispatch/command_queue.hpp"
#include "tt_metal/impl/device/device.hpp"

using namespace tt;

const uint32_t CB_ELEMENTS = 2048;
const uint32_t PAGE_SIZE = CB_ELEMENTS * sizeof(float);
const uint32_t PAGES = 2;
const uint32_t DEFAULT_SECONDS = 10;
const uint32_t DEFAULT_TARGET_WIDTH = 1;

uint32_t device_num_g = 0;
uint32_t duration_secs_g = DEFAULT_SECONDS;
uint32_t width_g = DEFAULT_TARGET_WIDTH;
uint32_t mcast_size_g = 16;
uint32_t ucast_size_g = 8192;
uint32_t start_row_g = 0;
uint32_t rows_g = 1;

void init(int argc, char** argv) {
    std::vector<std::string> input_args(argv, argv + argc);

    if (test_args::has_command_option(input_args, "-h") || test_args::has_command_option(input_args, "--help")) {
        log_info(LogTest, "Usage:");
        log_info(LogTest, "  -v: device number to run on (default 0), ", DEFAULT_SECONDS);
        log_info(LogTest, "  -d: duration in seconds (default {}), ", DEFAULT_SECONDS);
        log_info(LogTest, "  -w: target core width (default {}), ", DEFAULT_TARGET_WIDTH);
        log_info(LogTest, "  -m: mcast packet size");
        log_info(LogTest, "  -u: ucast packet size");
        log_info(LogTest, " -sr: start row");
        log_info(LogTest, "  nr: rows");
        exit(0);
    }

    device_num_g = test_args::get_command_option_uint32(input_args, "-v", 0);
    duration_secs_g = test_args::get_command_option_uint32(input_args, "-d", DEFAULT_SECONDS);
    width_g = test_args::get_command_option_uint32(input_args, "-w", DEFAULT_TARGET_WIDTH);
    mcast_size_g = test_args::get_command_option_uint32(input_args, "-m", 16);
    ucast_size_g = test_args::get_command_option_uint32(input_args, "-u", 8192);
    start_row_g = test_args::get_command_option_uint32(input_args, "-sr", 0);
    rows_g = test_args::get_command_option_uint32(input_args, "-r", 1);
}

int main(int argc, char** argv) {
    init(argc, argv);

    tt_metal::Device* device = tt_metal::CreateDevice(device_num_g);
    CommandQueue& cq = device->command_queue();
    tt_metal::Program program = tt_metal::CreateProgram();

    CoreRange workers({0, start_row_g}, {width_g, start_row_g + rows_g - 1});  // right column does no work
    std::vector<uint32_t> compile_args = {
        PAGE_SIZE,
        PAGES,
        width_g,
        duration_secs_g,
        ucast_size_g,
        mcast_size_g,
    };

    tt_metal::CircularBufferConfig cb_config =
        tt_metal::CircularBufferConfig(PAGE_SIZE * PAGES, {{0, tt::DataFormat::Float32}}).set_page_size(0, PAGE_SIZE);
    auto cb = tt_metal::CreateCircularBuffer(program, workers, cb_config);

    auto dm0 = tt_metal::CreateKernel(
        program,
        "tests/tt_metal/tt_metal/test_kernels/stress_noc_mcast.cpp",
        workers,
        tt_metal::DataMovementConfig{
            .processor = tt_metal::DataMovementProcessor::RISCV_0,
            .noc = tt_metal::NOC::RISCV_0_default,
            .compile_args = compile_args,
        });

    log_info(LogTest, "Running on cores: {}", workers.str());
    log_info(LogTest, "MCast core(s) writing {} bytes per xfer", mcast_size_g);
    log_info(LogTest, "UCast core(s) writing {} bytes per xfer", ucast_size_g);
    log_info(LogTest, "Running for {} seconds", duration_secs_g);

    EnqueueProgram(cq, program, true);
    tt_metal::CloseDevice(device);
}
