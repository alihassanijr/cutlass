/***************************************************************************************************
 * Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **************************************************************************************************/
/*!
  \file Base Schedule for Distributed GEMM

  Templates Distributed GEMM schedules so that they can be expressed as a set of CuTe primitives and
  other static values.
*/

#pragma once

#include "cute/layout.hpp"
#include "cute/tensor.hpp"
#include "cutlass/cutlass.h"


///////////////////////////////////////////////////////////////////////////////

namespace cutlass::distributed::schedules {


namespace detail {

template <typename ProblemShape>
constexpr auto check_problem_shape(const ProblemShape & problem_shape) {
  static_assert(cute::rank(ProblemShape{}) == 3 || cute::rank(ProblemShape{}) == 4,
      "Expected rank-3 or rank-4 GEMM problem shape.");
  if constexpr (cute::rank(ProblemShape{}) == 3) {
    return append<4>(problem_shape, 1);
  } else {
    return problem_shape;
  }
}

} // namespace detail


/*
 * Distributed GEMM schedules define exactly how operand tensors are tiled and sliced across 
 * processors (GPUs) and stages/iterations.
 *
 * BaseSchedule's role is to ease the implementation of arbitrary Distributed GEMM schedules
 * and reduce code repetition, simply by reducing the implementation to CuTe primitives and a few
 * other static values (buffer sizes, whether tensors are rotated using memcpies or not, and the
 * like.)
 */
template <
  class TP_,                      // CuTe constant defining the number of processors / GPUs / TP value
  class ProcessorTiler_,          // CuTe tiler defining how fully materialized tensors are sharded across devices
  class IterationTiler_,          // CuTe tiler defining how local tensors are tiled across stages/iterations
  class PeerDeviceMapping_,       // CuTe layout mapping device index and stage/iteration to the device's peer index for that stage/iteration
  class ProcessorMappingA_,       // CuTe layout mapping device index to slice index for tensor A tiled with ProcessorTiler_
  class ProcessorMappingB_,       // CuTe layout mapping device index to slice index for tensor B tiled with ProcessorTiler_
  class ProcessorMappingC_,       // CuTe layout mapping device index to slice index for tensor C tiled with ProcessorTiler_
  class ProcessorMappingD_,       // CuTe layout mapping device index to slice index for tensor D tiled with ProcessorTiler_
  class IterationMappingA_,       // CuTe layout mapping device index and stage/iteration to slice index for tensor A tiled with IterationTiler_
  class IterationMappingB_,       // CuTe layout mapping device index and stage/iteration to slice index for tensor B tiled with IterationTiler_
  class IterationMappingC_,       // CuTe layout mapping device index and stage/iteration to slice index for tensor C tiled with IterationTiler_
  class IterationMappingD_,       // CuTe layout mapping device index and stage/iteration to slice index for tensor D tiled with IterationTiler_
  bool MemcpyA_,                  // Whether tensor A is memcpied
  bool MemcpyB_,                  // Whether tensor B is memcpied
  bool KernelWritesArrivalFlag_,  // Whether the kernel writes arrival flags (when tensors are directly accessed from peer and not memcpied)
  int NumBuffersA_,               // Number of buffers required for tensor A
  int NumBuffersB_,               // Number of buffers required for tensor B
  int NumBuffersC_,               // Number of buffers required for tensor C
  int NumBuffersD_>               // Number of buffers required for tensor D
struct BaseSchedule {

  using TP = TP_;

  static_assert(
      cute::is_static<TP>::value && cute::is_integral<TP>::value && cute::rank(TP{}) == 1 && cute::depth(TP{}) == 0,
      "Only integers allowed for TP at this time.");

  static_assert(cute::rank(ProcessorTiler_{}) == 4, "Expected rank-4 processor tiler.");
  static_assert(cute::rank(IterationTiler_{}) == 4, "Expected rank-4 iteration tiler.");

  static_assert(cute::rank(PeerDeviceMapping_{}) == 3, 
      "PeerDeviceMapping must be rank-3 (device_idx, iter) + one bias mode.");

  static_assert(cute::rank(ProcessorMappingA_{}) == 2, 
      "ProcessorMappingA must be rank-2 (device_idx) + one bias mode.");
  static_assert(cute::rank(ProcessorMappingB_{}) == 2, 
      "ProcessorMappingB must be rank-2 (device_idx) + one bias mode.");
  static_assert(cute::rank(ProcessorMappingC_{}) == 2, 
      "ProcessorMappingC must be rank-2 (device_idx) + one bias mode.");
  static_assert(cute::rank(ProcessorMappingD_{}) == 2, 
      "ProcessorMappingD must be rank-2 (device_idx) + one bias mode.");

