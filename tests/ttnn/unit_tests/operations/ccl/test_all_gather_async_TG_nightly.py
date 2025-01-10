# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import torch
import pytest
from loguru import logger
import ttnn
from tests.tt_eager.python_api_testing.sweep_tests.comparison_funcs import comp_equal, comp_pcc
from models.utility_functions import skip_for_grayskull
from tests.ttnn.unit_tests.operations.ccl.test_ccl_common import (
    create_and_load_sub_device_manager_with_fabric_interface,
    teardown_fabric_interface,
    create_global_semaphore_with_same_address,
)

from tests.ttnn.unit_tests.operations.ccl.test_all_gather_TG_post_commit import (
    run_line_all_gather_on_TG_with_mesh_tensor_along_rows,
)

from tests.ttnn.unit_tests.operations.ccl.test_new_all_gather import (
    run_all_gather_impl,
)


# Enumerate the post-commit cases explicitly
@skip_for_grayskull("Requires eth connected devices to run")
@pytest.mark.parametrize(
    "num_devices, num_links, output_shape, dim, layout",
    [
        (4, 1, [1, 1, 64, 512], 3, ttnn.TILE_LAYOUT),
        # (4, 1, [1, 1, 32, 32768], 3, ttnn.TILE_LAYOUT),
        # (4, 1, [1, 1, 2048, 16384], 3, ttnn.TILE_LAYOUT),
    ],
)
@pytest.mark.parametrize(
    "input_dtype",
    [
        ttnn.bfloat16,
    ],
)
@pytest.mark.parametrize(
    "mem_config",
    [
        ttnn.MemoryConfig(buffer_type=ttnn.BufferType.DRAM),
    ],
)
@pytest.mark.parametrize("num_iters", [8])
@pytest.mark.parametrize("enable_async", [True])
def test_all_gather(
    t3k_mesh_device,
    # pcie_mesh_device,
    num_devices,
    output_shape,
    dim,
    num_links,
    input_dtype,
    layout,
    mem_config,
    num_iters,
    use_program_cache,
    function_level_defaults,
    enable_async,
):
    run_all_gather_impl(
        t3k_mesh_device,
        num_devices,
        output_shape,
        dim,
        num_links,
        input_dtype,
        layout,
        use_program_cache,
        function_level_defaults,
        all_gather_topology=ttnn.Topology.Ring,
        num_iters=num_iters,
        enable_async=enable_async,
        rand_tensor=True,
        mem_config=mem_config,
    )


# Enumerate the post-commit cases explicitly
@skip_for_grayskull("Requires eth connected devices to run")
@pytest.mark.parametrize(
    "num_devices, output_shape, dim, layout, input_shard_shape, shard_grid, tensor_mem_layout",
    [
        (
            2,
            [1, 1, 32, 256],
            3,
            ttnn.TILE_LAYOUT,
            (32, 32),
            ttnn.CoreRangeSet({ttnn.CoreRange(ttnn.CoreCoord(0, 0), ttnn.CoreCoord(0, 3))}),
            ttnn.TensorMemoryLayout.WIDTH_SHARDED,
        ),
        (
            2,
            [1, 1, 32, 256],
            3,
            ttnn.TILE_LAYOUT,
            (32, 64),
            ttnn.CoreRangeSet({ttnn.CoreRange(ttnn.CoreCoord(0, 0), ttnn.CoreCoord(0, 1))}),
            ttnn.TensorMemoryLayout.WIDTH_SHARDED,
        ),
        (
            2,
            [1, 1, 32, 256],
            3,
            ttnn.TILE_LAYOUT,
            (32, 128),
            ttnn.CoreRangeSet({ttnn.CoreRange(ttnn.CoreCoord(0, 0), ttnn.CoreCoord(0, 0))}),
            ttnn.TensorMemoryLayout.WIDTH_SHARDED,
        ),
        (
            2,
            [1, 1, 64, 256],
            2,
            ttnn.TILE_LAYOUT,
            (32, 128),
            ttnn.CoreRangeSet({ttnn.CoreRange(ttnn.CoreCoord(0, 0), ttnn.CoreCoord(0, 1))}),
            ttnn.TensorMemoryLayout.WIDTH_SHARDED,
        ),
        (
            2,
            [1, 4, 32, 256],
            3,
            ttnn.TILE_LAYOUT,
            (32, 128),
            ttnn.CoreRangeSet({ttnn.CoreRange(ttnn.CoreCoord(0, 0), ttnn.CoreCoord(0, 3))}),
            ttnn.TensorMemoryLayout.HEIGHT_SHARDED,
        ),
        (
            4,
            [1, 4, 32, 1280],
            3,
            ttnn.TILE_LAYOUT,
            (32, 128),
            ttnn.CoreRangeSet({ttnn.CoreRange(ttnn.CoreCoord(0, 0), ttnn.CoreCoord(1, 4))}),
            ttnn.TensorMemoryLayout.HEIGHT_SHARDED,
        ),
    ],
)
@pytest.mark.parametrize("num_links", [1])
@pytest.mark.parametrize(
    "input_dtype",
    [
        ttnn.bfloat16,
    ],
)
@pytest.mark.parametrize("num_iters", [8])
@pytest.mark.parametrize("enable_async", [True])
def test_all_gather_sharded(
    t3k_mesh_device,
    # pcie_mesh_device,
    num_devices,
    output_shape,
    dim,
    num_links,
    input_dtype,
    layout,
    num_iters,
    use_program_cache,
    function_level_defaults,
    enable_async,
    input_shard_shape,
    shard_grid,
    tensor_mem_layout,
):
    run_all_gather_impl(
        t3k_mesh_device,
        num_devices,
        output_shape,
        dim,
        num_links,
        input_dtype,
        layout,
        use_program_cache,
        function_level_defaults,
        all_gather_topology=ttnn.Topology.Ring,
        num_iters=num_iters,
        enable_async=enable_async,
        rand_tensor=True,
        input_shard_shape=input_shard_shape,
        shard_grid=shard_grid,
        tensor_mem_layout=tensor_mem_layout,
        create_persistent_fabric=True,
        teardown_persistent_fabric=True,
    )


