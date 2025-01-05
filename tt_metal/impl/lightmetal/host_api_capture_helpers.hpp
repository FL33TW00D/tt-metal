// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include "lightmetal_capture.hpp"
#include "command_generated.h"
#include "tt_metal/common/logger.hpp"

// FIXME (kmabee) - Temp hack, remove before merge and integrate as cmake define.
#define ENABLE_TRACING 1

#ifdef ENABLE_TRACING
#define TRACE_FUNCTION_CALL(capture_func, ...)             \
    do {                                                   \
        if (LightMetalCaptureContext::Get().IsTracing()) { \
            capture_func(__VA_ARGS__);                     \
        }                                                  \
    } while (0)
#else
#define TRACE_FUNCTION_CALL(capture_func, ...) \
    do {                                       \
    } while (0)
#endif

// Generic helper to build command and add to vector of cmds (CQ)
inline void CaptureCommand(tt::target::CommandType cmd_type, ::flatbuffers::Offset<void> fb_offset) {
    auto& ctx = LightMetalCaptureContext::Get();
    ctx.GetCmdsVector().push_back(tt::target::CreateCommand(ctx.GetBuilder(), cmd_type, fb_offset));
}

inline void captureReplayTrace(Device* device, uint8_t cq_id, uint32_t tid, bool blocking) {
    auto& ctx = LightMetalCaptureContext::Get();
    log_debug(tt::LogMetalTrace, "{}: cq_id: {}, tid: {}, blocking: {}", __FUNCTION__, cq_id, tid, blocking);
    auto cmd = tt::target::CreateReplayTraceCommand(ctx.GetBuilder(), cq_id, tid, blocking);
    captureCommand(tt::target::CommandType::ReplayTraceCommand, cmd.Union());
}

inline void captureEnqueueTrace(CommandQueue& cq, uint32_t tid, bool blocking) {
    auto& ctx = LightMetalCaptureContext::Get();
    log_debug(tt::LogMetalTrace, "{}: cq_id: {}, tid: {}, blocking: {}", __FUNCTION__, cq.id(), tid, blocking);
    auto cmd = tt::target::CreateEnqueueTraceCommand(ctx.GetBuilder(), cq.id(), tid, blocking);
    captureCommand(tt::target::CommandType::EnqueueTraceCommand, cmd.Union());
}

inline void captureLoadTrace(Device* device, const uint8_t cq_id, const uint32_t tid) {
    auto& ctx = LightMetalCaptureContext::Get();
    log_debug(tt::LogMetalTrace, "{}: cq_id: {}, tid: {}", __FUNCTION__, cq_id, tid);
    auto cmd = tt::target::CreateLoadTraceCommand(ctx.GetBuilder(), tid, cq_id);
    captureCommand(tt::target::CommandType::LoadTraceCommand, cmd.Union());
}
