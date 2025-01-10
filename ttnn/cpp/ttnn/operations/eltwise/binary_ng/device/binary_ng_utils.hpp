// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "binary_ng_device_operation.hpp"
#include "ttnn/operations/eltwise/binary_ng/types.hpp"
#include "ttnn/tensor/types.hpp"

#include <optional>
#include <string>

namespace ttnn::operations::binary_ng {

enum class KernelName {
    ReaderNoBcast,
    ReaderRowBcast,
    ReaderColBcast,
    ReaderScalarBcast,
    WriterNoBcast,
    WriterRowBcast,
    WriterColBcast,
    WriterScalarBcast,
    WriterScalar,
    ComputeNoBcast,
    ComputeBcast,
    ComputeScalar
};

struct BinaryNgKernelConfig {
    BinaryNgKernelConfig(SubtileBroadcastType subtile_broadcast_type);

    std::string bcast_input_str() const;

    KernelName reader_kernel;
    KernelName compute_kernel;
    KernelName writer_kernel;
    std::optional<uint32_t> bcast_input;
};

std::string get_kernel_file_path(KernelName kernel_name, bool is_sfpu);

struct OpConfig {
    enum class FpuBinaryOp { ADD, SUB, MUL };
    enum class SfpuBinaryOp {
        ADD,
        SUB,
        MUL,
        DIV,
        POWER,
        RSUB,
        LEFT_SHIFT,
        RIGHT_SHIFT,
        BITWISE_AND,
        BITWISE_OR,
        BITWISE_XOR
    };

    OpConfig(BinaryOpType binary_op_type, bool is_sfpu);

    std::map<std::string, std::string> as_defines() const;
    std::pair<std::string, std::string> get_sfpu_init_fn(DataType dtype) const;
    std::map<std::string, std::string> as_sfpu_defines(DataType dtype) const;

    std::optional<unary::UnaryOpType> process_lhs{};
    std::optional<unary::UnaryOpType> process_rhs{};
    std::optional<unary::UnaryOpType> postprocess{};
    FpuBinaryOp fpu_binary_op;
    SfpuBinaryOp sfpu_binary_op;
};

void add_activation_defines(
    std::map<std::string, std::string>& defines,
    tt::stl::Span<const unary::UnaryOpType> activations,
    std::string_view operand);

struct Lowercase {
    std::string_view view;
};

}  // namespace ttnn::operations::binary_ng