@skip_for_grayskull("Requires eth connected devices to run")
@pytest.mark.parametrize(
    "num_devices, num_links, per_chip_output_shape, dim, layout",
    [
        (2, 1, [1, 2, 32, 1280], 1, ttnn.TILE_LAYOUT),
        (2, 1, [2, 1, 32, 1280], 0, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 2048], 1, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 2304], 1, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 4096], 1, ttnn.TILE_LAYOUT),
    ],
)
@pytest.mark.parametrize(
    "input_dtype",
    [
        ttnn.bfloat16,
    ],
)
@pytest.mark.parametrize(
    "buffer_type",
    [
        ttnn.BufferType.DRAM,
    ],
)
@pytest.mark.parametrize("enable_async", [True])
@pytest.mark.parametrize("replication_factor", [4])
def test_line_all_gather_async_on_T3K_cols_transient_fabric_post_commit(
    t3k_mesh_device,
    num_devices,
    per_chip_output_shape,
    dim,
    num_links,
    input_dtype,
    layout,
    buffer_type,
    use_program_cache,
    function_level_defaults,
    enable_async,
    replication_factor,
    num_iters=1,
):
    if len(t3k_mesh_device.get_devices()) < 8:
        pytest.skip("Not T3K!")
    run_line_all_gather_on_TG_with_mesh_tensor_along_rows(
        t3k_mesh_device,
        num_devices,
        per_chip_output_shape,
        ttnn.TensorMemoryLayout.INTERLEAVED,
        dim,
        num_links,
        input_dtype,
        layout,
        buffer_type,
        use_program_cache,
        function_level_defaults,
        enable_async=enable_async,
        num_iters=num_iters,
        num_all_gather_instances=replication_factor,
        cluster_axis=0,
        use_all_gather_async=True,
        enable_persistent_fabric=False,
        create_persistent_fabric=False,
        teardown_persistent_fabric=False,
    )