  static_assert(cute::rank(IterationMappingA_{}) == 3, 
      "IterationMappingA must be rank-3 (device_idx, iter) + one bias mode.");
  static_assert(cute::rank(IterationMappingB_{}) == 3, 
      "IterationMappingB must be rank-3 (device_idx, iter) + one bias mode.");
  static_assert(cute::rank(IterationMappingC_{}) == 3, 
      "IterationMappingC must be rank-3 (device_idx, iter) + one bias mode.");
  static_assert(cute::rank(IterationMappingD_{}) == 3, 
      "IterationMappingD must be rank-3 (device_idx, iter) + one bias mode.");

  using ProcessorTiler = ProcessorTiler_;
  using IterationTiler = IterationTiler_;

  using PeerDeviceMapping = PeerDeviceMapping_;
  using ProcessorMappingA = ProcessorMappingA_;
  using ProcessorMappingB = ProcessorMappingB_;
  using ProcessorMappingC = ProcessorMappingC_;
  using ProcessorMappingD = ProcessorMappingD_;
  using IterationMappingA = IterationMappingA_;
  using IterationMappingB = IterationMappingB_;
  using IterationMappingC = IterationMappingC_;
  using IterationMappingD = IterationMappingD_;

  static constexpr bool KernelWritesArrivalFlag = KernelWritesArrivalFlag_;
  static constexpr bool MemcpyA = MemcpyA_;
  static constexpr bool MemcpyB = MemcpyB_;
  static constexpr bool HasMemcpy = MemcpyA || MemcpyB;

  static constexpr int NumBuffersA = NumBuffersA_;
  static constexpr int NumBuffersB = NumBuffersB_;
  static constexpr int NumBuffersC = NumBuffersC_;
  static constexpr int NumBuffersD = NumBuffersD_;

  static_assert(
      NumBuffersA > 0 ^ 
      NumBuffersB > 0 ^ 
      NumBuffersC > 0 ^ 
      NumBuffersD > 0,
      "Only one of the ABCD tensors can be buffered!");

  static constexpr bool BufferedOutput = NumBuffersC > 0 || NumBuffersD > 0;
  static constexpr bool RemoteC = NumBuffersC == 0 && NumBuffersD > 0;
  static constexpr bool RemoteD = NumBuffersD == 0 && NumBuffersC > 0;

  static_assert(not RemoteD, "Remote D is not supported yet.");

  // Host-side API: can_implement based on the GLOBAL problem shape
  template <typename ProblemShape>
  static bool
  can_implement_global(ProblemShape const& global_problem_shape) {
    auto [M, N, K, L] = detail::check_problem_shape(global_problem_shape);

    auto [ptileM, ptileN, ptileK, ptileL] = ProcessorTiler{};
    auto [itileM, itileN, itileK, itileL] = IterationTiler{};

    auto tileM = ptileM * itileM;
    auto tileN = ptileN * itileN;
    auto tileK = ptileK * itileK;
    auto tileL = ptileL * itileL;

    return M % tileM == 0 && N % tileN == 0 && K % tileK == 0 && L % tileL == 0;
  }

  template <typename ProblemShape>
  CUTLASS_HOST_DEVICE
  static auto
  get_local_gemm_shape(ProblemShape const& global_problem_shape) {
    auto problem_shape_MNKL = detail::check_problem_shape(global_problem_shape);

    return shape_div(
        shape_div(
          problem_shape_MNKL,
          ProcessorTiler{}),
        IterationTiler{});
  }

  // Host-side API: determine peers
  static auto
  get_peers_for_device(int device_idx) {
    auto left_peer_id = device_idx > 0 ? device_idx - 1 : TP{} - 1;
    auto right_peer_id = device_idx < TP{} - 1 ? device_idx + 1 : 0;

    return cute::make_tuple(left_peer_id, right_peer_id);
  }

  // Determines peer given device index and iteration
  static int
  get_remote_peer_id(int device_idx, int iteration) {
    auto device_iter_to_peer_idx = PeerDeviceMapping{};
    auto peer_idx = device_iter_to_peer_idx(make_coord(device_idx, iteration, 1));
    // account for negative peer indices due to possible negative bias value, just in case cute
    // doesn't automatically handle it.
    return peer_idx >= 0 ? peer_idx % TP{} : TP{} + peer_idx;
  }

