// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#include "compute_kernel_api/untilize.h"
#include "compute_kernel_api/pack_untilize.h"
#include "compute_kernel_api/eltwise_unary/eltwise_unary.h"
#include "debug/dprint.h"

namespace NAMESPACE {
void MAIN {

    constexpr uint32_t per_core_block_cnt = get_compile_time_arg_val(0);
    constexpr uint32_t per_core_block_tile_cnt = get_compile_time_arg_val(1);
#ifndef SHORT_INIT
    pack_untilize_init<per_core_block_tile_cnt>(tt::CB::c_in0, tt::CB::c_out0);
#else
    unary_op_init_common(tt::CB::c_in0, tt::CB::c_out0);
    pack_untilize_init_short<per_core_block_tile_cnt>(tt::CB::c_in0, tt::CB::c_out0);
#endif
dfasdfa
    //DPRINT << "IN PACK UNTILIZE " << ENDL();
    PACK(( DPRINT << "Block count=" << uint32_t(per_core_block_cnt) << " tile count=" << per_core_block_tile_cnt << ENDL() ));
    UNPACK(( DPRINT << "Block count=" << uint32_t(per_core_block_cnt) << " tile count=" << per_core_block_tile_cnt << ENDL() ));
    for(uint32_t b = 0; b < per_core_block_cnt; ++ b) {
        cb_wait_front(tt::CB::c_in0, per_core_block_tile_cnt);
        cb_reserve_back(tt::CB::c_out0, per_core_block_tile_cnt);

        pack_untilize_block<per_core_block_tile_cnt, 2>(tt::CB::c_in0, 1, tt::CB::c_out0);

        cb_push_back(tt::CB::c_out0, per_core_block_tile_cnt);
        cb_pop_front(tt::CB::c_in0, per_core_block_tile_cnt);
    }

    pack_untilize_uninit();
}
}
