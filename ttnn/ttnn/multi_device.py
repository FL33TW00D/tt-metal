# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import contextlib

from typing import List, Dict

import ttnn


def get_device_mesh_core_grid(device_mesh):
    compute_with_storage_grid_size = device_mesh.compute_with_storage_grid_size()
    return ttnn.CoreGrid(y=compute_with_storage_grid_size.y, x=compute_with_storage_grid_size.x)


DeviceMesh = ttnn._ttnn.multi_device.DeviceMesh
DeviceMesh.core_grid = property(get_device_mesh_core_grid)


def visualize_device_mesh(device_mesh):
    from rich import box, padding
    from rich.align import Align
    from rich.console import Console
    from rich.table import Table

    # Setup rich table
    rows, cols = device_mesh.shape
    mesh_table = Table(
        title=f"DeviceMesh(rows={rows}, cols={cols}):",
        show_header=False,
        show_footer=False,
        box=box.SQUARE,
        expand=False,
        show_lines=True,
        padding=(0, 0),
    )

    for _ in range(cols):
        mesh_table.add_column(justify="center", vertical="middle")

    # Populate table
    for row_idx in range(rows):
        row_cells = []
        for col_idx in range(cols):
            device = device_mesh.get_device(row_idx, col_idx)
            cell_content = f"Dev. ID: {device.id()}\n ({row_idx}, {col_idx})" if device else "Empty"
            cell = padding.Padding(Align(cell_content, "center", vertical="middle"), (0, 0))
            row_cells.append(cell)
        mesh_table.add_row(*row_cells)

    Console().print(mesh_table)


def get_num_devices() -> List[int]:
    return ttnn._ttnn.deprecated.device.GetNumAvailableDevices()


def get_num_pcie_devices() -> int:
    return ttnn._ttnn.deprecated.device.GetNumPCIeDevices()


def get_pcie_device_ids() -> List[int]:
    num_pcie_devices = get_num_pcie_devices()
    return list(range(num_pcie_devices))


def get_device_ids() -> List[int]:
    num_devices = get_num_devices()
    return list(range(num_devices))


def open_device_mesh(
    device_grid: ttnn.DeviceGrid,
    device_ids: List[int],
    l1_small_size: int = ttnn._ttnn.deprecated.device.DEFAULT_L1_SMALL_SIZE,
    trace_region_size: int = ttnn._ttnn.deprecated.device.DEFAULT_TRACE_REGION_SIZE,
    num_command_queues: int = 1,
):
    """
    open_device_mesh(device_grid: ttnn.DeviceGrid, device_ids: int) -> ttnn.DeviceMesh:

    Open a device with the given device_id. If the device is already open, return the existing device.
    """
    assert len(device_ids) > 0

    return ttnn._ttnn.multi_device.DeviceMesh(
        device_grid=device_grid.as_tuple(),
        device_ids=device_ids,
        l1_small_size=l1_small_size,
        trace_region_size=trace_region_size,
        num_command_queues=num_command_queues,
    )


def close_device_mesh(device_mesh):
    """
    close_device_mesh(multi_device: ttnn.Multi) -> None:

    Close the device and remove it from the device cache.
    """
    return ttnn._ttnn.multi_device.close_device_mesh(device_mesh)


@contextlib.contextmanager
def create_device_mesh(
    device_grid: ttnn.DeviceGrid,
    device_ids: List[int],
    l1_small_size: int = ttnn._ttnn.deprecated.device.DEFAULT_L1_SMALL_SIZE,
    trace_region_size: int = ttnn._ttnn.deprecated.device.DEFAULT_TRACE_REGION_SIZE,
    num_command_queues: int = 1,
):
    """
    create_device_mesh(device_grid: ttnn.DeviceGrid, device_ids: List[int]) -> ttnn.DeviceMesh

    Context manager for opening and closing a device.
    """
    device_mesh = open_device_mesh(
        device_grid=device_grid,
        device_ids=device_ids,
        l1_small_size=l1_small_size,
        trace_region_size=trace_region_size,
        num_command_queues=num_command_queues,
    )
    try:
        yield device_mesh
    finally:
        close_device_mesh(device_mesh)


def synchronize_devices(devices):
    """
    synchronize_device(device: ttnn.Device) -> None:

    Synchronize the device with host by waiting for all operations to complete.
    """
    if isinstance(devices, ttnn.Device):
        ttnn._ttnn.deprecated.device.Synchronize(devices)
    else:
        for device in devices.get_device_ids():
            ttnn._ttnn.deprecated.device.Synchronize(devices.get_device(device))


