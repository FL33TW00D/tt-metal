// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "binary_ng_device_operation.hpp"

using namespace tt::tt_metal;

namespace ttnn::operations::binary_ng {

namespace utils {
bool is_binary_sfpu_op(BinaryOpType val, DataType a, DataType b) {
    switch (val) {
        case BinaryOpType::ADD:
            return (
                (a == DataType::FLOAT32 && b == DataType::FLOAT32) || (a == DataType::INT32 && b == DataType::INT32));
        case BinaryOpType::SUB:
        case BinaryOpType::MUL:
        case BinaryOpType::DIV:
        case BinaryOpType::RSUB:
        case BinaryOpType::LOGADDEXP:
        case BinaryOpType::LOGADDEXP2:
        case BinaryOpType::LDEXP:
        case BinaryOpType::SQUARED_DIFFERENCE:
        case BinaryOpType::LOGICAL_OR:
        case BinaryOpType::LOGICAL_XOR:
        case BinaryOpType::LOGICAL_AND:
        case BinaryOpType::BIAS_GELU:
        case BinaryOpType::GT:
        case BinaryOpType::LT:
        case BinaryOpType::GTE:
        case BinaryOpType::LTE:
        case BinaryOpType::EQ:
        case BinaryOpType::NE: return (a == DataType::FLOAT32 && b == DataType::FLOAT32);
        case BinaryOpType::LEFT_SHIFT:
        case BinaryOpType::RIGHT_SHIFT:
        case BinaryOpType::BITWISE_XOR:
        case BinaryOpType::BITWISE_AND:
        case BinaryOpType::BITWISE_OR: return (a == DataType::INT32 && b == DataType::INT32);
        case BinaryOpType::POWER: return true;
        default: return false;
    }
    return false;
}
}  // namespace utils

SubtileBroadcastType get_subtile_broadcast_type(uint32_t a_h, uint32_t a_w, uint32_t b_h, uint32_t b_w) {
    if (a_h == b_h && a_w == b_w) {
        return SubtileBroadcastType::NONE;
    }
    if (a_h == 1 && a_w == 1) {
        return SubtileBroadcastType::SCALAR_A;
    }
    if (b_h == 1 && b_w == 1) {
        return SubtileBroadcastType::SCALAR_B;
    }
    if (a_h == 1 /* && a_w != 1 */ && b_w == 1 /* && b_h != 1 */) {
        return SubtileBroadcastType::ROW_A_COL_B;
    }
    if (a_w == 1 /* && a_h != 1 */ && b_h == 1 /* && b_w != 1 */) {
        return SubtileBroadcastType::ROW_B_COL_A;
    }
    if (a_h == 1) {
        return SubtileBroadcastType::ROW_A;
    }
    if (a_w == 1) {
        return SubtileBroadcastType::COL_A;
    }
    if (b_h == 1) {
        return SubtileBroadcastType::ROW_B;
    }
    if (b_w == 1) {
        return SubtileBroadcastType::COL_B;
    }

    TT_THROW("Invalid subtile broadcast type");
}

tt::stl::hash::hash_t BinaryNgDeviceOperation::operation_attributes_t::to_hash() const {
    return tt::stl::hash::hash_objects_with_default_seed(
        binary_op_type,
        lhs_activations,
        rhs_activations,
        post_activations,
        memory_config,
        get_dtype(),
        compute_kernel_config,
        subtile_broadcast_type,
        is_sfpu);
    // should is_sfpu attribute be a part of this hash fn ?
}

DataType BinaryNgDeviceOperation::operation_attributes_t::get_dtype() const {
    return this->dtype.value_or(this->input_dtype);
}

void BinaryNgDeviceOperation::validate_on_program_cache_miss(
    const operation_attributes_t& attributes, const tensor_args_t& tensor_args) {
    // We don't support sharding for now
    const auto& input_tensor_a = tensor_args.input_tensor_a;
    const auto& input_tensor_b = tensor_args.input_tensor_b;
    const auto& output_tensor = tensor_args.output_tensor;

    TT_FATAL(
        input_tensor_b.has_value() != attributes.scalar.has_value(), "Either the tensor b or scalar should be set");

    BinaryNgDeviceOperation::validate_on_program_cache_hit(attributes, tensor_args);

    TT_FATAL(input_tensor_a.get_layout() == Layout::TILE, "First operand to eltwise binary must be tilized");
    TT_FATAL(
        input_tensor_a.memory_config().memory_layout == TensorMemoryLayout::INTERLEAVED,
        "First operand to eltwise binary must be interleaved");
    TT_FATAL(
        attributes.memory_config.memory_layout == TensorMemoryLayout::INTERLEAVED,
        "Output tensor to eltwise binary must be interleaved");

    if (input_tensor_b.has_value()) {
        TT_FATAL(input_tensor_b->get_layout() == Layout::TILE, "Second operand to eltwise binary must be tilized");
        TT_FATAL(
            input_tensor_b->memory_config().memory_layout == TensorMemoryLayout::INTERLEAVED,
            "Second operand to eltwise binary must be interleaved");
    }

    if (attributes.dtype.has_value() && output_tensor.has_value()) {
        TT_FATAL(
            *attributes.dtype == output_tensor->get_dtype(),
            "If both output dtype and output tensor provided dtype should match");
    }
}

void BinaryNgDeviceOperation::validate_on_program_cache_hit(
    const operation_attributes_t& attributes, const tensor_args_t& tensor_args) {
    const auto& input_tensor_a = tensor_args.input_tensor_a;
    const auto& output_tensor = tensor_args.output_tensor;

    const auto& input_shape_a = input_tensor_a.get_logical_shape();
    const auto input_shape_b =
        tensor_args.input_tensor_b.has_value() ? tensor_args.input_tensor_b->get_logical_shape() : ttnn::Shape{1, 1};

    const int rank_a = input_shape_a.rank();
    const int rank_b = input_shape_b.rank();
    const int larger_rank = std::max(rank_a, rank_b);
    for (int i = -1; i >= -larger_rank; --i) {
        auto a_dim = (i >= -rank_a) ? input_shape_a[i] : 1;
        auto b_dim = (i >= -rank_b) ? input_shape_b[i] : 1;
        TT_FATAL(
            a_dim == b_dim || a_dim == 1 || b_dim == 1,
            "Broadcasting rule violation for rank {}, dim a: {}, dim b: {}",
            i,
            a_dim,
            b_dim);
    }
}

BinaryNgDeviceOperation::spec_return_value_t BinaryNgDeviceOperation::compute_output_specs(
    const operation_attributes_t& attributes, const tensor_args_t& tensor_args) {
    const auto& output_tensor = tensor_args.output_tensor;
    if (output_tensor.has_value()) {
        return output_tensor->get_tensor_spec();
    }

    const auto& input_tensor_a = tensor_args.input_tensor_a;
    const auto input_shape_a = input_tensor_a.logical_shape();
    const auto& tensor_b = tensor_args.input_tensor_b;
    const auto input_shape_b = tensor_b.has_value() ? tensor_b->logical_shape() : ttnn::SimpleShape{};

    const int rank_a = input_shape_a.rank();
    const int rank_b = input_shape_b.rank();
    const int larger_rank = std::max(rank_a, rank_b);

    // Broadcasting Rules Overview:
    // - If the two tensors have different ranks, we virtually pad the smaller-rank tensor's shape
    //   with ones on the left (i.e., higher-order dimensions) until both shapes have the same length.
    // - For each dimension (starting from the rightmost), the sizes are compatible if:
    //     - They are equal, or
    //     - One of them is 1 (the dimension can be broadcast to match the other size).
    auto compute_broadcasted_output = [rank_a, rank_b, larger_rank](const auto& shape_a, const auto& shape_b) {
        SmallVector<uint32_t> output_shape(larger_rank, 1);
        for (int i = -1; i >= -larger_rank; --i) {
            auto dim_a = (i >= -rank_a) ? shape_a[i] : 1;
            auto dim_b = (i >= -rank_b) ? shape_b[i] : 1;
            if (dim_a != 1 && dim_b != 1) {
                output_shape[i + larger_rank] = dim_a;
            } else {
                output_shape[i + larger_rank] = dim_a + dim_b - 1;
            }
        }
        return ttnn::SimpleShape(output_shape);
    };

    auto output_shape = compute_broadcasted_output(input_shape_a, input_shape_b);
    return TensorSpec(
        output_shape, TensorLayout(attributes.get_dtype(), PageConfig(Layout::TILE), attributes.memory_config));
}

BinaryNgDeviceOperation::program_factory_t BinaryNgDeviceOperation::select_program_factory(
    const operation_attributes_t&, const tensor_args_t&) {
    return ProgramFactory{};
}

BinaryNgDeviceOperation::tensor_return_value_t BinaryNgDeviceOperation::create_output_tensors(
    const operation_attributes_t& operation_attributes, const tensor_args_t& tensor_args) {
    const auto& output_tensor = tensor_args.output_tensor;
    if (output_tensor.has_value()) {
        return output_tensor.value();
    }

    return create_device_tensor(
        compute_output_specs(operation_attributes, tensor_args), tensor_args.input_tensor_a.device());
}

tt::stl::hash::hash_t BinaryNgDeviceOperation::compute_program_hash(
    const operation_attributes_t& attributes, const tensor_args_t& tensor_args) {
    const auto& input_tensor_a = tensor_args.input_tensor_a;
    const auto& input_tensor_b = tensor_args.input_tensor_b;

    TT_ASSERT(
        std::holds_alternative<DeviceStorage>(input_tensor_a.get_storage()),
        "Unexpected type {}",
        tt::stl::get_active_type_name_in_variant(input_tensor_a.get_storage()));

    if (input_tensor_b.has_value()) {
        TT_ASSERT(
            std::holds_alternative<DeviceStorage>(input_tensor_b->get_storage()),
            "Unexpected type {}",
            tt::stl::get_active_type_name_in_variant(input_tensor_b->get_storage()));

        return operation::hash_operation<BinaryNgDeviceOperation>(
            attributes,
            input_tensor_a.dtype(),
            std::get<DeviceStorage>(input_tensor_a.storage()).memory_config(),
            input_tensor_b->dtype(),
            std::get<DeviceStorage>(input_tensor_b->storage()).memory_config());
    }

    return operation::hash_operation<BinaryNgDeviceOperation>(
        attributes, input_tensor_a.dtype(), std::get<DeviceStorage>(input_tensor_a.storage()).memory_config());
}

std::tuple<BinaryNgDeviceOperation::operation_attributes_t, BinaryNgDeviceOperation::tensor_args_t>
BinaryNgDeviceOperation::invoke(
    const Tensor& input_tensor_a_arg,
    const Tensor& input_tensor_b_arg,
    BinaryOpType binary_op_type,
    const std::optional<const DataType>& output_dtype,
    const std::optional<MemoryConfig>& memory_config,
    std::optional<Tensor> optional_output_tensor,
    tt::stl::Span<const ttnn::operations::unary::UnaryOpType> lhs_activations,
    tt::stl::Span<const ttnn::operations::unary::UnaryOpType> rhs_activations,
    tt::stl::Span<const ttnn::operations::unary::UnaryOpType> post_activations) {
    auto subtile_broadcast_type = get_subtile_broadcast_type(
        input_tensor_a_arg.get_logical_shape()[-2],
        input_tensor_a_arg.get_logical_shape()[-1],
        input_tensor_b_arg.get_logical_shape()[-2],
        input_tensor_b_arg.get_logical_shape()[-1]);

    DataType dtype1 = input_tensor_a_arg.get_dtype();
    DataType dtype2 = input_tensor_a_arg.get_dtype();
    bool device_check = input_tensor_a_arg.device()->arch() != tt::ARCH::GRAYSKULL;
    bool is_sfpu_op = (utils::is_binary_sfpu_op(binary_op_type, dtype1, dtype2) && device_check);
    std::cout << "is sfpu device op? " << is_sfpu_op << std::endl;

    return {
        operation_attributes_t{
            binary_op_type,
            {lhs_activations.begin(), lhs_activations.end()},
            {rhs_activations.begin(), rhs_activations.end()},
            {post_activations.begin(), post_activations.end()},
            std::nullopt,
            memory_config.value_or(input_tensor_a_arg.memory_config()),
            input_tensor_a_arg.get_dtype(),
            output_dtype,
            std::nullopt,
            subtile_broadcast_type,
            is_sfpu_op},
        tensor_args_t{input_tensor_a_arg, input_tensor_b_arg, std::move(optional_output_tensor)}};
}

std::tuple<BinaryNgDeviceOperation::operation_attributes_t, BinaryNgDeviceOperation::tensor_args_t>
BinaryNgDeviceOperation::invoke(
    const Tensor& input_tensor_a_arg,
    float scalar,
    BinaryOpType binary_op_type,
    const std::optional<const DataType>& output_dtype,
    const std::optional<MemoryConfig>& memory_config,
    std::optional<Tensor> optional_output_tensor,
    tt::stl::Span<const unary::UnaryOpType> lhs_activations,
    tt::stl::Span<const unary::UnaryOpType> rhs_activations,
    tt::stl::Span<const unary::UnaryOpType> post_activations) {
    DataType dtype1 = input_tensor_a_arg.get_dtype();
    bool device_check = input_tensor_a_arg.device()->arch() != tt::ARCH::GRAYSKULL;
    bool is_sfpu_op = (utils::is_binary_sfpu_op(binary_op_type, dtype1, dtype1) && device_check);
    return {
        operation_attributes_t{
            binary_op_type,
            {lhs_activations.begin(), lhs_activations.end()},
            {rhs_activations.begin(), rhs_activations.end()},
            {post_activations.begin(), post_activations.end()},
            scalar,
            memory_config.value_or(input_tensor_a_arg.memory_config()),
            input_tensor_a_arg.get_dtype(),
            output_dtype,
            std::nullopt,
            SubtileBroadcastType::NONE,
            is_sfpu_op},
        tensor_args_t{input_tensor_a_arg, std::nullopt, std::move(optional_output_tensor)}};
}

}  // namespace ttnn::operations::binary_ng
