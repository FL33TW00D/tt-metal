#pragma once

#include "ttnn/cpp/ttnn/operations/eltwise/common/eltwise_l1_interface_common.hpp"

class EltwiseOpL1Usage {
   public:
    EltwiseOpL1Usage(const EltwiseOpParams& input_a, const EltwiseOpParams& input_b, const EltwiseOpParams& output);
    virtual ~EltwiseOpL1Usage() = default;

    virtual std::vector<std::tuple<uint32_t, uint32_t>> get_circular_buffer_l1_allocations_per_core() const = 0;
    virtual std::vector<std::tuple<uint32_t, uint32_t>> get_tensor_l1_allocations_per_core() const = 0;

   protected:
    std::optional<EltwiseOpParams> calculate_repeat_buffer_impl(
        const EltwiseOpParams& input_a, const EltwiseOpParams& input_b);

    std::optional<ShardSpec> get_op_shard_spec() const;

    EltwiseOpParams input_a;
    EltwiseOpParams input_b;
    EltwiseOpParams output;
    std::optional<EltwiseOpParams> repeat;
};

class ElementWiseMultiCoreOpL1Usage : public EltwiseOpL1Usage {
   public:
    ElementWiseMultiCoreOpL1Usage(
        const EltwiseOpParams& input_a, const EltwiseOpParams& input_b, const std::optional<EltwiseOpParams>& output);
    virtual ~ElementWiseMultiCoreOpL1Usage() = default;

    virtual std::vector<std::tuple<uint32_t, uint32_t>> get_circular_buffer_l1_allocations_per_core() const override;
    virtual std::vector<std::tuple<uint32_t, uint32_t>> get_tensor_l1_allocations_per_core() const override;
};

class BroadcastWidthMultiCoreOpL1Usage : public EltwiseOpL1Usage {
   public:
    BroadcastWidthMultiCoreOpL1Usage(
        const EltwiseOpParams& input_a, const EltwiseOpParams& input_b, const std::optional<EltwiseOpParams>& output);
    virtual ~BroadcastWidthMultiCoreOpL1Usage() = default;

    virtual std::vector<std::tuple<uint32_t, uint32_t>> get_circular_buffer_l1_allocations_per_core() const override;
    virtual std::vector<std::tuple<uint32_t, uint32_t>> get_tensor_l1_allocations_per_core() const override;
};

class EltwiseOpL1UsageFactory {
   public:
    EltwiseOpL1UsageFactory() = delete;
    static std::unique_ptr<EltwiseOpL1Usage> Make(
        const EltwiseOpParams& input_a,
        const EltwiseOpParams& input_b,
        const std::optional<EltwiseOpParams>& output = std::nullopt);
};
