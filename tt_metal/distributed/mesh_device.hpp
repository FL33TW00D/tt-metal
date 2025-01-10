// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "tt_metal/include/tt_metal/device.hpp"
#include "tt_metal/distributed/mesh_handle.hpp"

#include "tt_metal/distributed/mesh_config.hpp"
#include "tt_metal/distributed/mesh_device_view.hpp"
#include "tt_metal/impl/sub_device/sub_device_types.hpp"
#include "tt_metal/tt_stl/span.hpp"

namespace tt::tt_metal::distributed {

// fwd declarations
class MeshCommandQueue;
class MeshDeviceView;

class IMeshDevice {
public:
    virtual ~IMeshDevice() = default;
    virtual std::vector<IDevice*> get_devices(const std::optional<MeshType>& type = std::nullopt) const = 0;
    virtual IDevice* get_device_index(size_t logical_device_id) const = 0;
    virtual IDevice* get_device(chip_id_t physical_device_id) const = 0;
    virtual IDevice* get_device(size_t row_idx, size_t col_idx) const = 0;

    virtual const DeviceIds get_device_ids() const = 0;

    virtual size_t num_devices() const = 0;
    virtual size_t num_rows() const = 0;
    virtual size_t num_cols() const = 0;
    virtual MeshShape shape() const = 0;

    virtual void reshape(const MeshShape& new_shape) = 0;
    virtual const MeshDeviceView& get_view() const = 0;

    virtual std::string to_string() const = 0;
    virtual bool is_parent_mesh() const = 0;

    virtual std::vector<std::shared_ptr<MeshDevice>> get_submeshes() const = 0;

    virtual std::shared_ptr<MeshDevice> create_submesh(
        const MeshShape& submesh_shape,
        const MeshOffset& offset = MeshOffset{0, 0},
        MeshType type = MeshType::RowMajor) = 0;

    virtual std::vector<std::shared_ptr<MeshDevice>> create_submeshes(
        const MeshShape& submesh_shape, MeshType type = MeshType::RowMajor) = 0;
};
//
class MeshDevice : public IMeshDevice, public IDevice, public std::enable_shared_from_this<MeshDevice> {
private:
    std::shared_ptr<IMeshHandle> mesh_handle_;

    MeshDeviceID mesh_id_;
    MeshShape mesh_shape_;
    MeshType type_;
    std::unique_ptr<MeshDeviceView> view_;
    std::vector<std::shared_ptr<MeshDevice>>
        submeshes_;                          // Parent owns submeshes and is responsible for their destruction
    std::weak_ptr<MeshDevice> parent_mesh_;  // Submesh created with reference to parent mesh
    std::unique_ptr<MeshCommandQueue> mesh_command_queue_;

    void initialize();

    // This is a reference device used to query properties that are the same for all devices in the mesh.
    IDevice* reference_device() const;

public:
    MeshDevice(
        std::shared_ptr<IMeshHandle> mesh_handle,
        const MeshShape& mesh_shape,
        MeshType type,
        std::weak_ptr<MeshDevice> parent_mesh = {});
    ~MeshDevice() override;

    MeshDevice(const MeshDevice&) = delete;
    MeshDevice& operator=(const MeshDevice&) = delete;

    MeshDevice(MeshDevice&&) = delete;
    MeshDevice& operator=(MeshDevice&&) = delete;

    // IDevice interface implementation
    tt::ARCH arch() const override;
    MeshDeviceID id() const override;
    uint32_t build_key() const override;
    uint8_t num_hw_cqs() const override;
    bool is_initialized() const override;

    int num_dram_channels() const override;
    uint32_t l1_size_per_core() const override;
    uint32_t dram_size_per_channel() const override;

    CoreCoord grid_size() const override;
    CoreCoord logical_grid_size() const override;
    CoreCoord dram_grid_size() const override;
    CoreType core_type_from_virtual_core(const CoreCoord& virtual_coord) const override;
    CoreCoord virtual_noc_coordinate(uint8_t noc_index, CoreCoord coord) const override;
    CoreCoord virtual_noc0_coordinate(uint8_t noc_index, CoreCoord coord) const override;

    std::vector<CoreCoord> worker_cores_from_logical_cores(const std::vector<CoreCoord>&logical_cores) const override;
    std::vector<CoreCoord> ethernet_cores_from_logical_cores(const std::vector<CoreCoord> &logical_cores) const override;
    std::vector<CoreCoord> get_optimal_dram_bank_to_logical_worker_assignment() override;