@skip_for_grayskull("Requires eth connected devices to run")
@pytest.mark.parametrize(
    "num_devices, num_links, per_chip_output_shape, dim, layout",
    [
        (2, 1, [1, 2, 32, 1280], 1, ttnn.TILE_LAYOUT),
        (2, 1, [2, 1, 32, 1280], 0, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 2048], 1, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 2304], 1, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 4096], 1, ttnn.TILE_LAYOUT),
    ],
)
@pytest.mark.parametrize(
    "input_dtype",
    [
        ttnn.bfloat16,
    ],
)
@pytest.mark.parametrize(
    "buffer_type",
    [
        ttnn.BufferType.DRAM,
    ],
)
@pytest.mark.parametrize("enable_async", [True])
@pytest.mark.parametrize("replication_factor", [4])
def test_line_all_gather_async_on_T3K_cols_persistent_fabric_post_commit(
    t3k_mesh_device,
    num_devices,
    per_chip_output_shape,
    dim,
    num_links,
    input_dtype,
    layout,
    buffer_type,
    use_program_cache,
    function_level_defaults,
    enable_async,
    replication_factor,
    num_iters=1,
):
    if len(t3k_mesh_device.get_devices()) < 8:
        pytest.skip("Not T3K!")
    run_line_all_gather_on_TG_with_mesh_tensor_along_rows(
        t3k_mesh_device,
        num_devices,
        per_chip_output_shape,
        ttnn.TensorMemoryLayout.INTERLEAVED,
        dim,
        num_links,
        input_dtype,
        layout,
        buffer_type,
        use_program_cache,
        function_level_defaults,
        enable_async=enable_async,
        num_iters=num_iters,
        num_all_gather_instances=replication_factor,
        cluster_axis=0,
        use_all_gather_async=True,
        enable_persistent_fabric=True,
        create_persistent_fabric=True,
        teardown_persistent_fabric=True,
    )


# Enumerate the post-commit cases explicitly
@skip_for_grayskull("Requires eth connected devices to run")
@pytest.mark.parametrize(
    "num_devices, num_links, per_chip_output_shape, dim, layout",
    [
        (4, 1, [4, 1, 32, 1280], 0, ttnn.TILE_LAYOUT),
        (4, 1, [1, 1, 32, 16384 * 4], 3, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 2304], 1, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 4096], 1, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 6656], 1, ttnn.TILE_LAYOUT),
    ],
)
@pytest.mark.parametrize(
    "input_dtype",
    [
        ttnn.bfloat16,
        ttnn.bfloat8_b,
    ],
)
@pytest.mark.parametrize(
    "buffer_type",
    [
        ttnn.BufferType.DRAM,
        ttnn.BufferType.L1,
    ],
)
@pytest.mark.parametrize("replication_factor", [2])
@pytest.mark.parametrize("enable_async", [True])
def test_line_all_gather_async_on_T3K_rows_transient_fabric_post_commit(
    t3k_mesh_device,
    num_devices,
    per_chip_output_shape,
    dim,
    num_links,
    input_dtype,
    layout,
    buffer_type,
    use_program_cache,
    function_level_defaults,
    enable_async,
    replication_factor,
    num_iters=1,
):
    if len(t3k_mesh_device.get_devices()) < 8:
        pytest.skip("Not T3K!")
    run_line_all_gather_on_TG_with_mesh_tensor_along_rows(
        t3k_mesh_device,
        num_devices,
        per_chip_output_shape,
        ttnn.TensorMemoryLayout.INTERLEAVED,
        dim,
        num_links,
        input_dtype,
        layout,
        buffer_type,
        use_program_cache,
        function_level_defaults,
        enable_async=enable_async,
        num_iters=num_iters,
        num_all_gather_instances=replication_factor,
        cluster_axis=1,
        use_all_gather_async=True,
        enable_persistent_fabric=False,
        create_persistent_fabric=False,
        teardown_persistent_fabric=False,
    )


# Enumerate the post-commit cases explicitly
@skip_for_grayskull("Requires eth connected devices to run")
@pytest.mark.parametrize(
    "num_devices, num_links, per_chip_output_shape, dim, layout",
    [
        (4, 1, [4, 1, 32, 1280], 0, ttnn.TILE_LAYOUT),
        (4, 1, [1, 1, 32, 16384 * 4], 3, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 2304], 1, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 4096], 1, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 6656], 1, ttnn.TILE_LAYOUT),
    ],
)
@pytest.mark.parametrize(
    "input_dtype",
    [
        ttnn.bfloat16,
        ttnn.bfloat8_b,
    ],
)
@pytest.mark.parametrize(
    "buffer_type",
    [
        ttnn.BufferType.DRAM,
        ttnn.BufferType.L1,
    ],
)
@pytest.mark.parametrize("replication_factor", [2])
@pytest.mark.parametrize("enable_async", [True])
def test_line_all_gather_async_on_T3K_rows_persistent_fabric_post_commit(
    t3k_mesh_device,
    num_devices,
    per_chip_output_shape,
    dim,
    num_links,
    input_dtype,
    layout,
    buffer_type,
    use_program_cache,
    function_level_defaults,
    enable_async,
    replication_factor,
    num_iters=1,
):
    if len(t3k_mesh_device.get_devices()) < 8:
        pytest.skip("Not T3K!")
    run_line_all_gather_on_TG_with_mesh_tensor_along_rows(
        t3k_mesh_device,
        num_devices,
        per_chip_output_shape,
        ttnn.TensorMemoryLayout.INTERLEAVED,
        dim,
        num_links,
        input_dtype,
        layout,
        buffer_type,
        use_program_cache,
        function_level_defaults,
        enable_async=enable_async,
        num_iters=num_iters,
        num_all_gather_instances=replication_factor,
        cluster_axis=1,
        use_all_gather_async=True,
        enable_persistent_fabric=True,
        create_persistent_fabric=True,
        teardown_persistent_fabric=True,
    )


