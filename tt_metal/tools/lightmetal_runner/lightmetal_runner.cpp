// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_metal/impl/lightmetal/lightmetal_replay.hpp"
#include "tt_metal/common/logger.hpp"
#include "tt_metal/common/assert.hpp"

using namespace tt;

int main(int argc, char* argv[]) {
    // Process cmdline arguments
    std::string program_filename = argv[0];
    TT_FATAL(argc == 2, "Invalid number of supplied arguments. Usage: {} <binary_file>", program_filename.c_str());
    std::string binary_filename = argv[1];

    // Read the Light Metal Binary file into blob, transfer ownership and execute it.
    std::vector<uint8_t> binary_blob;
    tt::tt_metal::ReadBinaryBlobFromFile(binary_filename, binary_blob);
    tt::tt_metal::LightMetalReplay lm_replay(std::move(binary_blob));

    if (!lm_replay.ExecuteLightMetalBinary()) {
        log_fatal("Light Metal Binary {} failed to execute or encountered errors.", binary_filename);
        return 1;
    } else {
        log_info(tt::LogMetalTrace, "Light Metal Binary {} executed successfully", binary_filename);
        return 0;
    }
}
