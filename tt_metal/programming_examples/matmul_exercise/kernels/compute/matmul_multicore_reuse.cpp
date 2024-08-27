// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#include "compute_kernel_api/matmul.h"
#include "compute_kernel_api/tile_move_copy.h"

namespace NAMESPACE {
void MAIN {
    uint32_t in0_block_w = get_compile_time_arg_val(0);              // inner block size in tiles
    uint32_t in0_num_subblocks = get_compile_time_arg_val(1);        // outer row block size (in inner row blocks)
    uint32_t in0_block_num_tiles = get_compile_time_arg_val(2);      // out_subblock_h*in0_block_w*in0_num_subblocks;
    uint32_t in0_subblock_num_tiles = get_compile_time_arg_val(3);   // out_subblock_h*in0_block_w
    uint32_t in1_num_subblocks = get_compile_time_arg_val(4);        // outer column block size (in inner column blocks)
    uint32_t in1_block_num_tiles = get_compile_time_arg_val(5);      // out_subblock_w*in0_block_w* in1_num_subblocks;
    uint32_t in1_per_core_w = get_compile_time_arg_val(6);           // out_subblock_w*in1_num_subblocks
    uint32_t num_blocks = get_compile_time_arg_val(7);               // outer inner dim (in inner dim blocks)
    uint32_t out_subblock_h = get_compile_time_arg_val(8);           // inner row block size in tiles
    uint32_t out_subblock_w = get_compile_time_arg_val(9);           // inner column block size in tiles
    uint32_t out_subblock_num_tiles = get_compile_time_arg_val(10);  // out_subblock_h * out_subblock_w;

    constexpr uint32_t in0_cb_id = tt::CB::c_in0;
    constexpr uint32_t in1_cb_id = tt::CB::c_in1;
    constexpr uint32_t out_cb_id = tt::CB::c_out0;
    constexpr uint32_t mm_partials_cb_id = tt::CB::c_intermed0;

    // TODO: Initialize mm using the `mm_init` API
    uint32_t transpose = 0;
    mm_init(in0_cb_id, in1_cb_id, out_cb_id, transpose);

    bool spill = num_blocks > 1;
    bool enable_reload = false;
    uint32_t out_num_tiles_to_wait = out_subblock_num_tiles;

    ///////////////////////////////////////////////
    // TODO: Fill the /* */ with appropriate code
    ///////////////////////////////////////////////
    for (uint32_t block = 0; block < num_blocks /* */; block++) {
        bool last_out = block == (num_blocks - 1);

        cb_wait_front(in0_cb_id, in0_block_num_tiles /* */);
        cb_wait_front(in1_cb_id, in1_block_num_tiles /* */);
        int in0_index_subblock_offset = 0;
        for (uint32_t in0_subblock = 0; in0_subblock < in0_num_subblocks /* */; in0_subblock++) {
            int in1_index_subblock_offset = 0;
            for (uint32_t in1_subblock = 0; in1_subblock < in1_num_subblocks /* */; in1_subblock++) {
                tile_regs_acquire();
                if (enable_reload) {
                    copy_tile_to_dst_init_short();
                    cb_wait_front(mm_partials_cb_id, out_subblock_num_tiles);
                    // TODO: Copy tiles using `copy_tile` with a loop
                    for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                        copy_tile(mm_partials_cb_id, i, i);
                    }
                    copy_tile(mm_partials_cb_id, 0, 0);
                    cb_pop_front(mm_partials_cb_id, out_subblock_num_tiles);
                    mm_init_short();
                }

                // Compute output sub-block from in0_subblock x in1_subblock
                int dst_index = 0;
                int in0_index_h_offset = 0;
                for (uint32_t h = 0; h < out_subblock_h /* */; h++) {
                    for (uint32_t w = 0; w < out_subblock_w; w++) {
                        int in1_index_inner_dim_offset = 0;
                        for (uint32_t inner_dim = 0; inner_dim < in0_block_w; inner_dim++) {
                            // int in0_index = in0_index_subblock_offset + in0_index_h_offset + inner_dim;
                            // int in1_index = in1_index_subblock_offset + in1_index_inner_dim_offset + w;
                            int in0_index = in0_subblock_num_tiles * in0_subblock + inner_dim + in0_block_w * h;
                            int in1_index =
                                out_subblock_w * in1_num_subblocks * inner_dim + in1_subblock * out_subblock_w + w;
                            matmul_tiles(in0_cb_id, in1_cb_id, in0_index, in1_index, dst_index, false /* transpose */);
                            in1_index_inner_dim_offset += in1_per_core_w;
                        }
                        dst_index++;
                    }
                    in0_index_h_offset += in0_block_w /* */;
                }

                tile_regs_commit();

                tile_regs_wait();
                if (last_out) {
                    // Pack out to output buffer
                    cb_reserve_back(out_cb_id, out_subblock_num_tiles);
                    // TODO: Pack tiles using `pack_tile` with a loop
                    // CHECK
                    for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                        pack_tile(i, out_cb_id, i);
                    }
                    cb_push_back(out_cb_id, out_subblock_num_tiles);
                } else {
                    // Wait for tiles in output buffer to be written out since interm and output share memory
                    if (block == 0) {
                        cb_reserve_back(out_cb_id, out_num_tiles_to_wait);
                        out_num_tiles_to_wait += out_subblock_num_tiles;
                    }
                    // Move partial result to interm buffer
                    cb_reserve_back(mm_partials_cb_id, out_subblock_num_tiles);
                    // TODO: Pack tiles using `pack_tile` with a loop
                    // CHECK
                    for (uint32_t i = 0; i < out_subblock_num_tiles; i++) {
                        pack_tile(i, mm_partials_cb_id, i);
                    }
                    cb_push_back(mm_partials_cb_id, out_subblock_num_tiles);
                }

                tile_regs_release();
                in1_index_subblock_offset += out_subblock_w /* */;
            }
            in0_index_subblock_offset += in0_subblock_num_tiles /* */;
        }

        if (spill)
            enable_reload = true;

        cb_pop_front(in0_cb_id, in0_block_num_tiles);
        cb_pop_front(in1_cb_id, in1_block_num_tiles);
    }
}
}  // namespace NAMESPACE
