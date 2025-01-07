// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <c_tensix_core.h>

constexpr uint32_t page_size = get_compile_time_arg_val(0);
constexpr uint32_t pages = get_compile_time_arg_val(1);
constexpr uint32_t tlx = get_compile_time_arg_val(2);
constexpr uint32_t tly = get_compile_time_arg_val(3);
constexpr uint32_t mcaster = get_compile_time_arg_val(4);
constexpr uint32_t width = get_compile_time_arg_val(5);
constexpr uint32_t height = get_compile_time_arg_val(6);
constexpr uint32_t ucast_v = get_compile_time_arg_val(7);
constexpr uint64_t duration = (uint64_t)get_compile_time_arg_val(8) * 1000 * 1000 * 1000;
constexpr uint32_t ucast_size = (uint64_t)get_compile_time_arg_val(9);
constexpr uint32_t mcast_size = (uint64_t)get_compile_time_arg_val(10);

void kernel_main() {
    uint32_t cb_addr = 200000;  // get_write_ptr(0);

    uint64_t done_time = c_tensix_core::read_wall_clock() + duration;

    uint64_t dst_x, dst_y;
    if (ucast_v) {
        dst_x = my_x[NOC_INDEX];
        dst_y = (my_y[NOC_INDEX] == tly) ? tly + height - 1 : my_y[NOC_INDEX] - 1;
    } else {
        dst_x = (my_x[NOC_INDEX] == tlx + width - 1) ? tlx : my_x[NOC_INDEX] + 1;
        dst_y = my_y[NOC_INDEX];
    }

    while (c_tensix_core::read_wall_clock() < done_time) {
        for (uint32_t count = 0; count < 1000; count++) {
            uint32_t read_ptr = cb_addr;
            uint32_t write_ptr = cb_addr;
            if (mcaster) {
                uint64_t dst_noc_multicast_addr =
                    get_noc_multicast_addr(tlx, tly, tlx + width - 1, tly + height - 1, write_ptr);
                noc_async_write_multicast(read_ptr, dst_noc_multicast_addr, mcast_size, width * height, false);
            } else {
                uint64_t noc_write_addr = NOC_XY_ADDR(NOC_X(dst_x), NOC_Y(dst_y), write_ptr);
                noc_async_write(read_ptr, noc_write_addr, ucast_size);
            }
        }
    }

    noc_async_write_barrier();
}
