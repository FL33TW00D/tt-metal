import torch
import ttnn

device_id = 0
device = ttnn.open_device(device_id=device_id)

torch_input_tensor_a = torch.rand(4, 7, dtype=torch.float32)
input_tensor_a = ttnn.from_torch(torch_input_tensor_a, dtype=ttnn.bfloat16, layout=ttnn.TILE_LAYOUT, device=device)


x = ttnn.experimental.welford_layer_norm(input_tensor_a)
print("X: ", x)

ttnn.close_device(device)
