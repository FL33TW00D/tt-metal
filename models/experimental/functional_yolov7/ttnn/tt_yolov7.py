import ttnn
import torch
import tt_lib
from models.experimental.functional_yolov7.ttnn.common import Conv
from tt_lib.fallback_ops import fallback_ops
from models.utility_functions import (
    tt_to_torch_tensor,
    torch_to_tt_tensor_rm,
)


class ttnn_SPPCSPC:
    def __init__(self, device, parameters, k=(5, 9, 13)) -> None:
        self.device = device
        self.parameters = parameters
        self.k = k
        self.cv1 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["cv1"])
        self.cv2 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["cv2"])
        self.cv3 = Conv([1, 20, 20, 512], (3, 3, 1, 1, 1, 1, 1, 1), parameters["cv3"])
        self.cv4 = Conv([1, 20, 20, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["cv4"])
        self.cv5 = Conv([1, 20, 20, 2048], (1, 1, 1, 1, 0, 0, 1, 1), parameters["cv5"], height_sharding=False)
        self.cv6 = Conv([1, 20, 20, 512], (3, 3, 1, 1, 1, 1, 1, 1), parameters["cv6"])
        self.cv7 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["cv7"])

    def __call__(self, x):
        # x = ttnn.sharded_to_interleaved(x, ttnn.L1_MEMORY_CONFIG)
        x1 = self.cv1(self.device, x)  # PCC: 0.9817001329875311 to 0.9805905043668925
        x1 = ttnn.silu(x1)
        x1 = self.cv3(self.device, x1)  # PCC: 0.9768847560818801 to 0.9760019824202963
        x1 = ttnn.silu(x1)
        x1 = self.cv4(self.device, x1)  # PCC: 0.9693328598815477 to 0.9680883351514727
        x1 = ttnn.silu(x1)
        x1 = ttnn.sharded_to_interleaved(x1, ttnn.L1_MEMORY_CONFIG)
        x1 = ttnn.to_layout(x1, ttnn.TILE_LAYOUT, dtype=ttnn.bfloat16)
        # print("x1: ", x1.shape, x1.memory_config)
        x1_m1 = ttnn.max_pool2d(  # PCC: 0.9744723321525488 to 0.7604622057126111
            input_tensor=x1,
            batch_size=1,
            input_h=20,
            input_w=20,
            channels=512,
            kernel_size=[5, 5],
            stride=[1, 1],
            padding=[2, 2],
            dilation=[1, 1],
        )
        x1_m2 = ttnn.max_pool2d(  # PCC: 0.9752950534769703
            input_tensor=x1,
            batch_size=1,
            input_h=20,
            input_w=20,
            channels=512,
            kernel_size=[9, 9],
            stride=[1, 1],
            padding=[4, 4],
            dilation=[1, 1],
        )
        # print("x1_m2: ", x1_m2.shape)
        x1_m3 = ttnn.max_pool2d(  # PCC: 0.9748731003654871
            input_tensor=x1,
            batch_size=1,
            input_h=20,
            input_w=20,
            channels=512,
            kernel_size=[13, 13],
            stride=[1, 1],
            padding=[6, 6],
            dilation=[1, 1],
        )
        x1 = ttnn.to_layout(x1, ttnn.ROW_MAJOR_LAYOUT)
        # print("x1: ", x1.memory_config)

        # x1_m1 = ttnn.to_layout(x1_m1, ttnn.TILE_LAYOUT)
        x1_m1 = ttnn.sharded_to_interleaved(x1_m1, ttnn.L1_MEMORY_CONFIG)
        # print("x1_m1: ", x1_m1.memory_config)
        # x1_m2 = ttnn.to_layout(x1_m2, ttnn.TILE_LAYOUT)
        x1_m2 = ttnn.sharded_to_interleaved(x1_m2, ttnn.L1_MEMORY_CONFIG)
        # print("x1_m2: ", x1_m2.memory_config)
        # x1_m3 = ttnn.to_layout(x1_m3, ttnn.TILE_LAYOUT)
        x1_m3 = ttnn.sharded_to_interleaved(x1_m3, ttnn.L1_MEMORY_CONFIG)
        # print("x1_m3: ", x1_m3.memory_config)

        # y1 = ttnn.concat(
        #     [x1, x1_m1, x1_m2, x1_m3], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        # )  # PCC: 0.06769340355865372 to -0.029705818317636776
        # ttnn.deallocate(x1)
        # ttnn.deallocate(x1_m1)
        # ttnn.deallocate(x1_m2)
        # ttnn.deallocate(x1_m3)
        # # print("x1: ", x1.memory_config)

        # # x1_m1 = ttnn.to_layout(x1_m1, ttnn.TILE_LAYOUT)
        # x1_m1 = ttnn.sharded_to_interleaved(x1_m1, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m1: ", x1_m1.memory_config)
        # # x1_m2 = ttnn.to_layout(x1_m2, ttnn.TILE_LAYOUT)
        # x1_m2 = ttnn.sharded_to_interleaved(x1_m2, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m2: ", x1_m2.memory_config)
        # # x1_m3 = ttnn.to_layout(x1_m3, ttnn.TILE_LAYOUT)
        # x1_m3 = ttnn.sharded_to_interleaved(x1_m3, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m3: ", x1_m3.memory_config)

        # y1 = ttnn.concat(
        #     [x1, x1_m1, x1_m2, x1_m3], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        # )  # PCC: 0.06769340355865372
        # ttnn.deallocate(x1)
        # ttnn.deallocate(x1_m1)
        # ttnn.deallocate(x1_m2)
        # ttnn.deallocate(x1_m3)
        # # print("x1: ", x1.memory_config)

        # # x1_m1 = ttnn.to_layout(x1_m1, ttnn.TILE_LAYOUT)
        # x1_m1 = ttnn.sharded_to_interleaved(x1_m1, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m1: ", x1_m1.memory_config)
        # # x1_m2 = ttnn.to_layout(x1_m2, ttnn.TILE_LAYOUT)
        # x1_m2 = ttnn.sharded_to_interleaved(x1_m2, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m2: ", x1_m2.memory_config)
        # # x1_m3 = ttnn.to_layout(x1_m3, ttnn.TILE_LAYOUT)
        # x1_m3 = ttnn.sharded_to_interleaved(x1_m3, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m3: ", x1_m3.memory_config)

        # y1 = ttnn.concat(
        #     [x1, x1_m1, x1_m2, x1_m3], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        # )  # PCC: 0.06769340355865372
        # ttnn.deallocate(x1)
        # ttnn.deallocate(x1_m1)
        # ttnn.deallocate(x1_m2)
        # ttnn.deallocate(x1_m3)
        # # print("x1: ", x1.memory_config)

        # # x1_m1 = ttnn.to_layout(x1_m1, ttnn.TILE_LAYOUT)
        # x1_m1 = ttnn.sharded_to_interleaved(x1_m1, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m1: ", x1_m1.memory_config)
        # # x1_m2 = ttnn.to_layout(x1_m2, ttnn.TILE_LAYOUT)
        # x1_m2 = ttnn.sharded_to_interleaved(x1_m2, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m2: ", x1_m2.memory_config)
        # # x1_m3 = ttnn.to_layout(x1_m3, ttnn.TILE_LAYOUT)
        # x1_m3 = ttnn.sharded_to_interleaved(x1_m3, ttnn.L1_MEMORY_CONFIG)
        # # print("x1_m3: ", x1_m3.memory_config)

        y1 = ttnn.concat(
            [x1, x1_m1, x1_m2, x1_m3], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )  # PCC: 0.06769340355865372
        ttnn.deallocate(x1)
        ttnn.deallocate(x1_m1)
        ttnn.deallocate(x1_m2)
        ttnn.deallocate(x1_m3)

        y1 = self.cv5(self.device, y1)
        y1 = ttnn.silu(y1)

        y1 = self.cv6(self.device, y1)
        y1 = ttnn.silu(y1)

        y2 = self.cv2(self.device, x)
        y2 = ttnn.silu(y2)

        y1 = ttnn.sharded_to_interleaved(y1, ttnn.L1_MEMORY_CONFIG)
        y2 = ttnn.sharded_to_interleaved(y2, ttnn.L1_MEMORY_CONFIG)
        out = ttnn.concat([y1, y2], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)

        out = self.cv7(self.device, out)
        out = ttnn.silu(out)

        ttnn.deallocate(y1)
        ttnn.deallocate(y2)

        return out