    CoreCoord virtual_core_from_logical_core(const CoreCoord& logical_coord, const CoreType& core_type) const override;
    CoreCoord worker_core_from_logical_core(const CoreCoord& logical_core) const override;
    CoreCoord ethernet_core_from_logical_core(const CoreCoord& logical_core) const override;
    CoreCoord logical_core_from_ethernet_core(const CoreCoord& ethernet_core) const override;
    std::unordered_set<CoreCoord> get_active_ethernet_cores(bool skip_reserved_tunnel_cores=false) const override;
    std::unordered_set<CoreCoord> get_inactive_ethernet_cores() const override;
    bool is_active_ethernet_core(CoreCoord logical_core, bool skip_reserved_tunnel_cores=false) const override;
    std::tuple<chip_id_t, CoreCoord> get_connected_ethernet_core(CoreCoord eth_core) const override;
    std::vector<CoreCoord> get_ethernet_sockets(chip_id_t connected_chip_id) const override;
    bool is_inactive_ethernet_core(CoreCoord logical_core) const override;
    CoreCoord compute_with_storage_grid_size() const override;
    CoreRangeSet worker_cores(HalProgrammableCoreType core_type, SubDeviceId sub_device_id) const override;
    uint32_t num_worker_cores(HalProgrammableCoreType core_type, SubDeviceId sub_device_id) const override;
    const std::unique_ptr<Allocator>& get_initialized_allocator() const override;
    const std::unique_ptr<Allocator>& get_initialized_allocator(SubDeviceId sub_device_id) const override;
    DeviceAddr get_base_allocator_addr(const HalMemType& mem_type) const override;
    DeviceAddr get_base_allocator_addr(const HalMemType& mem_type, SubDeviceId sub_device_id) const override;
    uint32_t num_banks(const BufferType& buffer_type) const override;
    uint32_t num_banks(const BufferType& buffer_type, SubDeviceId sub_device_id) const override;
    uint32_t bank_size(const BufferType& buffer_type) const override;
    uint32_t bank_size(const BufferType& buffer_type, SubDeviceId sub_device_id) const override;
    uint32_t dram_channel_from_bank_id(uint32_t bank_id) const override;
    uint32_t dram_channel_from_bank_id(uint32_t bank_id, SubDeviceId sub_device_id) const override;
    CoreCoord logical_core_from_dram_channel(uint32_t dram_channel) const override;
    uint32_t dram_channel_from_logical_core(const CoreCoord& logical_core) const override;
    int32_t bank_offset(BufferType buffer_type, uint32_t bank_id) const override;
    int32_t bank_offset(BufferType buffer_type, uint32_t bank_id, SubDeviceId sub_device_id) const override;
    CoreCoord logical_core_from_bank_id(uint32_t bank_id) const override;
    CoreCoord logical_core_from_bank_id(uint32_t bank_id, SubDeviceId sub_device_id) const override;
    const std::vector<uint32_t>& bank_ids_from_dram_channel(uint32_t dram_channel) const override;
    const std::vector<uint32_t>& bank_ids_from_dram_channel(uint32_t dram_channel, SubDeviceId sub_device_id) const override;
    const std::vector<uint32_t>& bank_ids_from_logical_core(BufferType buffer_type, const CoreCoord& logical_core) const override;
    const std::vector<uint32_t>& bank_ids_from_logical_core(BufferType buffer_type, const CoreCoord& logical_core, SubDeviceId sub_device_id) const override;
    allocator::Statistics get_memory_allocation_statistics(const BufferType& buffer_type) const override;
    allocator::Statistics get_memory_allocation_statistics(const BufferType& buffer_type, SubDeviceId sub_device_id) const override;
    uint32_t get_allocator_alignment() const override;
    uint32_t get_allocator_alignment(SubDeviceId sub_device_id) const override;
    std::optional<DeviceAddr> lowest_occupied_compute_l1_address() const override;
    std::optional<DeviceAddr> lowest_occupied_compute_l1_address(tt::stl::Span<const SubDeviceId> sub_device_ids) const override;
    size_t get_l1_small_size() const override;
    size_t get_l1_small_size(SubDeviceId sub_device_id) const override;
    const std::unordered_set<Buffer*>& get_allocated_buffers() const override;
    const std::unordered_set<Buffer*>& get_allocated_buffers(SubDeviceId sub_device_id) const override;
    void deallocate_buffers() override;
    void deallocate_buffers(SubDeviceId sub_device_id) override;
    void dump_memory_blocks(const BufferType& buffer_type, std::ofstream& out) const override;
    void dump_memory_blocks(const BufferType& buffer_type, std::ofstream& out, SubDeviceId sub_device_id) const override;
    const std::set<CoreCoord>& ethernet_cores() const override;
    const std::set<CoreCoord>& storage_only_cores() const override;
    uint32_t get_noc_unicast_encoding(uint8_t noc_index, const CoreCoord& core) const override;
    uint32_t get_noc_multicast_encoding(uint8_t noc_index, const CoreRange& cores) const override;
    const JitBuildEnv& build_env() const override;
    const string build_firmware_target_path(uint32_t programmable_core, uint32_t processor_class, int i) const override;
    const string build_kernel_target_path(uint32_t programmable_core, uint32_t processor_class, int i, const string& kernel_name) const override;
    const JitBuildState& build_firmware_state(uint32_t programmable_core, uint32_t processor_class, int i) const override;
    const JitBuildState& build_kernel_state(uint32_t programmable_core, uint32_t processor_class, int i) const override;
    const JitBuildStateSubset build_kernel_states(uint32_t programmable_core, uint32_t processor_class) const override;
    SystemMemoryManager& sysmem_manager() override;
    HWCommandQueue& hw_command_queue(size_t cq_id = 0) override;
    CommandQueue& command_queue(size_t cq_id = 0) override;