  // Construct tilers and index mappers for sharding across processors
  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_processor_tiler_a(Tensor tensor) {
    if constexpr (NumBuffersA > 0) {
      return shape_div(tensor.shape(), select<0,2,3>(IterationTiler{}));
    } else {
      return shape_div(tensor.shape(), select<0,2,3>(ProcessorTiler{}));
    }
  }

  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_processor_tiler_b(Tensor tensor) {
    if constexpr (NumBuffersB > 0) {
      return shape_div(tensor.shape(), select<1,2,3>(IterationTiler{}));
    } else {
      return shape_div(tensor.shape(), select<1,2,3>(ProcessorTiler{}));
    }
  }

  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_processor_tiler_c(Tensor tensor) {
    if constexpr (BufferedOutput) {
      return shape_div(tensor.shape(), select<0,1,3>(IterationTiler{}));
    } else {
      return shape_div(tensor.shape(), select<0,1,3>(ProcessorTiler{}));
    }
  }

  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_processor_tiler_d(Tensor tensor) {
    return get_processor_tiler_c(tensor);
  }

  // Construct tilers and index mappers for tiling and iterating on device
  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_device_tiler_a(Tensor tensor) {
    static_assert(NumBuffersA == 0, "Buffered tensors don't have device tilers!");
    return shape_div(tensor.shape(), select<0,2,3>(IterationTiler{}));
  }

  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_device_tiler_b(Tensor tensor) {
    static_assert(NumBuffersB == 0, "Buffered tensors don't have device tilers!");
    return shape_div(tensor.shape(), select<1,2,3>(IterationTiler{}));
  }

  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_device_tiler_c(Tensor tensor) {
    static_assert(NumBuffersC == 0 && NumBuffersD == 0, "Buffered tensors don't have device tilers!");
    return shape_div(tensor.shape(), select<0,1,3>(IterationTiler{}));
  }

  template <typename Tensor>
  CUTLASS_HOST_DEVICE
  static auto
  get_device_tiler_d(Tensor tensor) {
    static_assert(NumBuffersC == 0 && NumBuffersD == 0, "Buffered tensors don't have device tilers!");
    return shape_div(tensor.shape(), select<0,1,3>(IterationTiler{}));
  }

  // Map device index to processor tile coordinate (shard coordinate)
  CUTLASS_HOST_DEVICE
  static auto
  get_processor_tile_idx_a(int device_idx) {
    auto device_to_tile_idx = ProcessorMappingA{};
    return device_to_tile_idx(make_coord(device_idx, 1)) % TP{};
  }

  CUTLASS_HOST_DEVICE
  static int
  get_processor_tile_idx_b(int device_idx) {
    auto device_to_tile_idx = ProcessorMappingB{};
    return device_to_tile_idx(make_coord(device_idx, 1)) % TP{};
  }

  CUTLASS_HOST_DEVICE
  static int
  get_processor_tile_idx_c(int device_idx) {
    auto device_to_tile_idx = ProcessorMappingC{};
    return device_to_tile_idx(make_coord(device_idx, 1)) % TP{};
  }

  CUTLASS_HOST_DEVICE
  static int
  get_processor_tile_idx_d(int device_idx) {
    auto device_to_tile_idx = ProcessorMappingD{};
    return device_to_tile_idx(make_coord(device_idx, 1)) % TP{};
  }

  // Map device index and iteration to tile coordinate
  // Must be implemented by children for now.
  CUTLASS_HOST_DEVICE
  static auto
  get_device_tile_idx_a(int device_idx, int iteration) {
    auto device_iter_to_tile_idx = IterationMappingA{};
    return device_iter_to_tile_idx(make_coord(device_idx, iteration, 1)) % TP{};
  }

  CUTLASS_HOST_DEVICE
  static int
  get_device_tile_idx_b(int device_idx, int iteration) {
    auto device_iter_to_tile_idx = IterationMappingB{};
    return device_iter_to_tile_idx(make_coord(device_idx, iteration, 1)) % TP{};
  }

  CUTLASS_HOST_DEVICE
  static int
  get_device_tile_idx_c(int device_idx, int iteration) {
    auto device_iter_to_tile_idx = IterationMappingC{};
    return device_iter_to_tile_idx(make_coord(device_idx, iteration, 1)) % TP{};
  }

