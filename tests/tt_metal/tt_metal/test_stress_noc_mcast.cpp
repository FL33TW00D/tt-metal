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
const uint32_t DEFAULT_TARGET_HEIGHT = 1;

uint32_t device_num_g = 0;
uint32_t duration_secs_g = DEFAULT_SECONDS;
uint32_t width_g = DEFAULT_TARGET_WIDTH;
uint32_t height_g = DEFAULT_TARGET_HEIGHT;
uint32_t tlx_g = 1;
uint32_t tly_g = 0;
uint32_t mcast_x_g = 0;
uint32_t mcast_y_g = 0;
bool ucast_v_g = false;
uint32_t mcast_size_g = 16;
uint32_t ucast_size_g = 8192;
uint32_t mcast_from_n_eth_g;
bool mcast_from_eth_g;
bool rnd_delay_g = false;
bool rnd_coord_g = false;

void init(int argc, char** argv) {
    std::vector<std::string> input_args(argv, argv + argc);

    if (test_args::has_command_option(input_args, "-h") || test_args::has_command_option(input_args, "--help")) {
        log_info(LogTest, "Usage:");
        log_info(LogTest, "  -v: device number to run on (default 0) ", DEFAULT_SECONDS);
        log_info(LogTest, "  -d: duration in seconds (default {})", DEFAULT_SECONDS);
        log_info(LogTest, "  -width: unicast grid width (default {})", DEFAULT_TARGET_WIDTH);
        log_info(LogTest, "  -height: unicast grid height (default {})", DEFAULT_TARGET_HEIGHT);
        log_info(LogTest, "-tlx: grid top left x");
        log_info(LogTest, "-tly: grid top left y");
        log_info(LogTest, " -mx: mcast core x");
        log_info(LogTest, " -my: mcast core y");
        log_info(LogTest, "  -l: unicast vertically (default horizontally)");
        log_info(LogTest, " -en: mcast from nth idle eth core (ignores -mx,-my)");
        log_info(LogTest, "  -m: mcast packet size");
        log_info(LogTest, "  -u: ucast packet size");
        log_info(LogTest, "-rdelay: insert random delay between noc transactions");
        log_info(LogTest, "-rcoord: use randomized dst noc coords");
        exit(0);
    }

    device_num_g = test_args::get_command_option_uint32(input_args, "-v", 0);
    duration_secs_g = test_args::get_command_option_uint32(input_args, "-d", DEFAULT_SECONDS);
    width_g = test_args::get_command_option_uint32(input_args, "-width", DEFAULT_TARGET_WIDTH);
    height_g = test_args::get_command_option_uint32(input_args, "-height", DEFAULT_TARGET_HEIGHT);
    tlx_g = test_args::get_command_option_uint32(input_args, "-tlx", 0);
    tly_g = test_args::get_command_option_uint32(input_args, "-tly", 0);
    mcast_x_g = test_args::get_command_option_uint32(input_args, "-mx", 0);
    mcast_y_g = test_args::get_command_option_uint32(input_args, "-my", 0);
    mcast_size_g = test_args::get_command_option_uint32(input_args, "-m", 16);
    ucast_size_g = test_args::get_command_option_uint32(input_args, "-u", 8192);
    ucast_v_g = test_args::has_command_option(input_args, "-l");
    mcast_from_n_eth_g = test_args::get_command_option_uint32(input_args, "-en", 0xffff);
    mcast_from_eth_g = (mcast_from_n_eth_g != 0xffff);
    rnd_delay_g = test_args::has_command_option(input_args, "-rdelay");
    rnd_coord_g = test_args::has_command_option(input_args, "-rcoord");

    if (!mcast_from_eth_g && mcast_x_g >= tlx_g && mcast_x_g <= tlx_g + width_g - 1 && mcast_y_g >= tly_g &&
        mcast_y_g <= tly_g + height_g - 1) {
        log_fatal("Mcast core can't be within mcast grid");
        exit(-1);
    }
}

