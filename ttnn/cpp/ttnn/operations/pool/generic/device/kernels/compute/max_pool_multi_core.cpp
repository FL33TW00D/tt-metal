// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#include "compute_kernel_api/tilize.h"
#include "compute_kernel_api/reduce.h"
#include "compute_kernel_api/pack_untilize.h"
#include "tt_metal/hw/inc/debug/dprint_tensix.h"

#define DEBUG_PRINT 0

#if DEBUG_PRINT == 1
    #include "debug/dprint.h"

    inline void print_tile_rows(uint32_t cb_id, uint32_t rows = 32, uint32_t tile_id = 0, bool untilize = false) {
        // UNPACK(( DPRINT << "======" << ENDL() ));
        for (uint16_t r = 0; r < rows; ++ r) {
            UNPACK(( DPRINT << (uint)r << " :: " << TileSlice(cb_id, tile_id, SliceRange{.h0 = (uint8_t)r, .h1 = (uint8_t)(r + 1), .hs = (uint8_t)1, .w0 = (uint8_t)0, .w1 = (uint8_t)32, .ws = (uint8_t)1}, true, untilize) << ENDL() ));
        }
        // UNPACK(( DPRINT << "++++++" << ENDL() ));
    }

    // inline void print_full_tile(uint32_t cb_id, uint32_t tile_id = 0, bool untilize = false) {
    //     UNPACK(( DPRINT << "======" << ENDL() ));
    //     for (uint16_t r = 0; r < 32; ++ r) {
    //         SliceRange sr = SliceRange{.h0 = r, .h1 = (uint16_t)(r+1), .hs = 1, .w0 = 0, .w1 = 32, .ws = 1};
    //         UNPACK(( DPRINT << (uint)r << TileSlice(cb_id, tile_id, sr, true, untilize) << ENDL() ));
    //     }
    //     UNPACK(( DPRINT << "++++++" << ENDL() ));
    // }

    // inline void print_cb_details(uint32_t cb_id) {
    //     DPRINT << "cb_id " << cb_id << ": { "
    //             << "size: " << cb_interface[cb_id].fifo_size << ", "
    //             << "limit: " << cb_interface[cb_id].fifo_limit << ", "
    //             << "page_size: " << cb_interface[cb_id].fifo_page_size << ", "
    //             << "num_pages: " << cb_interface[cb_id].fifo_num_pages << ", "
    //             << "rd_ptr: " << cb_interface[cb_id].fifo_rd_ptr << ", "
    //             << "wr_ptr: " << cb_interface[cb_id].fifo_wr_ptr << ", "
    //             << "wr_tile_ptr: " << cb_interface[cb_id].fifo_wr_tile_ptr << " }" << ENDL();
    // }
#endif

uint16_t minus_inf = 63487;

#define ALWI inline __attribute__((always_inline))

// Fill an L1 buffer with the given val
// WARNING: Use with caution as there's no memory protection. Make sure size is within limits
ALWI bool fill_with_val(uint32_t begin_addr, uint32_t n, uint16_t val) {
    // simplest impl:
    volatile tt_l1_ptr uint32_t* ptr = reinterpret_cast<volatile tt_l1_ptr uint32_t*>(begin_addr);
    for (uint32_t i = 0; i < n/2; ++ i) {
        ptr[i] = (val | (val << 16));
    }
    return true;
}

template<uint32_t num_output_tiles, bool is_partial_tile, uint32_t split_reader, uint32_t unpA_face_r_dim>
inline void reduce_h_fused(
    const uint32_t in_cb_id,
    const uint32_t in_scalar_cb_id,
    const uint32_t in_stick_index,
    const uint32_t out_cb_id) {

    constexpr uint32_t num_faces_in_tile = is_partial_tile ? 1 : 2;
    constexpr uint32_t num_out_rows = 1;

    cb_reserve_back(out_cb_id, 1);
    const uint32_t curr_in_cb_id = split_reader ? (in_cb_id + (in_stick_index & 0x1)) : in_cb_id;
    cb_wait_front(curr_in_cb_id, 1);
    tile_regs_acquire();
    unpack_tilizeA_B_block(curr_in_cb_id, in_scalar_cb_id, num_output_tiles, 0 /*tile idx for Src b is 0 because only 1 tile of constants is loaded*/, num_faces_in_tile /* unpack 1 or 2 faces ) */, unpA_face_r_dim);
    for (uint32_t c_i = 0; c_i < num_output_tiles; ++c_i) {
        reduce_tile_math(c_i,  num_faces_in_tile /* reduce 1 or 2 faces */);
    }
    //if (curr_in_cb_id != in_cb_id) {
        dprint_tensix_dest_reg(0);
    //}
    cb_pop_front(curr_in_cb_id, 1);
    tile_regs_wait();
    tile_regs_commit();
    pack_untilize_dst<num_output_tiles>(out_cb_id, 1/*out_subblock_h*/, 0, num_out_rows, num_faces_in_tile);  /* pack 1 row (1x16 or 1x32) */
    tile_regs_release();
    //if (curr_in_cb_id != in_cb_id) {
    //    print_tile_rows(out_cb_id, 32, 0, false);
    //}
    cb_push_back(out_cb_id, 1);
}