  CUTLASS_HOST_DEVICE
  static int
  get_device_tile_idx_d(int device_idx, int iteration) {
    auto device_iter_to_tile_idx = IterationMappingD{};
    return device_iter_to_tile_idx(make_coord(device_idx, iteration, 1)) % TP{};
  }

  // Device Partitioners: partition non-buffered processor-resident operands.
  // Processor-resident operands fall into two categories: buffered, and not buffered.
  // Those buffered aren't expected to be further partitioned, and those 
  template <typename Tensor>
  static auto
  get_tensor_A(Tensor original_tensor, void * tensor_buffer_ptr, int device_idx, int iteration) {
    static_assert(rank(original_tensor) == 3);

    using Element = typename Tensor::value_type;
    // Recreate tensor without constness. This is to ensure return types match.
    Element* ptr = const_cast<Element*>(original_tensor.data());
    auto shape = original_tensor.shape();
    auto layout = original_tensor.layout();
    auto tensor = make_tensor(ptr, layout);

    if constexpr (NumBuffersA  == 0) {
      auto tiler = get_device_tiler_a(tensor);
      auto idx = get_device_tile_idx_a(device_idx, iteration);
      return inner_partition(tensor, tiler, idx);
    } else {
      Element* ptr_buffer = reinterpret_cast<Element*>(tensor_buffer_ptr);
      if (iteration == 0) {
        return tensor;
      }
      ptr_buffer += size(shape) * (iteration - 1);

      return make_tensor(ptr_buffer, layout);
    }
  }

  template <typename Tensor>
  static auto
  get_tensor_B(Tensor original_tensor, void * tensor_buffer_ptr, int device_idx, int iteration) {
    static_assert(rank(original_tensor) == 3);

    using Element = typename Tensor::value_type;
    // Recreate tensor without constness. This is to ensure return types match.
    Element * ptr = const_cast<Element *>(original_tensor.data());
    auto shape = original_tensor.shape();
    auto layout = original_tensor.layout();
    auto tensor = make_tensor(ptr, layout);

    if constexpr (NumBuffersB  == 0) {
      auto tiler = get_device_tiler_b(tensor);
      auto idx = get_device_tile_idx_b(device_idx, iteration);
      return inner_partition(tensor, tiler, idx);
    } else {
      Element * ptr_buffer = reinterpret_cast<Element *>(tensor_buffer_ptr);
      if (iteration == 0) {
        return tensor;
      }
      ptr_buffer += size(shape) * (iteration - 1);

      return make_tensor(ptr_buffer, layout);
    }
  }

  template <typename Tensor>
  static auto
  get_tensor_C(Tensor original_tensor, void * tensor_buffer_ptr, int device_idx, int iteration) {
    static_assert(rank(original_tensor) == 3);

    using Element = typename Tensor::value_type;
    // Recreate tensor without constness. This is to ensure return types match.
    Element * ptr = const_cast<Element *>(original_tensor.data());
    auto shape = original_tensor.shape();
    auto layout = original_tensor.layout();
    auto tensor = make_tensor(ptr, layout);

    if constexpr (not BufferedOutput) {
      auto tiler = get_device_tiler_c(tensor);
      auto idx = get_device_tile_idx_c(device_idx, iteration);
      return inner_partition(tensor, tiler, idx);
    } else {
      // TODO: implement Remote D
      static_assert(RemoteC, "");

      Element * ptr_buffer = reinterpret_cast<Element *>(tensor_buffer_ptr);
      if (iteration == 0) {
        return tensor;
      }
      ptr_buffer += size(shape) * (iteration - 1);

      return make_tensor(ptr_buffer, layout);
    }
  }

  template <typename Tensor>
  static auto
  get_tensor_D(Tensor original_tensor, void * tensor_buffer_ptr, int device_idx, int iteration) {
    static_assert(rank(original_tensor) == 3);

    using Element = typename Tensor::value_type;
    // Recreate tensor without constness. This is to ensure return types match.
    Element * ptr = const_cast<Element *>(original_tensor.data());
    auto shape = original_tensor.shape();
    auto layout = original_tensor.layout();
    auto tensor = make_tensor(ptr, layout);

    if constexpr (not BufferedOutput) {
      auto tiler = get_device_tiler_d(tensor);
      auto idx = get_device_tile_idx_d(device_idx, iteration);
      return inner_partition(tensor, tiler, idx);
    } else {
      // TODO: implement Remote D
      static_assert(RemoteC, "");

      Element * ptr_buffer = reinterpret_cast<Element *>(tensor_buffer_ptr);
      // last iteration is the local tensor, the rest are buffers
      if (iteration == TP{} - 1) {
        return tensor;
      }
      ptr_buffer += size(shape) * iteration; // note: iteration, not iteration - 1

      return make_tensor(ptr_buffer, layout);
    }
  }

