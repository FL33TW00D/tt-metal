// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0


#include "third_party/magic_enum/magic_enum.hpp"
#include "tt_eager/tt_numpy/functions.hpp"
#include "ttnn/cpp/ttnn/operations/eltwise/unary/unary.hpp"
#include "ttnn/cpp/ttnn/operations/eltwise/binary/binary.hpp"
#include "tt_eager/tt_dnn/op_library/composite/composite_ops.hpp"
#include "binary_composite_op.hpp"
#include "tt_eager/tt_dnn/op_library/run_operation.hpp"
#include "ttnn/cpp/ttnn/types.hpp"
#include "tt_metal/common/bfloat16.hpp"

namespace ttnn::operations::binary{


Tensor _hypot(const Tensor& input_a, const Tensor& input_b, const std::optional<MemoryConfig>& output_mem_config) {
    Tensor a_sq = ttnn::square(input_a, output_mem_config);
    Tensor b_sq = ttnn::square(input_b, output_mem_config);
    Tensor c_sq = ttnn::add(a_sq, b_sq, std::nullopt, output_mem_config);
    a_sq.deallocate();
    b_sq.deallocate();
    return ttnn::sqrt(c_sq, output_mem_config);
}

// xlogy(x,y)=x*log(y)
Tensor _xlogy(const Tensor& input_a, const Tensor& input_b, const std::optional<MemoryConfig>& output_mem_config) {
    Tensor t_nan = ttnn::full_like(input_b, std::nanf(" "));
    Tensor result = ttnn::multiply(input_a, ttnn::log(input_b, output_mem_config), std::nullopt, output_mem_config);
    result = where(
        ttnn::logical_or(
            ttnn::ltz(input_b, output_mem_config),
            ttnn::eq(input_b, t_nan, std::nullopt, output_mem_config),
            std::nullopt,
            output_mem_config),
        t_nan,
        result);
    return result;
}

// subalpha(input,other,alpha)=input-alpha*other
Tensor _subalpha(const Tensor& input_a, const Tensor& input_b, float alpha, const std::optional<MemoryConfig>& output_mem_config) {
    Tensor result = ttnn::add(
        ttnn::neg(ttnn::multiply(input_b, alpha, std::nullopt, output_mem_config), output_mem_config), input_a, std::nullopt, output_mem_config);
    return result;
}

// addalpha(input, other, alpha) = input + (alpha * other)
Tensor _addalpha(
    const Tensor& input_a,
    const Tensor& input_b,
    float alpha,
    const std::optional<MemoryConfig>& output_mem_config) {

    return ttnn::add(ttnn::multiply(input_b, alpha, std::nullopt, output_mem_config), input_a, std::nullopt, output_mem_config);
}


// nextafter
Tensor _nextafter(const Tensor& input_a, const Tensor& input_b, const std::optional<MemoryConfig>& output_mem_config) {
    const float eps = input_a.device()->sfpu_eps();
    Tensor result(input_a);
    {
        Tensor eps_gt(input_a);
        {
            eps_gt = where(
                ttnn::gt(input_a, input_b, std::nullopt, output_mem_config),
                ttnn::add(input_a, eps, std::nullopt, output_mem_config),
                input_a);
        }
        result = where(
            ttnn::lt(input_a, input_b, std::nullopt, output_mem_config),
            ttnn::subtract(input_a, eps, std::nullopt, output_mem_config),
            eps_gt);
    }
    return result;
}

// ∣input−other∣≤ atol+rtol×∣other∣
Tensor _isclose(
    const Tensor& input_a,
    const Tensor& input_b,
    float rtol,
    float atol,
    bool equal_nan,
     const std::optional<MemoryConfig>& output_mem_config) {
    Tensor value1 = input_a;
    Tensor value2 = input_b;
    if (!equal_nan) {
        // If equal_nan false, then two NaN will not be considered be equal
        // As below operation's computes the NaN and make it as false based on the formula.
        // Input 1 = 1, Input = 0 => 1 - 0 <= atol + rtol * |0|, hence comparison explicily false.
        value1 = where(ttnn::isnan(value1, output_mem_config), 1.0f, value1);
        value2 = where(ttnn::isnan(value2, output_mem_config), 0.0f, value2);
    }
    Tensor is_close_lhs = ttnn::abs(ttnn::subtract(value1, value2, std::nullopt, output_mem_config), output_mem_config);
    Tensor is_close_rhs(input_b);
    {
        Tensor mul_result = ttnn::multiply(ttnn::abs(value2, output_mem_config), rtol, std::nullopt, output_mem_config);
        is_close_rhs = ttnn::add(mul_result, atol, std::nullopt, output_mem_config);
    }
    return where(
        ttnn::le(is_close_lhs, is_close_rhs, std::nullopt, output_mem_config),
        ttnn::ones_like(value2),
        ttnn::zeros_like(value2));
}

// minimum(a,b) = a - (a - b > 0 )*(a-b)
Tensor _minimum(const Tensor& input_a, const Tensor& input_b, const std::optional<MemoryConfig>& output_mem_config) {
    Tensor t_diff = ttnn::subtract(input_a, input_b, std::nullopt, output_mem_config);
    Tensor result = where(t_diff, input_b, input_a);
    return result;
}

// maximum(a,b) = a + (b - a > 0 )*(b-a)
Tensor _maximum(const Tensor& input_a, const Tensor& input_b, const std::optional<MemoryConfig>& output_mem_config) {
    Tensor t_diff = ttnn::subtract(input_b, input_a, std::nullopt, output_mem_config);
    Tensor result = where(t_diff, input_b, input_a);
    return result;
}

Tensor _atan2(const Tensor& input_a, const Tensor& input_b, const std::optional<MemoryConfig>& output_mem_config) {
    Tensor result(input_a);
    {
        Tensor atan_input = ttnn::multiply(
            ttnn::abs(input_b, output_mem_config),
            ttnn::reciprocal(ttnn::abs(input_a, output_mem_config), output_mem_config),
            std::nullopt,
            output_mem_config);
        result = ttnn::atan(atan_input, output_mem_config);
    }
    Tensor res(result);
    {
        Tensor ib_gtz = ttnn::gtz(input_b, output_mem_config);
        Tensor ib_gt = ttnn::gtz(input_b, output_mem_config);
        Tensor ib_lt = ttnn::ltz(input_b, output_mem_config);
        float pi_2 = M_PI_2;
        Tensor neg_result = ttnn::neg(result, output_mem_config);

        res = where(
            ttnn::gtz(input_a, output_mem_config),
            where(ib_gtz, result, neg_result),
            where(
                ttnn::ltz(input_a, output_mem_config),
                where(
                    ib_gt,
                    ttnn::add(neg_result, M_PI, std::nullopt, output_mem_config),
                    where(ib_lt, ttnn::subtract(result, M_PI, std::nullopt, output_mem_config), M_PI)),
                where(ib_gt, pi_2, where(ib_lt, -pi_2, 0.0f))));
    }
    return res;
}

Tensor _logical_xor(const Tensor& input_a, const Tensor& input_b, const std::optional<MemoryConfig>& output_mem_config) {
    Tensor in_a_eq_zero = ttnn::eqz(input_a, output_mem_config);
    Tensor in_b_eq_zero = ttnn::eqz(input_b, output_mem_config);
    Tensor in_b_neq_zero = ttnn::nez(input_b, output_mem_config);
    Tensor result = where(in_a_eq_zero, in_b_neq_zero, in_b_eq_zero);
    return result;
}

} // namespace ttnn::operations::binary
