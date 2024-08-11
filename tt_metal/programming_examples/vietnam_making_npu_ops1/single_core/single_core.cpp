#include <iostream>
#include <memory>

#include "common/bfloat16.hpp"
#include "tt_metal/common/constants.hpp"
#include "tt_metal/detail/tt_metal.hpp"
#include "tt_metal/host_api.hpp"
#include "ttnn/cpp/ttnn/deprecated/tt_dnn/op_library/work_split.hpp"
using namespace tt::tt_metal;
using namespace tt::constants;
using namespace tt;

uint16_t round_to_nearest_even(float val) {
  uint _val = reinterpret_cast<uint &>(val);
  return static_cast<ushort>((_val + ((_val >> 16) & 1) + ((uint)0x7FFF)) >>
                             16);
}

float bf16_to_float(uint16_t bf16) {
  union {
    uint32_t u;
    float f;
  } tmp;
  tmp.u = bf16 << 16;
  return tmp.f;
}

int main() {
  // create device object and get command queue.
  constexpr int device_id = 0;
  Device *device = CreateDevice(device_id);
  CommandQueue &cq = device->command_queue();

  //////////////////////////////////////////////////////////////////////////////////
  // Allocate host buffer0.
  // Fill host buffer0 with neg, zero, pos for each tile.
  //////////////////////////////////////////////////////////////////////////////////
  auto host_num_tiles = 3;
  auto host_buffer_size = host_num_tiles * 1024 * 2;

  auto host_buffer0 = std::shared_ptr<void>(malloc(host_buffer_size), free);
  auto host_buffer0_ptr = reinterpret_cast<uint16_t *>(host_buffer0.get());
  for (int i = 0; i < 1024; ++i) {
    host_buffer0_ptr[i] = round_to_nearest_even(-i % 32);
    host_buffer0_ptr[1024 + i] = round_to_nearest_even(0);
    host_buffer0_ptr[2048 + i] = round_to_nearest_even(i % 32);
  }

  // auto host_buffer1 = std::shared_ptr<void>(malloc(host_buffer_size), free);

  /////////////////////////////////////////////////////////////////////////////////
  // TODO: Allocate Device Buffer 0,1
  /////////////////////////////////////////////////////////////////////////////////
  // uint32_t num_tiles = /*TODO*/;
  // auto page_size =  /*TODO*/;
  // auto device_buffer_size = /*TODO*/;

  // auto device_buffer0_config =
  //     InterleavedBufferConfig{.device = device,
  //                             .size = device_buffer_size ,
  //                             .page_size = page_size,
  //                             .buffer_type = BufferType::DRAM};
  // auto device_buffer0 = CreateBuffer(device_buffer0_config);

  // auto device_buffer1_config =
  //     InterleavedBufferConfig{.device = device,
  //                             .size =  /*TODO*/,
  //                             .page_size =  /*TODO*/,
  //                             .buffer_type = BufferType::DRAM};
  // auto device_buffer1 = CreateBuffer(device_buffer1_config);

  // std::cout << "device_buffer0 total size : " << device_buffer0->size()
  //           << std::endl;
  // std::cout << "device_buffer0 page size : " << device_buffer0->page_size()
  //           << std::endl;
  // std::cout << "device_buffer1 total size : " << device_buffer1->size()
  //           << std::endl;
  // std::cout << "device_buffer1 page size : " << device_buffer1->page_size()
  //           << std::endl;

  /////////////////////////////////////////////////////////////////////////////////
  // Copy host buffer0 to device buffer0
  /////////////////////////////////////////////////////////////////////////////////

  // EnqueueWriteBuffer(cq, device_buffer0, host_buffer0.get(),
  //                    true /*blocking*/);

  /////////////////////////////////////////////////////////////////////////////////
  // Create program instance.
  /////////////////////////////////////////////////////////////////////////////////

  // Program program = CreateProgram();
  // auto target_cores = CoreCoord{0, 0};
  /////////////////////////////////////////////////////////////////////////////////
  // Allocate circular buffer 0 and 1.
  /////////////////////////////////////////////////////////////////////////////////

  // auto cb_num_tiles = 2;
  // auto cb0_id = CB::c_in0;
  // auto cb0_data_format = tt::DataFormat::Float16_b;
  // auto cb0_config =
  //     CircularBufferConfig(
  //         cb_num_tiles * page_size,
  //         {{cb0_id, cb0_data_format}})
  //         .set_page_size(cb0_id, page_size);
  // CreateCircularBuffer(program, target_cores, cb0_config);

  // auto cb1_id = CB::c_out0;
  // auto cb1_data_format = tt::DataFormat::Float16_b;
  // auto cb1_config = CircularBufferConfig(/* TODO */)
  //                       .set_page_size( /*TODO*/);
  // CreateCircularBuffer(program, target_cores, cb1_config);

  // std::cout << "cb0 total size : " << cb0_config.total_size() << std::endl;
  // std::cout << "cb1 total size : " << cb1_config.total_size() << std::endl;

  /////////////////////////////////////////////////////////////////////////////////
  // Create kernels
  /////////////////////////////////////////////////////////////////////////////////
  // KernelHandle compute_kernel_id =
  //     CreateKernel(program,
  //                  "tt_metal/programming_examples/vietnam_making_npu_ops1/"
  //                  "single_core/kernels/compute.cpp",
  //                  target_cores,
  //                  ComputeConfig{
  //                      .compile_args = {},
  //                      .defines = {},
  //                  });

  // const uint32_t device_buffer0_is_dram = static_cast<uint32_t>(
  //     device_buffer0_config.buffer_type == BufferType::DRAM);
  // const uint32_t device_buffer1_is_dram = static_cast<uint32_t>(
  //     device_buffer1_config.buffer_type == BufferType::DRAM);

  // KernelHandle reader_kernel_id = CreateKernel(
  //     program,
  //     "tt_metal/programming_examples/vietnam_making_npu_ops1/single_core/"
  //     "kernels/"
  //     "reader.cpp" /*reader kernel path. TODO*/,
  //     target_cores,
  //     ReaderDataMovementConfig({device_buffer0_is_dram},
  //                              {}));

  // KernelHandle writer_kernel_id = CreateKernel(
  //     program,
  //      /* TODO path to writer.cpp*/,
  //     target_cores,
  //     WriterDataMovementConfig(/* TODO */));

  /////////////////////////////////////////////////////////////////////////////////
  // Set runtime args
  /////////////////////////////////////////////////////////////////////////////////

  // const std::vector<uint32_t> reader_runtime_args = {
  //     device_buffer0->address(), static_cast<uint32_t>(cb0_id),
  //     num_tiles};

  // SetRuntimeArgs(program, reader_kernel_id, target_cores, reader_runtime_args);

  // const std::vector<uint32_t> writer_runtime_args = {/*TODO*/};

  // SetRuntimeArgs(program, writer_kernel_id, target_cores, writer_runtime_args);

  // const std::vector<uint32_t> compute_runtime_args = {};

  // SetRuntimeArgs(program, compute_kernel_id, target_cores,
  //                compute_runtime_args);

  //////////////////////////////////////////////////////////////////////////////////
  // EnqueueProgram and Copy device buffer1 to host buffer1
  //////////////////////////////////////////////////////////////////////////////////
  // EnqueueProgram(cq, program, true /*blocking*/);

  // EnqueueReadBuffer(cq, device_buffer1, host_buffer1.get(), true /*blocking*/);
  // auto host_buffer1_ptr = reinterpret_cast<uint16_t *>(host_buffer1.get());
  // for (int tile = 0; tile < 3; ++tile) {
  //   for (int r = 0; r < 32; ++r) {
  //     for (int c = 0; c < 32; ++c) {
  //       std::cout << bf16_to_float(host_buffer1_ptr[tile * 1024 + r * 32 + c])
  //                 << " ";
  //     }
  //     std::cout << std::endl;
  //   }
  //   std::cout << "\n\n\n";
  // }

  // {
  //   auto pass = true;
  //   auto relu = [](float f) { return f > 0.0 ? f : 0.0; };
  //   auto host_buffer0_ptr = reinterpret_cast<uint16_t *>(host_buffer0.get());
  //   auto host_buffer1_ptr = reinterpret_cast<uint16_t *>(host_buffer1.get());

  //   for (int tile = 0; tile < host_num_tiles; ++tile) {
  //     for (int i = 0; i < 1024; ++i) {
  //       auto answer = relu(bf16_to_float(host_buffer0_ptr[tile * 1024 + i]));
  //       auto result_npu = bf16_to_float(host_buffer1_ptr[tile * 1024 + i]);

  //       pass = pass && (answer == result_npu);
  //     }
  //   }
  //   if (pass) {
  //     std::cout << "Success" << std::endl;
  //   } else {
  //     std::cout << "Fail" << std::endl;
  //   }
  // }

  CloseDevice(device);

  return 0;
}