  template <typename ProblemShape>
  CUTLASS_HOST_DEVICE
  static auto
  get_local_a_shape(ProblemShape problem_shape) {
    auto problem_shape_MNKL = detail::check_problem_shape(problem_shape);
    if constexpr (NumBuffersA == 0) {
      return shape_div(
            select<0,2,3>(problem_shape_MNKL),
            select<0,2,3>(ProcessorTiler{}));
    } else {
      return shape_div(
          shape_div(
            select<0,2,3>(problem_shape_MNKL),
            select<0,2,3>(ProcessorTiler{})),
          select<0,2,3>(IterationTiler{}));
    }
  }

  template <typename ProblemShape>
  CUTLASS_HOST_DEVICE
  static auto
  get_local_b_shape(ProblemShape problem_shape) {
    auto problem_shape_MNKL = detail::check_problem_shape(problem_shape);
    if constexpr (NumBuffersB == 0) {
      return shape_div(
            select<1,2,3>(problem_shape_MNKL),
            select<1,2,3>(ProcessorTiler{}));
    } else {
      return shape_div(
          shape_div(
            select<1,2,3>(problem_shape_MNKL),
            select<1,2,3>(ProcessorTiler{})),
          select<1,2,3>(IterationTiler{}));
    }
  }

  template <typename ProblemShape>
  CUTLASS_HOST_DEVICE
  static auto
  get_local_c_shape(ProblemShape problem_shape) {
    auto problem_shape_MNKL = detail::check_problem_shape(problem_shape);
    if constexpr (not BufferedOutput) {
      return shape_div(
            select<0,1,3>(problem_shape_MNKL),
            select<0,1,3>(ProcessorTiler{}));
    } else {
      return shape_div(
          shape_div(
            select<0,1,3>(problem_shape_MNKL),
            select<0,1,3>(ProcessorTiler{})),
          select<0,1,3>(IterationTiler{}));
    }
  }

  template <typename ProblemShape>
  CUTLASS_HOST_DEVICE
  static auto
  get_local_d_shape(ProblemShape problem_shape) {
    auto problem_shape_MNKL = detail::check_problem_shape(problem_shape);
    if constexpr (not BufferedOutput) {
      return shape_div(
            select<0,1,3>(problem_shape_MNKL),
            select<0,1,3>(ProcessorTiler{}));
    } else {
      return shape_div(
          shape_div(
            select<0,1,3>(problem_shape_MNKL),
            select<0,1,3>(ProcessorTiler{})),
          select<0,1,3>(IterationTiler{}));
    }
  }

  // Host-side APIs: get_device_slice_{A,B,C,D}
  // Slice off a view of the GLOBAL tensor that corresponds to the shard that 
  // is going to be owned by a specific device. This helps with the initial 
  // distribution of the GLOBAL operands among devices.
  template <typename Tensor>
  static auto
  get_device_slice_A(Tensor tensor, int device_idx) {
    auto idx = get_processor_tile_idx_a(device_idx);
    auto tiler = get_processor_tiler_a(tensor);
    return inner_partition(tensor, tiler, idx);
  }

  template <typename Tensor>
  static auto
  get_device_slice_B(Tensor tensor, int device_idx) {
    auto idx = get_processor_tile_idx_b(device_idx);
    auto tiler = get_processor_tiler_b(tensor);
    return inner_partition(tensor, tiler, idx);
  }

  template <typename Tensor>
  static auto
  get_device_slice_C(Tensor tensor, int device_idx) {
    auto idx = get_processor_tile_idx_c(device_idx);
    auto tiler = get_processor_tiler_c(tensor);
    return inner_partition(tensor, tiler, idx);
  }

  template <typename Tensor>
  static auto
  get_device_slice_D(Tensor tensor, int device_idx) {
    auto idx = get_processor_tile_idx_d(device_idx);
    auto tiler = get_processor_tiler_d(tensor);
    return inner_partition(tensor, tiler, idx);
  }
};



} // namespace cutlass::gemm::distributed

///////////////////////////////////////////////////////////////////////////////