@pytest.mark.skip(
    "persistent fabric test with cluster-axis API and multiple concurrent all-gather instances not enabled yet"
)
@skip_for_grayskull("Requires eth connected devices to run")
@pytest.mark.parametrize(
    "num_devices1, num_links1, per_chip_output_shape1, dim1, layout1",
    [
        (2, 1, [1, 2, 32, 1280], 1, ttnn.TILE_LAYOUT),
        (2, 1, [2, 1, 32, 1280], 0, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 2048], 1, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 2304], 1, ttnn.TILE_LAYOUT),
        (2, 1, [1, 2, 32, 4096], 1, ttnn.TILE_LAYOUT),
    ],
)
@pytest.mark.parametrize(
    "input_dtype",
    [
        ttnn.bfloat16,
    ],
)
@pytest.mark.parametrize(
    "buffer_type",
    [
        ttnn.BufferType.DRAM,
    ],
)
@pytest.mark.parametrize("replication_factor1", [4])
@pytest.mark.parametrize("enable_async", [True])
@pytest.mark.parametrize(
    "num_devices2, num_links2, per_chip_output_shape2, dim2, layout2",
    [
        (4, 1, [4, 1, 32, 1280], 0, ttnn.TILE_LAYOUT),
        (4, 1, [1, 1, 32, 16384 * 4], 3, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 2304], 1, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 4096], 1, ttnn.TILE_LAYOUT),
        (4, 1, [1, 4, 32, 6656], 1, ttnn.TILE_LAYOUT),
    ],
)
@pytest.mark.parametrize("replication_factor2", [2])
def test_line_all_gather_async_on_T3K_back_to_back_cols_and_rows_persistent_fabric_post_commit(
    t3k_mesh_device,
    num_devices1,
    per_chip_output_shape1,
    dim1,
    num_links1,
    layout1,
    num_devices2,
    per_chip_output_shape2,
    dim2,
    num_links2,
    input_dtype,
    layout2,
    buffer_type,
    use_program_cache,
    function_level_defaults,
    enable_async,
    replication_factor1,
    replication_factor2,
    num_iters=1,
):
    if len(t3k_mesh_device.get_devices()) < 8:
        pytest.skip("Not T3K!")
    run_line_all_gather_on_TG_with_mesh_tensor_along_rows(
        t3k_mesh_device,
        num_devices1,
        per_chip_output_shape1,
        ttnn.TensorMemoryLayout.INTERLEAVED,
        dim1,
        num_links1,
        input_dtype,
        layout1,
        buffer_type,
        use_program_cache,
        function_level_defaults,
        enable_async=enable_async,
        num_iters=num_iters,
        num_all_gather_instances=replication_factor1,
        cluster_axis=0,
        use_all_gather_async=True,
        enable_persistent_fabric=True,
        create_persistent_fabric=True,
        teardown_persistent_fabric=False,
    )

    run_line_all_gather_on_TG_with_mesh_tensor_along_rows(
        t3k_mesh_device,
        num_devices2,
        per_chip_output_shape2,
        ttnn.TensorMemoryLayout.INTERLEAVED,
        dim2,
        num_links2,
        input_dtype,
        layout2,
        buffer_type,
        use_program_cache,
        function_level_defaults,
        enable_async=enable_async,
        num_iters=num_iters,
        num_all_gather_instances=replication_factor2,
        cluster_axis=1,
        use_all_gather_async=True,
        enable_persistent_fabric=True,
        create_persistent_fabric=False,
        teardown_persistent_fabric=True,
    )