class TensorToMesh:
    """
    Defines the mapping of a torch.Tensor to a device mesh: e.g. Shard/Replicate.
    You can also "Bring your own TensorToMesh" based on your custom mapping.
    """

    def __init__(self, device_mesh):
        self.device_mesh = device_mesh

    def map(self, tensor: "torch.Tensor"):
        raise NotImplementedError("Subclasses must implement this method")

    def config(self):
        raise NotImplementedError("Subclasses must implement this method")


class MeshToTensor:
    """
    Defines the inverse operation of TensorToMesh. Given a set of per-device
    ttnn.Tensor objects (aggregated into a single ttnn.Tensor), this class defines
    the mapping back to one or many torch.Tensor objects.

    You can also "Bring your own MeshToTensor" based on your custom mapping.
    """

    def compose(self, tensor: ttnn.Tensor):
        raise NotImplementedError("Subclasses must implement this method")


class ShardTensorToMesh(TensorToMesh):
    def __init__(self, device_mesh, dim):
        super().__init__(device_mesh)
        self.shard_dim = dim

    def map(self, tensor: "torch.Tensor") -> Dict[int, ttnn.Tensor]:
        import torch

        sliced_tensors = torch.chunk(tensor, self.device_mesh.get_num_devices(), dim=self.shard_dim)
        return list(sliced_tensors)

    def config(self):
        return {
            "strategy": "shard",
            "shard_dim": f"{self.shard_dim}",
        }


class ShardTensor2dMesh(TensorToMesh):
    def __init__(self, device_mesh, shard_grid, shard_dimensions):
        super().__init__(device_mesh)
        self.shard_grid = shard_grid  # defines shape of 2D grid of shards
        self.shard_dimensions = shard_dimensions  # defines which dimensions to shard

    def map(self, tensor):
        import torch

        Y, X = 0, 1
        # Returns list of tensors to map to row-major ordering of chips in shard grid
        if self.shard_dimensions[Y] is None:
            row_tensors = [tensor.clone() for _ in range(self.shard_grid[Y])]
        else:
            row_tensors = torch.chunk(tensor, self.shard_grid[Y], dim=self.shard_dimensions[Y])

        if self.shard_dimensions[X] is None:
            tensor_2d_shards = [row_tensor.clone() for row_tensor in row_tensors for _ in range(self.shard_grid[X])]
        else:
            tensor_2d_shards = [
                tt for t in row_tensors for tt in torch.chunk(t, self.shard_grid[X], dim=self.shard_dimensions[X])
            ]
        return tensor_2d_shards

    def config(self):
        return {
            "strategy": "shard",
            "shard_dim": f"{self.shard_dimensions[0] if self.shard_dimensions[0] else self.shard_dimensions[1]}",
            # "strategy": "shard_2d",
            # "shard_grid_y": f"{self.shard_grid[0]}",
            # "shard_grid_x": f"{self.shard_grid[1]}",
        }


class ReplicateTensorToMesh(TensorToMesh):
    def __init__(self, device_mesh: DeviceMesh):
        super().__init__(device_mesh)

    def map(self, tensor: "torch.Tensor"):
        return [tensor for i in range(self.device_mesh.get_num_devices())]

    def config(self):
        return {
            "strategy": "replicate",
            "replication_factor": str(self.device_mesh.get_num_devices()),
        }


class ConcatMeshToTensor(MeshToTensor):
    def __init__(self, device_mesh: DeviceMesh, dim: int):
        self.concat_dim = dim
        self.device_mesh = device_mesh

    def compose(self, tensor: ttnn.Tensor) -> "torch.Tensor":
        import torch

        device_shards_converted_to_torch = [
            ttnn.to_torch(tt_input_tensor) for tt_input_tensor in ttnn.get_device_tensors(tensor)
        ]
        return torch.cat(device_shards_converted_to_torch, dim=self.concat_dim)


class ListMeshToTensor(MeshToTensor):
    def __init__(self, device_mesh: DeviceMesh):
        self.device_mesh = device_mesh

    def compose(self, tensor: ttnn.Tensor) -> List["torch.Tensor"]:
        return [ttnn.to_torch(tt_input_tensor) for tt_input_tensor in ttnn.get_device_tensors(tensor)]


__all__ = []