namespace NAMESPACE {

void MAIN {

    // NOTE: here it is assumed that in_ntiles_hw == 1. General cases not handled yet.
    constexpr uint32_t in_ntiles_hw = get_compile_time_arg_val(0);
    constexpr uint32_t in_ntiles_c = get_compile_time_arg_val(1);
    constexpr uint32_t window_size_hw = get_compile_time_arg_val(3);
    constexpr uint32_t out_h = get_compile_time_arg_val(4);
    constexpr uint32_t out_w = get_compile_time_arg_val(5);

    constexpr uint32_t split_reader = get_compile_time_arg_val(12);

    constexpr uint32_t nsticks_per_core = get_compile_time_arg_val(13);
    constexpr uint32_t in_c = get_compile_time_arg_val(14);
    constexpr uint32_t in_nblocks_c = get_compile_time_arg_val(15);

    constexpr uint32_t in_cb_id = tt::CB::c_in0; // and tt::CB::c_in1 for split reader
    constexpr uint32_t in_scalar_cb_id = tt::CB::c_in4;
    constexpr uint32_t in_tiled_cb_id = tt::CB::c_intermed0;
    constexpr uint32_t out_cb_id = tt::CB::c_out0;

    constexpr bool is_partial_tile = in_c < 32;
    static_assert((!is_partial_tile || (in_c == 16)), "Partial tile must have c_dim 16");
    constexpr uint32_t num_faces_in_tile = is_partial_tile ? 1 : 2;
    constexpr uint32_t num_out_rows = 1;

    constexpr uint32_t MAX_TILES_PER_REDUCTION = 8;

    constexpr uint32_t max_tiles_per_iter = in_ntiles_c < MAX_TILES_PER_REDUCTION ? in_ntiles_c : MAX_TILES_PER_REDUCTION;
    constexpr uint32_t partial_iter_output_tiles = in_ntiles_c % MAX_TILES_PER_REDUCTION;
    tilizeA_B_reduce_init(in_cb_id,
                                in_scalar_cb_id,
                                max_tiles_per_iter,
                                out_cb_id,
                                num_faces_in_tile,
                                window_size_hw);
    pack_untilize_dst_init_short<max_tiles_per_iter>(out_cb_id, num_out_rows, num_faces_in_tile); /* pack 1 row (1x16 or 1x32) */

    cb_wait_front(in_scalar_cb_id, 1);
    for (uint32_t i = 0; i < nsticks_per_core; ++ i) {
        for (uint32_t b_i = 0; b_i < in_nblocks_c; ++ b_i) {
            if (b_i == in_nblocks_c - 1 && partial_iter_output_tiles > 0) {
                //tilize_uninit(in_cb_id, out_cb_id);
                pack_untilize_uninit(out_cb_id);
                /* tilizeA_B_reduce_init_short(in_cb_id,
                                            in_scalar_cb_id,
                                            partial_iter_output_tiles,
                                            out_cb_id,
                                            num_faces_in_tile,
                                            window_size_hw); */
                pack_untilize_dst_init_short<partial_iter_output_tiles>(out_cb_id, num_out_rows, num_faces_in_tile); /* pack 1 row (1x16 or 1x32) */
                reduce_h_fused<partial_iter_output_tiles, is_partial_tile, split_reader, window_size_hw>(in_cb_id, in_scalar_cb_id, i, out_cb_id);
            }
            else {
                //tilize_uninit(in_cb_id, out_cb_id);
                pack_untilize_uninit(out_cb_id);
                /* tilizeA_B_reduce_init_short(in_cb_id,
                                            in_scalar_cb_id,
                                            max_tiles_per_iter,
                                            out_cb_id,
                                            num_faces_in_tile,
                                            window_size_hw); */
                pack_untilize_dst_init_short<max_tiles_per_iter>(out_cb_id, num_out_rows, num_faces_in_tile); /* pack 1 row (1x16 or 1x32) */
                reduce_h_fused<max_tiles_per_iter, is_partial_tile, split_reader, window_size_hw>(in_cb_id, in_scalar_cb_id, i, out_cb_id);
            }
        }
    }
    cb_pop_front(in_scalar_cb_id, 1);
}

}  // namespace NAMESPACE