class ttnn_repconv:
    def __init__(self, device, parameters, input_shape) -> None:
        self.device = device
        self.parameters = parameters
        self.rbr_dense = Conv(input_shape, (3, 3, 1, 1, 1, 1, 1, 1), parameters["0"], height_sharding=False)
        self.rbr_1x1 = Conv(input_shape, (1, 1, 1, 1, 0, 0, 1, 1), parameters["1"], height_sharding=False)

    def __call__(self, x):
        x1 = self.rbr_dense(self.device, x)
        x2 = self.rbr_1x1(self.device, x)
        out = ttnn.add(x1, x2)
        out = ttnn.silu(out)
        ttnn.deallocate(x)
        ttnn.deallocate(x1)
        ttnn.deallocate(x2)
        return out


class ttnn_detect:
    def __init__(self, device, parameters, nc=80, anchors=(), ch=()) -> None:
        self.device = device
        self.nc = nc
        self.no = nc + 5
        self.nl = len(anchors)
        self.na = len(anchors[0]) // 2
        self.grid = [ttnn.zeros([1])] * self.nl
        a = torch.tensor(anchors).float().view(self.nl, -1, 2)
        a = ttnn.from_torch(a, dtype=ttnn.bfloat16, device=device)
        self.anchors = ttnn.clone(a)
        self.anchor_grid = ttnn.reshape((ttnn.clone(a)), (self.nl, 1, -1, 1, 1, 2))
        self.m = []
        self.convm_1 = Conv([1, 80, 80, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["0"], is_reshape=True)
        self.m.append(self.convm_1)

        self.convm_2 = Conv([1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["1"], is_reshape=True)
        self.m.append(self.convm_2)

        self.convm_2 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["2"], is_reshape=True)
        self.m.append(self.convm_2)
        print("ttnn yes")

    def __call__(self, x):
        z = []
        self.training = False
        self.stride = torch.tensor([8.0, 16.0, 32.0])
        self.stride = ttnn.from_torch(self.stride, dtype=ttnn.bfloat16)

        for i in range(self.nl):
            x[i] = self.m[i](self.device, x[i])
            bs, ny, nx, _ = x[i].shape
            x[i] = ttnn.reshape(x[i], (bs, self.na, self.no, ny, nx))
            x[i] = ttnn.to_torch(x[i])
            x[i] = x[i].permute(0, 1, 3, 4, 2).contiguous()
            # x[i] = ttnn.from_torch(x[i], dtype=ttnn.bfloat16)
            self.grid[i] = ttnn.to_torch(self.grid[i])
            if not self.training:
                if self.grid[i].shape[2:4] != x[i].shape[2:4]:
                    self.grid[i] = self._make_grid(nx=nx, ny=ny).to(x[i].device)

    def _make_grid(self, nx=20, ny=20):
        x_coords = ttnn.arange(0, nx, 1, device=self.device)  # X-coordinates
        x_coords = ttnn.reshape(x_coords, (1, nx))
        x_coords = ttnn.expand(x_coords, [ny, nx])
        print("x_coords shape:", x_coords.shape)
        y_coords = ttnn.arange(0, ny, 1, device=self.device)  # Y-coordinates
        print("y_coords shape:", y_coords.shape)
        y_coords = ttnn.reshape(y_coords, (ny, 1))
        print("y_coords shape:", y_coords.shape)
        y_coords = ttnn.expand(y_coords, [ny, nx])
        print("y_coords shape:", y_coords.shape)

        # yv, xv = torch.meshgrid([torch.arange(ny), torch.arange(nx)])
        return torch.stack((xv, yv), 2).view((1, 1, ny, nx, 2)).float()


class ttnn_yolov7:
    def __init__(self, device, parameters) -> None:
        self.device = device
        self.parameters = parameters
        self.nc = 80
        self.anchors = [[12, 16, 19, 36, 40, 28], [36, 75, 76, 55, 72, 146], [142, 110, 192, 243, 459, 401]]
        self.ch = [256, 512, 1024]
        self.conv1 = Conv([1, 640, 640, 3], (3, 3, 1, 1, 1, 1, 1, 1), parameters["0"], act_block_h=32)
        self.conv2 = Conv([1, 640, 640, 32], (3, 3, 2, 2, 1, 1, 1, 1), parameters["1"])
        self.conv3 = Conv([1, 320, 320, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["2"], act_block_h=32)
        self.conv4 = Conv([1, 320, 320, 64], (3, 3, 2, 2, 1, 1, 1, 1), parameters["3"])
        self.conv5 = Conv([1, 160, 160, 128], (1, 1, 1, 1, 0, 0, 1, 1), parameters["4"])
        self.conv6 = Conv([1, 160, 160, 128], (1, 1, 1, 1, 0, 0, 1, 1), parameters["5"])
        self.conv7 = Conv([1, 160, 160, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["6"])
        self.conv8 = Conv([1, 160, 160, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["7"])
        self.conv9 = Conv([1, 160, 160, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["8"])
        self.conv10 = Conv([1, 160, 160, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["9"])

        self.conv11 = Conv([1, 160, 160, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["11"])
        self.conv12 = Conv([1, 80, 80, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["13"], height_sharding=False)
        self.conv13 = Conv([1, 160, 160, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["14"])
        self.conv14 = Conv([1, 160, 160, 128], (3, 3, 2, 2, 1, 1, 1, 1), parameters["15"])

        self.conv15 = Conv([1, 80, 80, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["17"])
        self.conv16 = Conv([1, 80, 80, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["18"])
        self.conv17 = Conv([1, 80, 80, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["19"])
        self.conv18 = Conv([1, 80, 80, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["20"])
        self.conv19 = Conv([1, 80, 80, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["21"])
        self.conv20 = Conv([1, 80, 80, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["22"])

        self.conv21 = Conv([1, 80, 80, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["24"])
        self.conv22 = Conv(
            [1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["26"], act_block_h=64, height_sharding=False
        )
        self.conv23 = Conv([1, 80, 80, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["27"], height_sharding=False)
        self.conv24 = Conv([1, 80, 80, 256], (3, 3, 2, 2, 1, 1, 1, 1), parameters["28"])

        self.conv25 = Conv([1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["30"])
        self.conv26 = Conv([1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["31"])
        self.conv27 = Conv([1, 40, 40, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["32"])
        self.conv28 = Conv([1, 40, 40, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["33"])
        self.conv29 = Conv([1, 40, 40, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["34"])
        self.conv30 = Conv([1, 40, 40, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["35"])

        self.conv31 = Conv([1, 40, 40, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["37"], height_sharding=False)
        self.conv32 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["39"])
        self.conv33 = Conv([1, 40, 40, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["40"], act_block_h=64)
        self.conv34 = Conv(
            [1, 40, 40, 512], (3, 3, 2, 2, 1, 1, 1, 1), parameters["41"], act_block_h=64, height_sharding=False
        )

        self.conv35 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["43"])
        self.conv36 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["44"])
        self.conv37 = Conv([1, 20, 20, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["45"])
        self.conv38 = Conv([1, 20, 20, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["46"])
        self.conv39 = Conv([1, 20, 20, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["47"])
        self.conv40 = Conv([1, 20, 20, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["48"])

        self.conv41 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["50"], height_sharding=False)
        self.SPPCSPC = ttnn_SPPCSPC(device, parameters["51"])

        self.conv42 = Conv([1, 20, 20, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["52"])
        self.conv43 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["54"], num_cores_nhw=56)
        # self.conv43 = tt_lib.fallback_ops.Conv2d(
        #     weights=parameters["54"]["weight"],
        #     biases=parameters["54"]["bias"],
        #     in_channels=1024,
        #     out_channels=256,
        #     kernel_size=(1, 1),
        #     stride=(1, 1),
        #     padding=(0, 0),
        #     bias=False,
        # )

        self.conv44 = Conv([1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["56"])
        self.conv45 = Conv([1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["57"])
        self.conv46 = Conv([1, 40, 40, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["58"])
        self.conv47 = Conv([1, 40, 40, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["59"])
        self.conv48 = Conv([1, 40, 40, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["60"])
        self.conv49 = Conv([1, 40, 40, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["61"])

        self.conv50 = Conv([1, 40, 40, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["63"])
        self.conv51 = Conv([1, 40, 40, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["64"])
        self.conv52 = Conv([1, 80, 80, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["66"])

        self.conv53 = Conv([1, 80, 80, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["68"])
        self.conv54 = Conv([1, 80, 80, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["69"])
        self.conv55 = Conv([1, 80, 80, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["70"])
        self.conv56 = Conv([1, 80, 80, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["71"])
        self.conv57 = Conv([1, 80, 80, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["72"])
        self.conv58 = Conv([1, 80, 80, 64], (3, 3, 1, 1, 1, 1, 1, 1), parameters["73"])

        self.conv59 = Conv([1, 80, 80, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["75"])
        self.conv60 = Conv([1, 40, 40, 128], (1, 1, 1, 1, 0, 0, 1, 1), parameters["77"])
        self.conv61 = Conv([1, 80, 80, 128], (1, 1, 1, 1, 0, 0, 1, 1), parameters["78"])
        self.conv62 = Conv([1, 80, 80, 128], (3, 3, 2, 2, 1, 1, 1, 1), parameters["79"])

        self.conv63 = Conv([1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["81"])
        self.conv64 = Conv([1, 40, 40, 512], (1, 1, 1, 1, 0, 0, 1, 1), parameters["82"])
        self.conv65 = Conv([1, 40, 40, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["83"])
        self.conv66 = Conv([1, 40, 40, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["84"])
        self.conv67 = Conv([1, 40, 40, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["85"])
        self.conv68 = Conv([1, 40, 40, 128], (3, 3, 1, 1, 1, 1, 1, 1), parameters["86"])

        self.conv69 = Conv([1, 40, 40, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["88"])
        self.conv70 = Conv([1, 20, 20, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["90"], height_sharding=False)
        self.conv71 = Conv([1, 40, 40, 256], (1, 1, 1, 1, 0, 0, 1, 1), parameters["91"])
        self.conv72 = Conv([1, 40, 40, 256], (3, 3, 2, 2, 1, 1, 1, 1), parameters["92"])

        self.conv73 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["94"], height_sharding=False)
        self.conv74 = Conv([1, 20, 20, 1024], (1, 1, 1, 1, 0, 0, 1, 1), parameters["95"], height_sharding=False)
        self.conv75 = Conv([1, 20, 20, 512], (3, 3, 1, 1, 1, 1, 1, 1), parameters["96"])
        self.conv76 = Conv([1, 20, 20, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["97"])
        self.conv77 = Conv([1, 20, 20, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["98"])
        self.conv78 = Conv([1, 20, 20, 256], (3, 3, 1, 1, 1, 1, 1, 1), parameters["99"])

        self.conv79 = Conv([1, 20, 20, 2048], (1, 1, 1, 1, 0, 0, 1, 1), parameters["101"], height_sharding=False)
        self.repconv1 = ttnn_repconv(device, parameters["102"], [1, 80, 80, 128])
        self.repconv2 = ttnn_repconv(device, parameters["103"], [1, 40, 40, 256])
        self.repconv3 = ttnn_repconv(device, parameters["104"], [1, 20, 20, 512])
        self.detect = ttnn_detect(device, parameters["105"], self.nc, self.anchors, self.ch)

    def __call__(self, input_tensor):
        conv1 = self.conv1(self.device, input_tensor)
        conv1 = ttnn.silu(conv1)

        conv2 = self.conv2(self.device, conv1)
        conv2 = ttnn.silu(conv2)
        ttnn.deallocate(conv1)

        conv3 = self.conv3(self.device, conv2)
        conv3 = ttnn.silu(conv3)
        ttnn.deallocate(conv2)

        conv4 = self.conv4(self.device, conv3)
        conv4 = ttnn.silu(conv4)
        ttnn.deallocate(conv3)

        conv4 = ttnn.sharded_to_interleaved(conv4, ttnn.L1_MEMORY_CONFIG)

        conv5 = self.conv5(self.device, conv4)
        conv5 = ttnn.silu(conv5)

        conv6 = self.conv6(self.device, conv4)
        conv6 = ttnn.silu(conv6)

        conv7 = self.conv7(self.device, conv6)
        conv7 = ttnn.silu(conv7)

        conv8 = self.conv8(self.device, conv7)
        conv8 = ttnn.silu(conv8)

        conv9 = self.conv9(self.device, conv8)  # decrease in pcc - 0.988
        conv9 = ttnn.silu(conv9)

        conv10 = self.conv10(self.device, conv9)
        conv10 = ttnn.silu(conv10)  # decrease in pcc - 0.9856

        conv10 = ttnn.reshape(conv10, (1, 160, 160, 64))
        conv10 = ttnn.sharded_to_interleaved(conv10, ttnn.L1_MEMORY_CONFIG)

        conv8 = ttnn.reshape(conv8, (1, 160, 160, 64))
        conv8 = ttnn.sharded_to_interleaved(conv8, ttnn.L1_MEMORY_CONFIG)

        conv6 = ttnn.reshape(conv6, (1, 160, 160, 64))
        conv6 = ttnn.sharded_to_interleaved(conv6, ttnn.L1_MEMORY_CONFIG)

        conv5 = ttnn.reshape(conv5, (1, 160, 160, 64))
        conv5 = ttnn.sharded_to_interleaved(conv5, ttnn.L1_MEMORY_CONFIG)

        conv10 = ttnn.concat(
            [conv10, conv8, conv6, conv5], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )  # pcc = 0.99 (pcc 0.00909 - when inputs are in row major)
        ttnn.deallocate(conv4)
        ttnn.deallocate(conv7)
        ttnn.deallocate(conv9)

        conv11 = self.conv11(self.device, conv10)
        conv11 = ttnn.silu(conv11)
        ttnn.deallocate(conv5)
        ttnn.deallocate(conv6)
        ttnn.deallocate(conv8)

        mp1 = ttnn.max_pool2d(
            input_tensor=conv11,
            batch_size=1,
            input_h=160,
            input_w=160,
            channels=256,
            kernel_size=[2, 2],
            stride=[2, 2],
            padding=[0, 0],
            dilation=[1, 1],
        )
        ttnn.deallocate(conv10)

        mp1 = ttnn.sharded_to_interleaved(mp1, ttnn.L1_MEMORY_CONFIG)
        conv12 = self.conv12(self.device, mp1)
        conv12 = ttnn.silu(conv12)

        conv13 = self.conv13(self.device, conv11)  # PCC - 0.988204133831250
        conv13 = ttnn.silu(conv13)

        conv14 = self.conv14(self.device, conv13)  # PCC - 0.99
        conv14 = ttnn.silu(conv14)

        conv14 = ttnn.to_layout(conv14, ttnn.ROW_MAJOR_LAYOUT)
        conv14 = ttnn.sharded_to_interleaved(conv14, ttnn.L1_MEMORY_CONFIG)

        conv12 = ttnn.to_layout(conv12, ttnn.ROW_MAJOR_LAYOUT)
        conv12 = ttnn.sharded_to_interleaved(conv12, ttnn.L1_MEMORY_CONFIG)

        conv14 = ttnn.concat([conv14, conv12], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(conv11)
        ttnn.deallocate(mp1)
        ttnn.deallocate(conv13)

        conv15 = self.conv15(self.device, conv14)
        conv15 = ttnn.silu(conv15)
        ttnn.deallocate(conv12)

        conv16 = self.conv16(self.device, conv14)
        conv16 = ttnn.silu(conv16)
        ttnn.deallocate(conv14)

        conv17 = self.conv17(self.device, conv16)
        conv17 = ttnn.silu(conv17)

        conv18 = self.conv18(self.device, conv17)
        conv18 = ttnn.silu(conv18)

        conv19 = self.conv19(self.device, conv18)
        conv19 = ttnn.silu(conv19)

        conv20 = self.conv20(self.device, conv19)
        conv20 = ttnn.silu(conv20)

        conv20 = ttnn.to_layout(conv20, ttnn.ROW_MAJOR_LAYOUT)
        conv20 = ttnn.sharded_to_interleaved(conv20, ttnn.L1_MEMORY_CONFIG)

        conv18 = ttnn.to_layout(conv18, ttnn.ROW_MAJOR_LAYOUT)
        conv18 = ttnn.sharded_to_interleaved(conv18, ttnn.L1_MEMORY_CONFIG)

        conv16 = ttnn.to_layout(conv16, ttnn.ROW_MAJOR_LAYOUT)
        conv16 = ttnn.sharded_to_interleaved(conv16, ttnn.L1_MEMORY_CONFIG)

        conv15 = ttnn.to_layout(conv15, ttnn.ROW_MAJOR_LAYOUT)
        conv15 = ttnn.sharded_to_interleaved(conv15, ttnn.L1_MEMORY_CONFIG)

        conv20 = ttnn.concat([conv20, conv18, conv16, conv15], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(conv17)
        ttnn.deallocate(conv19)

        conv21 = self.conv21(self.device, conv20)
        conv21 = ttnn.silu(conv21)
        ttnn.deallocate(conv15)
        ttnn.deallocate(conv16)
        ttnn.deallocate(conv18)

        mp2 = ttnn.max_pool2d(
            input_tensor=conv21,
            batch_size=1,
            input_h=80,
            input_w=80,
            channels=512,
            kernel_size=[2, 2],
            stride=[2, 2],
            padding=[0, 0],
            dilation=[1, 1],
        )
        ttnn.deallocate(conv20)

        conv22 = self.conv22(self.device, mp2)
        conv22 = ttnn.silu(conv22)

        conv21 = ttnn.sharded_to_interleaved(conv21, ttnn.L1_MEMORY_CONFIG)
        conv23 = self.conv23(self.device, conv21)
        conv23 = ttnn.silu(conv23)

        conv24 = self.conv24(self.device, conv23)
        conv24 = ttnn.silu(conv24)

        conv24 = ttnn.to_layout(conv24, ttnn.ROW_MAJOR_LAYOUT)
        conv24 = ttnn.sharded_to_interleaved(conv24, ttnn.L1_MEMORY_CONFIG)

        conv22 = ttnn.to_layout(conv22, ttnn.ROW_MAJOR_LAYOUT)
        conv22 = ttnn.sharded_to_interleaved(conv22, ttnn.L1_MEMORY_CONFIG)

        conv24 = ttnn.concat([conv24, conv22], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(mp2)
        ttnn.deallocate(conv23)

        conv25 = self.conv25(self.device, conv24)
        conv25 = ttnn.silu(conv25)
        ttnn.deallocate(conv22)

        conv26 = self.conv26(self.device, conv24)
        conv26 = ttnn.silu(conv26)
        ttnn.deallocate(conv24)

        conv27 = self.conv27(self.device, conv26)
        conv27 = ttnn.silu(conv27)

        conv28 = self.conv28(self.device, conv27)
        conv28 = ttnn.silu(conv28)

        conv29 = self.conv29(self.device, conv28)
        conv29 = ttnn.silu(conv29)

        conv30 = self.conv30(self.device, conv29)
        conv30 = ttnn.silu(conv30)

        conv30 = ttnn.to_layout(conv30, ttnn.ROW_MAJOR_LAYOUT)
        conv30 = ttnn.sharded_to_interleaved(conv30, ttnn.L1_MEMORY_CONFIG)

        conv28 = ttnn.to_layout(conv28, ttnn.ROW_MAJOR_LAYOUT)
        conv28 = ttnn.sharded_to_interleaved(conv28, ttnn.L1_MEMORY_CONFIG)

        conv26 = ttnn.to_layout(conv26, ttnn.ROW_MAJOR_LAYOUT)
        conv26 = ttnn.sharded_to_interleaved(conv26, ttnn.L1_MEMORY_CONFIG)

        conv25 = ttnn.to_layout(conv25, ttnn.ROW_MAJOR_LAYOUT)
        conv25 = ttnn.sharded_to_interleaved(conv25, ttnn.L1_MEMORY_CONFIG)

        conv30 = ttnn.concat([conv30, conv28, conv26, conv25], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(conv27)
        ttnn.deallocate(conv29)

        conv31 = self.conv31(self.device, conv30)
        conv31 = ttnn.silu(conv31)
        ttnn.deallocate(conv25)
        ttnn.deallocate(conv26)
        ttnn.deallocate(conv28)

        conv31 = ttnn.sharded_to_interleaved(conv31, ttnn.L1_MEMORY_CONFIG)
        mp3 = ttnn.max_pool2d(
            input_tensor=conv31,
            batch_size=1,
            input_h=40,
            input_w=40,
            channels=1024,
            kernel_size=[2, 2],
            stride=[2, 2],
            padding=[0, 0],
            dilation=[1, 1],
        )
        ttnn.deallocate(conv30)

        mp3 = ttnn.sharded_to_interleaved(mp3, ttnn.L1_MEMORY_CONFIG)
        mp3 = ttnn.to_layout(mp3, ttnn.TILE_LAYOUT)
        conv32 = self.conv32(self.device, mp3)  # PCC: 0.99
        conv32 = ttnn.silu(conv32)

        conv33 = self.conv33(self.device, conv31)  # PCC:  0.988924054922872
        conv33 = ttnn.silu(conv33)

        conv33 = ttnn.sharded_to_interleaved(conv33, ttnn.L1_MEMORY_CONFIG)
        conv34 = self.conv34(self.device, conv33)  # PCC: 0.9829567970242702
        conv34 = ttnn.silu(conv34)

        conv34 = ttnn.sharded_to_interleaved(conv34, ttnn.L1_MEMORY_CONFIG)
        conv32 = ttnn.sharded_to_interleaved(conv32, ttnn.L1_MEMORY_CONFIG)

        conv34 = ttnn.concat(
            [conv34, conv32], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )  # PCC: 0.9876854226303078 to 0.9868002482338611
        ttnn.deallocate(mp3)
        ttnn.deallocate(conv33)

        conv35 = self.conv35(self.device, conv34)  # PCC: 0.9856130147840862
        conv35 = ttnn.silu(conv35)
        ttnn.deallocate(conv32)

        conv36 = self.conv36(self.device, conv34)  # PCC: 0.9828522540462243
        conv36 = ttnn.silu(conv36)
        ttnn.deallocate(conv34)

        conv37 = self.conv37(self.device, conv36)  # PCC: 0.9849196330481047
        conv37 = ttnn.silu(conv37)

        conv38 = self.conv38(self.device, conv37)  # PCC: 0.9793299310427367
        conv38 = ttnn.silu(conv38)

        conv39 = self.conv39(self.device, conv38)  # PCC: 0.9703874217816807
        conv39 = ttnn.silu(conv39)

        conv40 = self.conv40(self.device, conv39)  # PCC: 0.9623080326487907
        conv40 = ttnn.silu(conv40)

        conv40 = ttnn.sharded_to_interleaved(conv40, ttnn.L1_MEMORY_CONFIG)
        conv38 = ttnn.sharded_to_interleaved(conv38, ttnn.L1_MEMORY_CONFIG)
        conv36 = ttnn.sharded_to_interleaved(conv36, ttnn.L1_MEMORY_CONFIG)
        conv35 = ttnn.sharded_to_interleaved(conv35, ttnn.L1_MEMORY_CONFIG)

        conv40 = ttnn.concat(
            [conv40, conv38, conv36, conv35], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )  # PCC: 0.9814903200725837 to 0.9803143372865541
        ttnn.deallocate(conv37)
        ttnn.deallocate(conv39)

        conv41 = self.conv41(self.device, conv40)  # PCC: 0.9801213087446525 to 0.9788919219364098
        conv41 = ttnn.silu(conv41)
        ttnn.deallocate(conv35)
        ttnn.deallocate(conv36)
        ttnn.deallocate(conv38)
        ttnn.deallocate(conv40)

        SPPCSPC = self.SPPCSPC(conv41)  # PCC: 0.16423798979582446

        conv42 = self.conv42(self.device, SPPCSPC)
        conv42 = ttnn.silu(conv42)

        conv42 = ttnn.to_layout(conv42, ttnn.ROW_MAJOR_LAYOUT)
        conv42 = ttnn.sharded_to_interleaved(conv42, ttnn.L1_MEMORY_CONFIG)
        conv42 = ttnn.reshape(conv42, (1, 20, 20, 256))
        conv42 = ttnn.upsample(conv42, 2)
        conv42 = ttnn.reshape(conv42, (1, 1, 1600, 256))

        conv43 = self.conv43(self.device, conv31)  # PCC: 0.99
        conv43 = ttnn.silu(conv43)

        conv43 = ttnn.to_layout(conv43, ttnn.ROW_MAJOR_LAYOUT)
        conv43 = ttnn.sharded_to_interleaved(conv43, ttnn.L1_MEMORY_CONFIG)

        conv43 = ttnn.concat([conv43, conv42], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(conv31)

        conv44 = self.conv44(self.device, conv43)
        conv44 = ttnn.silu(conv44)
        ttnn.deallocate(conv42)

        conv45 = self.conv45(self.device, conv43)
        conv45 = ttnn.silu(conv45)

        conv46 = self.conv46(self.device, conv45)
        conv46 = ttnn.silu(conv46)
        ttnn.deallocate(conv43)

        conv47 = self.conv47(self.device, conv46)
        conv47 = ttnn.silu(conv47)

        conv48 = self.conv48(self.device, conv47)
        conv48 = ttnn.silu(conv48)

        conv49 = self.conv49(self.device, conv48)
        conv49 = ttnn.silu(conv49)

        conv49 = ttnn.sharded_to_interleaved(conv49, ttnn.L1_MEMORY_CONFIG)
        conv48 = ttnn.sharded_to_interleaved(conv48, ttnn.L1_MEMORY_CONFIG)
        conv47 = ttnn.sharded_to_interleaved(conv47, ttnn.L1_MEMORY_CONFIG)
        conv46 = ttnn.sharded_to_interleaved(conv46, ttnn.L1_MEMORY_CONFIG)
        conv45 = ttnn.sharded_to_interleaved(conv45, ttnn.L1_MEMORY_CONFIG)
        conv44 = ttnn.sharded_to_interleaved(conv44, ttnn.L1_MEMORY_CONFIG)
        conv49 = ttnn.concat(
            [conv49, conv48, conv47, conv46, conv45, conv44], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )

        conv50 = self.conv50(self.device, conv49)
        conv50 = ttnn.silu(conv50)
        ttnn.deallocate(conv44)
        ttnn.deallocate(conv45)
        ttnn.deallocate(conv46)
        ttnn.deallocate(conv47)
        ttnn.deallocate(conv48)

        conv51 = self.conv51(self.device, conv50)
        conv51 = ttnn.silu(conv51)
        ttnn.deallocate(conv49)

        conv51 = ttnn.to_layout(conv51, ttnn.ROW_MAJOR_LAYOUT)
        conv51 = ttnn.sharded_to_interleaved(conv51, ttnn.L1_MEMORY_CONFIG)
        conv51 = ttnn.reshape(conv51, (1, 40, 40, 128))
        conv51 = ttnn.upsample(conv51, 2)

        conv52 = self.conv52(self.device, conv21)
        conv52 = ttnn.silu(conv52)

        conv52 = ttnn.to_layout(conv52, ttnn.ROW_MAJOR_LAYOUT)
        conv52 = ttnn.reshape(conv52, (1, 80, 80, 128))
        conv52 = ttnn.sharded_to_interleaved(conv52, ttnn.L1_MEMORY_CONFIG)

        conv52 = ttnn.concat([conv52, conv51], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(conv51)

        conv53 = self.conv53(self.device, conv52)
        conv53 = ttnn.silu(conv53)

        conv54 = self.conv54(self.device, conv52)
        conv54 = ttnn.silu(conv54)

        conv55 = self.conv55(self.device, conv54)
        conv55 = ttnn.silu(conv55)

        conv56 = self.conv56(self.device, conv55)
        conv56 = ttnn.silu(conv56)

        conv57 = self.conv57(self.device, conv56)
        conv57 = ttnn.silu(conv57)

        conv58 = self.conv58(self.device, conv57)
        conv58 = ttnn.silu(conv58)

        conv58 = ttnn.sharded_to_interleaved(conv58, ttnn.L1_MEMORY_CONFIG)
        conv57 = ttnn.sharded_to_interleaved(conv57, ttnn.L1_MEMORY_CONFIG)
        conv56 = ttnn.sharded_to_interleaved(conv56, ttnn.L1_MEMORY_CONFIG)
        conv55 = ttnn.sharded_to_interleaved(conv55, ttnn.L1_MEMORY_CONFIG)
        conv54 = ttnn.sharded_to_interleaved(conv54, ttnn.L1_MEMORY_CONFIG)
        conv53 = ttnn.sharded_to_interleaved(conv53, ttnn.L1_MEMORY_CONFIG)
        conv58 = ttnn.concat(
            [conv58, conv57, conv56, conv55, conv54, conv53], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )
        ttnn.deallocate(conv52)

        conv59 = self.conv59(self.device, conv58)
        conv59 = ttnn.silu(conv59)
        ttnn.deallocate(conv53)
        ttnn.deallocate(conv54)
        ttnn.deallocate(conv55)
        ttnn.deallocate(conv56)
        ttnn.deallocate(conv57)
        ttnn.deallocate(conv58)

        mp4 = ttnn.max_pool2d(
            input_tensor=conv59,
            batch_size=1,
            input_h=80,
            input_w=80,
            channels=128,
            kernel_size=[2, 2],
            stride=[2, 2],
            padding=[0, 0],
            dilation=[1, 1],
        )

        conv60 = self.conv60(self.device, mp4)
        conv60 = ttnn.silu(conv60)

        conv61 = self.conv61(self.device, conv59)
        conv61 = ttnn.silu(conv61)

        conv62 = self.conv62(self.device, conv61)
        conv62 = ttnn.silu(conv62)

        conv62 = ttnn.sharded_to_interleaved(conv62, ttnn.L1_MEMORY_CONFIG)
        conv60 = ttnn.sharded_to_interleaved(conv60, ttnn.L1_MEMORY_CONFIG)
        conv50 = ttnn.sharded_to_interleaved(conv50, ttnn.L1_MEMORY_CONFIG)
        conv62 = ttnn.concat([conv62, conv60, conv50], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(conv50)
        ttnn.deallocate(conv60)
        ttnn.deallocate(conv61)

        conv63 = self.conv63(self.device, conv62)
        conv63 = ttnn.silu(conv63)

        conv64 = self.conv64(self.device, conv62)
        conv64 = ttnn.silu(conv64)

        conv65 = self.conv65(self.device, conv64)
        conv65 = ttnn.silu(conv65)
        ttnn.deallocate(conv62)

        conv66 = self.conv66(self.device, conv65)
        conv66 = ttnn.silu(conv66)

        conv67 = self.conv67(self.device, conv66)
        conv67 = ttnn.silu(conv67)

        conv68 = self.conv68(self.device, conv67)
        conv68 = ttnn.silu(conv68)

        conv68 = ttnn.sharded_to_interleaved(conv68, ttnn.L1_MEMORY_CONFIG)
        conv67 = ttnn.sharded_to_interleaved(conv67, ttnn.L1_MEMORY_CONFIG)
        conv66 = ttnn.sharded_to_interleaved(conv66, ttnn.L1_MEMORY_CONFIG)
        conv65 = ttnn.sharded_to_interleaved(conv65, ttnn.L1_MEMORY_CONFIG)
        conv64 = ttnn.sharded_to_interleaved(conv64, ttnn.L1_MEMORY_CONFIG)
        conv63 = ttnn.sharded_to_interleaved(conv63, ttnn.L1_MEMORY_CONFIG)

        conv68 = ttnn.concat(
            [conv68, conv67, conv66, conv65, conv64, conv63], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )

        conv69 = self.conv69(self.device, conv68)
        conv69 = ttnn.silu(conv69)
        ttnn.deallocate(conv63)
        ttnn.deallocate(conv64)
        ttnn.deallocate(conv65)
        ttnn.deallocate(conv66)
        ttnn.deallocate(conv67)

        mp5 = ttnn.max_pool2d(
            input_tensor=conv69,
            batch_size=1,
            input_h=40,
            input_w=40,
            channels=256,
            kernel_size=[2, 2],
            stride=[2, 2],
            padding=[0, 0],
            dilation=[1, 1],
        )
        ttnn.deallocate(conv68)

        mp5 = ttnn.to_layout(mp5, ttnn.ROW_MAJOR_LAYOUT)
        mp5 = ttnn.sharded_to_interleaved(mp5, ttnn.L1_MEMORY_CONFIG)
        conv70 = self.conv70(self.device, mp5)
        conv70 = ttnn.silu(conv70)

        conv71 = self.conv71(self.device, conv69)
        conv71 = ttnn.silu(conv71)

        conv71 = ttnn.sharded_to_interleaved(conv71, ttnn.L1_MEMORY_CONFIG)
        conv72 = self.conv72(self.device, conv71)
        conv72 = ttnn.silu(conv72)
        ttnn.deallocate(mp5)

        conv72 = ttnn.to_layout(conv72, ttnn.ROW_MAJOR_LAYOUT)
        conv72 = ttnn.sharded_to_interleaved(conv72, ttnn.L1_MEMORY_CONFIG)

        conv70 = ttnn.sharded_to_interleaved(conv70, ttnn.L1_MEMORY_CONFIG)
        conv70 = ttnn.to_layout(conv70, ttnn.ROW_MAJOR_LAYOUT)

        SPPCSPC = ttnn.to_layout(SPPCSPC, ttnn.ROW_MAJOR_LAYOUT)
        SPPCSPC = ttnn.sharded_to_interleaved(SPPCSPC, ttnn.L1_MEMORY_CONFIG)

        conv72 = ttnn.concat([conv72, conv70, SPPCSPC], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG)
        ttnn.deallocate(conv71)

        conv73 = self.conv73(self.device, conv72)
        conv73 = ttnn.silu(conv73)
        ttnn.deallocate(conv70)
        ttnn.deallocate(SPPCSPC)

        conv74 = self.conv74(self.device, conv72)
        conv74 = ttnn.silu(conv74)

        conv75 = self.conv75(self.device, conv74)
        conv75 = ttnn.silu(conv75)
        ttnn.deallocate(conv72)

        conv76 = self.conv76(self.device, conv75)
        conv76 = ttnn.silu(conv76)

        conv77 = self.conv77(self.device, conv76)
        conv77 = ttnn.silu(conv77)

        conv78 = self.conv78(self.device, conv77)
        conv78 = ttnn.silu(conv78)

        conv73 = ttnn.sharded_to_interleaved(conv73, ttnn.L1_MEMORY_CONFIG)
        conv74 = ttnn.sharded_to_interleaved(conv74, ttnn.L1_MEMORY_CONFIG)
        conv75 = ttnn.sharded_to_interleaved(conv75, ttnn.L1_MEMORY_CONFIG)
        conv76 = ttnn.sharded_to_interleaved(conv76, ttnn.L1_MEMORY_CONFIG)
        conv77 = ttnn.sharded_to_interleaved(conv77, ttnn.L1_MEMORY_CONFIG)
        conv78 = ttnn.sharded_to_interleaved(conv78, ttnn.L1_MEMORY_CONFIG)
        conv78 = ttnn.concat(
            [conv78, conv77, conv76, conv75, conv74, conv73], dim=3, memory_config=ttnn.L1_MEMORY_CONFIG
        )

        conv79 = self.conv79(self.device, conv78)
        conv79 = ttnn.silu(conv79)
        ttnn.deallocate(conv73)
        ttnn.deallocate(conv74)
        ttnn.deallocate(conv75)
        ttnn.deallocate(conv76)
        ttnn.deallocate(conv77)

        repconv1 = self.repconv1(conv59)
        print("repconv1 shape: ", repconv1.shape)
        conv69 = ttnn.sharded_to_interleaved(conv69, ttnn.L1_MEMORY_CONFIG)
        conv69 = ttnn.to_layout(conv69, ttnn.ROW_MAJOR_LAYOUT)
        repconv2 = self.repconv2(conv69)
        print("repconv2 shape: ", repconv2.shape)
        repconv3 = self.repconv3(conv79)
        print("repconv3 shape: ", repconv3.shape)

        output = self.detect([repconv1, repconv2, repconv3])

        return output
