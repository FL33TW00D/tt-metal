// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_metal/common/logger.hpp"
#include "lightmetal_capture.hpp"
#include "flatbuffers/flatbuffers.h"
#include "command_generated.h"
#include "binary_generated.h"
#include "tt_metal/impl/trace/trace_buffer.hpp"

#include <iostream>
#include <fstream>

namespace tt::tt_metal {
inline namespace v0 {

LightMetalCaptureContext::LightMetalCaptureContext() : is_tracing_(false), builder_() {}

// Singleton instance accessor
LightMetalCaptureContext& LightMetalCaptureContext::Get() {
    static LightMetalCaptureContext instance;
    return instance;
}

bool LightMetalCaptureContext::IsTracing() const { return is_tracing_; }

void LightMetalCaptureContext::SetTracing(bool is_tracing) { is_tracing_ = is_tracing; }

flatbuffers::FlatBufferBuilder& LightMetalCaptureContext::GetBuilder() { return builder_; }

std::vector<flatbuffers::Offset<tt::target::Command>>& LightMetalCaptureContext::GetCmdsVector() { return cmds_vec_; }

void LightMetalCaptureContext::CaptureTraceDescriptor(const detail::TraceDescriptor& trace_desc, const uint32_t tid) {
    trace_descs_vec_.push_back(ToFlatbuffer(builder_, trace_desc, tid));
}

// Create final flatbuffer binary from the built up data and return to caller as blob.
// If light_metal_binary itself (flatbuffer object) is of interest, could return it instead.
std::vector<uint8_t> LightMetalCaptureContext::CreateLightMetalBinary() {
    auto cmds_vec_fb = builder_.CreateVector(cmds_vec_);
    auto sorted_trace_descs = builder_.CreateVectorOfSortedTables(&trace_descs_vec_);
    auto light_metal_binary = target::lightmetal::CreateLightMetalBinary(builder_, cmds_vec_fb, sorted_trace_descs);
    builder_.Finish(light_metal_binary);

    const uint8_t* buffer_ptr = builder_.GetBufferPointer();
    size_t buffer_size = builder_.GetSize();
    return {buffer_ptr, buffer_ptr + buffer_size};
}

void LightMetalCaptureContext::Reset() {
    builder_.Clear();
    cmds_vec_.clear();
}

////////////////////////////////////////////
// Non-Class Helper Functions             //
////////////////////////////////////////////

bool WriteBinaryBlobToFile(const std::string& filename, const std::vector<uint8_t>& blob) {
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        std::cerr << "Unable to open file: " << filename << " for writing." << std::endl;
        return false;
    }

    if (!outFile.write(reinterpret_cast<const char*>(blob.data()), blob.size())) {
        std::cerr << "Failed to write binary data to file: " << filename << std::endl;
        return false;
    }

    return true;
}

// Serialize tt-metal traceDescriptor and trace_id to flatbuffer format.
TraceDescriptorByTraceIdOffset ToFlatbuffer(
    flatbuffers::FlatBufferBuilder& builder, const detail::TraceDescriptor& trace_desc, const uint32_t trace_id) {
    // Serialize the trace_data vector
    auto trace_data_offset = builder.CreateVector(trace_desc.data);

    // Serialize the sub_device_descriptors (map)
    std::vector<flatbuffers::Offset<tt::target::lightmetal::SubDeviceDescriptorMapping>> sub_device_descriptor_offsets;
    for (const auto& [sub_device_id, descriptor] : trace_desc.descriptors) {
        auto descriptor_offset = tt::target::lightmetal::CreateDescriptor(
            builder,
            descriptor.num_completion_worker_cores,
            descriptor.num_traced_programs_needing_go_signal_multicast,
            descriptor.num_traced_programs_needing_go_signal_unicast);
        auto mapping_offset = tt::target::lightmetal::CreateSubDeviceDescriptorMapping(
            builder,
            sub_device_id.to_index(),  // No need for static_cast; directly use uint8_t
            descriptor_offset);
        sub_device_descriptor_offsets.push_back(mapping_offset);
    }
    auto sub_device_descriptors_offset = builder.CreateVector(sub_device_descriptor_offsets);

    // Serialize the sub_device_ids vector
    std::vector<uint8_t> sub_device_ids_converted;
    sub_device_ids_converted.reserve(trace_desc.sub_device_ids.size());
    for (const auto& sub_device_id : trace_desc.sub_device_ids) {
        sub_device_ids_converted.push_back(sub_device_id.to_index());
    }
    auto sub_device_ids_offset = builder.CreateVector(sub_device_ids_converted);

    // Create the TraceDescriptor
    auto trace_descriptor_offset = tt::target::lightmetal::CreateTraceDescriptor(
        builder, trace_data_offset, sub_device_descriptors_offset, sub_device_ids_offset);

    // Create the TraceDescriptorByTraceId
    return tt::target::lightmetal::CreateTraceDescriptorByTraceId(builder, trace_id, trace_descriptor_offset);
}

}  // namespace v0
}  // namespace tt::tt_metal