int main(int argc, char** argv) {
    init(argc, argv);

    tt_metal::Device* device = tt_metal::CreateDevice(device_num_g);
    tt_metal::Program program = tt_metal::CreateProgram();

    const auto& eth_cores = device->get_inactive_ethernet_cores();

    CoreRange workers_logical({tlx_g, tly_g}, {tlx_g + width_g - 1, tly_g + height_g - 1});
    CoreCoord mcast_logical(mcast_x_g, mcast_y_g);
    CoreCoord tl_core = device->worker_core_from_logical_core({tlx_g, tly_g});

    if (mcast_from_eth_g) {
        CoreCoord eth_logical(0, mcast_from_n_eth_g);
        bool found = false;
        for (const auto& eth_core : eth_cores) {
            if (eth_logical == eth_core) {
                found = true;
                break;
            }
        }
        if (!found) {
            log_fatal("{} not found in the list of idle eth cores", mcast_from_n_eth_g);
            tt_metal::CloseDevice(device);
            exit(-1);
        }
        mcast_logical = eth_logical;
    }

    std::vector<uint32_t> runtime_args;
    for (int i = 0; i < 128; i++) {
        runtime_args.push_back(rand());
    }

    std::vector<uint32_t> compile_args = {
        PAGE_SIZE,
        PAGES,
        tl_core.x,
        tl_core.y,
        false,
        width_g,
        height_g,
        ucast_v_g,
        duration_secs_g,
        ucast_size_g,
        mcast_size_g,
        rnd_delay_g,
        rnd_coord_g};

    KernelHandle ucast_kernel = tt_metal::CreateKernel(
        program,
        "tests/tt_metal/tt_metal/test_kernels/stress_noc_mcast.cpp",
        workers_logical,
        tt_metal::DataMovementConfig{
            .processor = tt_metal::DataMovementProcessor::RISCV_0,
            .noc = tt_metal::NOC::RISCV_0_default,
            .compile_args = compile_args,
        });
    tt::tt_metal::SetRuntimeArgs(program, ucast_kernel, workers_logical, runtime_args);

    compile_args[4] = true;
    KernelHandle mcast_kernel;
    if (mcast_from_eth_g) {
        mcast_kernel = tt_metal::CreateKernel(
            program,
            "tests/tt_metal/tt_metal/test_kernels/stress_noc_mcast.cpp",
            mcast_logical,
            tt_metal::EthernetConfig{
                .eth_mode = Eth::IDLE,
                .noc = tt_metal::NOC::NOC_0,
                .compile_args = compile_args,
            });
    } else {
        mcast_kernel = tt_metal::CreateKernel(
            program,
            "tests/tt_metal/tt_metal/test_kernels/stress_noc_mcast.cpp",
            mcast_logical,
            tt_metal::DataMovementConfig{
                .processor = tt_metal::DataMovementProcessor::RISCV_0,
                .noc = tt_metal::NOC::RISCV_0_default,
                .compile_args = compile_args,
            });
    }
    tt::tt_metal::SetRuntimeArgs(program, mcast_kernel, mcast_logical, runtime_args);

    CoreCoord mcast_virtual = mcast_from_eth_g ? device->ethernet_core_from_logical_core(mcast_logical)
                                               : device->worker_core_from_logical_core(mcast_logical);

    log_info(
        LogTest,
        "MCast {} core: {}, virtual {}, writing {} bytes per xfer",
        mcast_from_eth_g ? "ETH" : "TENSIX",
        mcast_logical,
        mcast_virtual,
        mcast_size_g);
    log_info(LogTest, "Unicast grid: {}, writing {} bytes per xfer", workers_logical.str(), ucast_size_g);

    if (rnd_coord_g) {
        log_info("Randomizing ucast noc write coordinates");
    } else {
        log_info("Writing {}", ucast_v_g ? "bottom to top" : "left to right");
    }

    if (rnd_delay_g) {
        log_info("Randomizing delay");
    }
    log_info(LogTest, "Running for {} seconds", duration_secs_g);

    tt::tt_metal::detail::LaunchProgram(device, program, true);
    tt_metal::CloseDevice(device);
}
