// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <c_tensix_core.h>

constexpr uint32_t page_size = get_compile_time_arg_val(0);
constexpr uint32_t pages = get_compile_time_arg_val(1);
constexpr uint32_t width = get_compile_time_arg_val(2);
constexpr uint64_t duration = (uint64_t)get_compile_time_arg_val(3) * 1000 * 1000 * 1000;
constexpr uint32_t ucast_size = (uint64_t)get_compile_time_arg_val(4);
constexpr uint32_t mcast_size = (uint64_t)get_compile_time_arg_val(5);

void kernel_main() {
    cb_reserve_back(0, 1);
    uint32_t cb_addr = get_write_ptr(0);

    uint64_t done_time = c_tensix_core::read_wall_clock() + duration;
    bool mcast = (my_x[NOC_INDEX] == 0);
    while (c_tensix_core::read_wall_clock() < done_time) {
        for (uint32_t count = 0; count < 1000; count++) {
            uint32_t read_ptr = cb_addr;
            uint32_t write_ptr = cb_addr;
            if (mcast) {
                uint64_t dst_noc_multicast_addr =
                    get_noc_multicast_addr(2, my_y[NOC_INDEX], width + 1, my_y[NOC_INDEX], write_ptr);
                noc_async_write_multicast(read_ptr, dst_noc_multicast_addr, mcast_size, width, false);
            } else {
                uint64_t noc_write_addr = NOC_XY_ADDR(NOC_X(my_x[NOC_INDEX] + 1), NOC_Y(my_y[NOC_INDEX]), write_ptr);
                noc_async_write(read_ptr, noc_write_addr, ucast_size);
            }
        }
    }

    noc_async_write_barrier();
}