    // Trace APIs
    void begin_trace(const uint8_t cq_id, const uint32_t tid) override;
    void end_trace(const uint8_t cq_id, const uint32_t tid) override;
    void replay_trace(const uint8_t cq_id, const uint32_t tid, const bool blocking) override;
    void release_trace(const uint32_t tid) override;
    std::shared_ptr<TraceBuffer> get_trace(uint32_t tid) override;
    uint32_t get_trace_buffers_size() const override;
    void set_trace_buffers_size(uint32_t size) override;

    bool using_slow_dispatch() const override;
    bool using_fast_dispatch() const override;

    // Initialization APIs
    bool initialize(const uint8_t num_hw_cqs, size_t l1_small_size, size_t trace_region_size, tt::stl::Span<const std::uint32_t> l1_bank_remap = {}, bool minimal = false) override;
    void build_firmware() override;
    void reset_cores() override;
    void initialize_and_launch_firmware() override;
    void init_command_queue_host() override;
    void init_command_queue_device() override;
    void initialize_synchronous_sw_cmd_queue() override;
    void update_dispatch_cores_for_multi_cq_eth_dispatch() override;
    bool close() override;
    void enable_async(bool enable) override;
    void synchronize() override;
    WorkExecutorMode get_worker_mode() override;
    void set_worker_queue_mode(const WorkerQueueMode& mode) override;
    WorkerQueueMode get_worker_queue_mode() override;
    bool is_worker_queue_empty() const override;
    bool can_use_passthrough_scheduling() const override;
    void push_work(std::function<void()> work, bool blocking) override;
    void enable_program_cache() override;
    void disable_and_clear_program_cache() override;
    program_cache::detail::ProgramCache& get_program_cache() override;
    std::size_t num_program_cache_entries() override;
    HalProgrammableCoreType get_programmable_core_type(CoreCoord virtual_core) const override;
    std::vector<std::pair<transfer_info_cores, uint32_t>> extract_dst_noc_multicast_info(const std::vector<CoreRange>& ranges, const CoreType core_type) override;
    bool dispatch_s_enabled() const override;
    bool distributed_dispatcher() const override;
    NOC dispatch_go_signal_noc() const override;
    size_t get_device_kernel_defines_hash() override;
    uint8_t num_noc_mcast_txns(SubDeviceId sub_device_id) const override;
    uint8_t num_noc_unicast_txns(SubDeviceId sub_device_id) const override;
    uint8_t noc_data_start_index(SubDeviceId sub_device_id, bool mcast_data=true, bool unicast_data=true) const override;
    SubDeviceManagerId get_active_sub_device_manager_id() const override;
    SubDeviceManagerId get_default_sub_device_manager_id() const override;
    SubDeviceManagerId create_sub_device_manager(tt::stl::Span<const SubDevice> sub_devices, DeviceAddr local_l1_size) override;
    void remove_sub_device_manager(SubDeviceManagerId sub_device_manager_id) override;
    void load_sub_device_manager(SubDeviceManagerId sub_device_manager_id) override;
    void clear_loaded_sub_device_manager() override;
    LaunchMessageRingBufferState& get_worker_launch_message_buffer_state(SubDeviceId sub_device_id) override;
    CoreCoord virtual_program_dispatch_core(uint8_t cq_id) const override;
    const std::vector<SubDeviceId>& get_sub_device_ids() const override;
    uint32_t num_sub_devices() const override;
    // TODO #16526: Temporary api until migration to actual fabric is complete
    std::tuple<SubDeviceManagerId, SubDeviceId> create_sub_device_manager_with_fabric(tt::stl::Span<const SubDevice> sub_devices, DeviceAddr local_l1_size) override;
    std::optional<SubDeviceId> get_fabric_sub_device_id() const override;
    uint32_t get_completion_queue_reader_core() const override;
    bool is_mmio_capable() const override;
    std::vector<std::vector<chip_id_t>> get_tunnels_from_mmio() const override;

