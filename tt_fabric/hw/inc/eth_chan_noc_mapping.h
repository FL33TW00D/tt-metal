// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// TODO: WH only, need to move this to generated code path for BH support
uint16_t eth_chan_to_noc_xy[2][16] __attribute__((used)) = {
    {
        // noc=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 25) << NOC_COORD_REG_OFFSET),  // NOC_X=9 NOC_Y=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 18) << NOC_COORD_REG_OFFSET),  // NOC_X=1 NOC_Y=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 24) << NOC_COORD_REG_OFFSET),  // NOC_X=8 NOC_Y=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 19) << NOC_COORD_REG_OFFSET),  // NOC_X=2 NOC_Y=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 23) << NOC_COORD_REG_OFFSET),  // NOC_X=7 NOC_Y=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 20) << NOC_COORD_REG_OFFSET),  // NOC_X=3 NOC_Y=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 22) << NOC_COORD_REG_OFFSET),  // NOC_X=6 NOC_Y=0
        (((16 << NOC_ADDR_NODE_ID_BITS) | 21) << NOC_COORD_REG_OFFSET),  // NOC_X=4 NOC_Y=0
        (((17 << NOC_ADDR_NODE_ID_BITS) | 25) << NOC_COORD_REG_OFFSET),  // NOC_X=9 NOC_Y=6
        (((17 << NOC_ADDR_NODE_ID_BITS) | 18) << NOC_COORD_REG_OFFSET),  // NOC_X=1 NOC_Y=6
        (((17 << NOC_ADDR_NODE_ID_BITS) | 24) << NOC_COORD_REG_OFFSET),  // NOC_X=8 NOC_Y=6
        (((17 << NOC_ADDR_NODE_ID_BITS) | 19) << NOC_COORD_REG_OFFSET),  // NOC_X=2 NOC_Y=6
        (((17 << NOC_ADDR_NODE_ID_BITS) | 23) << NOC_COORD_REG_OFFSET),  // NOC_X=7 NOC_Y=6
        (((17 << NOC_ADDR_NODE_ID_BITS) | 20) << NOC_COORD_REG_OFFSET),  // NOC_X=3 NOC_Y=6
        (((17 << NOC_ADDR_NODE_ID_BITS) | 22) << NOC_COORD_REG_OFFSET),  // NOC_X=6 NOC_Y=6
        (((17 << NOC_ADDR_NODE_ID_BITS) | 21) << NOC_COORD_REG_OFFSET),  // NOC_X=4 NOC_Y=6
    },
    {
        // noc=1
        (((11 << NOC_ADDR_NODE_ID_BITS) | 0) << NOC_COORD_REG_OFFSET),  // NOC_X=0 NOC_Y=11
        (((11 << NOC_ADDR_NODE_ID_BITS) | 8) << NOC_COORD_REG_OFFSET),  // NOC_X=8 NOC_Y=11
        (((11 << NOC_ADDR_NODE_ID_BITS) | 1) << NOC_COORD_REG_OFFSET),  // NOC_X=1 NOC_Y=11
        (((11 << NOC_ADDR_NODE_ID_BITS) | 7) << NOC_COORD_REG_OFFSET),  // NOC_X=7 NOC_Y=11
        (((11 << NOC_ADDR_NODE_ID_BITS) | 2) << NOC_COORD_REG_OFFSET),  // NOC_X=2 NOC_Y=11
        (((11 << NOC_ADDR_NODE_ID_BITS) | 6) << NOC_COORD_REG_OFFSET),  // NOC_X=6 NOC_Y=11
        (((11 << NOC_ADDR_NODE_ID_BITS) | 3) << NOC_COORD_REG_OFFSET),  // NOC_X=3 NOC_Y=11
        (((11 << NOC_ADDR_NODE_ID_BITS) | 5) << NOC_COORD_REG_OFFSET),  // NOC_X=5 NOC_Y=11
        (((5 << NOC_ADDR_NODE_ID_BITS) | 0) << NOC_COORD_REG_OFFSET),   // NOC_X=0 NOC_Y=5
        (((5 << NOC_ADDR_NODE_ID_BITS) | 8) << NOC_COORD_REG_OFFSET),   // NOC_X=8 NOC_Y=5
        (((5 << NOC_ADDR_NODE_ID_BITS) | 1) << NOC_COORD_REG_OFFSET),   // NOC_X=1 NOC_Y=5
        (((5 << NOC_ADDR_NODE_ID_BITS) | 7) << NOC_COORD_REG_OFFSET),   // NOC_X=7 NOC_Y=5
        (((5 << NOC_ADDR_NODE_ID_BITS) | 2) << NOC_COORD_REG_OFFSET),   // NOC_X=2 NOC_Y=5
        (((5 << NOC_ADDR_NODE_ID_BITS) | 6) << NOC_COORD_REG_OFFSET),   // NOC_X=6 NOC_Y=5
        (((5 << NOC_ADDR_NODE_ID_BITS) | 3) << NOC_COORD_REG_OFFSET),   // NOC_X=3 NOC_Y=5
        (((5 << NOC_ADDR_NODE_ID_BITS) | 5) << NOC_COORD_REG_OFFSET),   // NOC_X=5 NOC_Y=5
    },
};