    // A MeshDevice is a collection of devices arranged in a 2D grid.
    // The type parameter allows the caller to specify how to linearize the devices in the mesh.
    // If type is not provided, the default behavior is to return the devices based on the MeshType of the MeshDevice.

    std::vector<IDevice*> get_devices(const std::optional<MeshType>& type = std::nullopt) const override;
    IDevice* get_device_index(size_t logical_device_id) const override;
    IDevice* get_device(chip_id_t physical_device_id) const override;
    IDevice* get_device(size_t row_idx, size_t col_idx) const override;

    const DeviceIds get_device_ids() const override;

    size_t num_devices() const override;
    size_t num_rows() const override;
    size_t num_cols() const override;
    MeshShape shape() const override;

    // Reshapes the logical mesh and re-maps the physical devices to the new logical coordinates.
    // Reshaping Rules:
    // 1. The old_shape volume must equal the new_shape volume (i.e. number of devices must remain constant)
    // 2. Line-to-Line Reshaping (when either dimension is 1):
    //    - Always possible between 1xN and Nx1 shapes (e.g.: 1x8 <-> 8x1
    // 3. Grid-to-Grid Reshaping:
    //    - Only possible if the devices can form a connected physical mesh in the new shape
    //    - Must maintain physical connectivity between adjacent devices
    // 4. Line-to-Grid Reshaping:
    //    - Only possible if the physical devices can form a connected physical mesh in the new shape
    //    - Example: 1x8 -> 2x4 is possible only if physical mesh permits a 2x4 configuration
    //
    // @throws std::runtime_error if any of the following constraints are not met:
    // 1. The old_shape volume must equal the new_shape volume (i.e. number of devices must remain constant)
    // 2. For Grid-to-Grid or Line-to-Grid reshaping: physical connectivity must be possible with current devices
    void reshape(const MeshShape& new_shape) override;
    const MeshDeviceView& get_view() const override;

    std::string to_string() const override;
    bool is_parent_mesh() const override;

    std::vector<std::shared_ptr<MeshDevice>> get_submeshes() const override;

    std::shared_ptr<MeshDevice> create_submesh(
        const MeshShape& submesh_shape,
        const MeshOffset& offset = MeshOffset{0, 0},
        MeshType type = MeshType::RowMajor) override;

    std::vector<std::shared_ptr<MeshDevice>> create_submeshes(
        const MeshShape& submesh_shape, MeshType type = MeshType::RowMajor) override;

    MeshCommandQueue& mesh_command_queue();
    static std::shared_ptr<MeshDevice> create(
        const MeshDeviceConfig& config,
        size_t l1_small_size = DEFAULT_L1_SMALL_SIZE,
        size_t trace_region_size = DEFAULT_TRACE_REGION_SIZE,
        size_t num_command_queues = 1,
        const DispatchCoreConfig& dispatch_core_config = DispatchCoreConfig{});

    // TODO #16492: Add get_sub_device_stall_group once MeshDevice is no longer just a collection of single Devices
    // and the MeshDevice has a single SubDeviceManager responsible for all Devices.
    const std::vector<SubDeviceId>& get_sub_device_stall_group() const override;
    void set_sub_device_stall_group(tt::stl::Span<const SubDeviceId> sub_device_ids) override;
    void reset_sub_device_stall_group() override;
};

std::ostream& operator<<(std::ostream& os, const MeshDevice& mesh_device);

}  // namespace tt::tt_metal::distributed
