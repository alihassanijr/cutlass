/***************************************************************************************************
 * Copyright (c) 2017 - 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
/*! \file
    \brief Template for a double-buffered threadblock-scoped Back-to-back fused GEMM kernel.
*/

#pragma once

#include "cutlass/cutlass.h"
#include "cutlass/array.h"
#include "cutlass/aligned_buffer.h"
#include "cutlass/numeric_conversion.h"

#include "cutlass/numeric_types.h"
#include "cutlass/matrix_shape.h"

#include "cutlass/gemm/gemm.h"
#include "cutlass/gemm/warp/mma_tensor_op_fragment_iterator.h"

#include "threadblock/b2b_mma_base.h"

/////////////////////////////////////////////////////////////////////////////////////////////////

namespace cutlass {
namespace gemm {
namespace threadblock {

////////////////////////////////////////////////////////////////////////////////////////////////

/// Structure to compute the matrix product targeting CUDA cores and SIMT math instructions.
template <
  /// Size of the Gemm problem - concept: gemm::GemmShape<>
  typename Shape0_,
  /// Iterates over tiles of A operand in global memory 
  //  (concept: ReadableTileIterator | ForwardTileIterator | MaskedTileIterator)
  typename IteratorA0_,
  /// Iterates over tiles of A operand in shared memory
  /// (concept: WriteableTileIterator | RandomAccessTileIterator)
  typename SmemIteratorA0_,
  /// Iterates over tiles of B operand in global memory
  //  (concept: ReadableTileIterator | ForwardTileIterator | MaskedTileIterator)
  typename IteratorB0_,
  /// Iterates over tiles of B operand in shared memory
  /// (concept: WriteableTileIterator | RandomAccessTileIterator)
  typename SmemIteratorB0_,
  /// Size of the Gemm problem - concept: gemm::GemmShape<>
  typename Shape1_,
  /// Iterates over the intermediate accumulator tile
  //  (concept::MmaTensorOpFragmentIterator) 
  typename FragmentIteratorA1_,
  /// Iterates over vectors of scale and bias vector in global memory
  //  (concept: VectorIterator)
  typename IteratorAccumulatorScaleBias_,
  /// FragmentIterator to load Scale or Bias vector from threadblock fragment
  typename FragmentIteratorA1ScaleBias_,
  /// Iterates over tiles of B operand in global memory
  //  (concept: ReadableTileIterator | ForwardTileIterator | MaskedTileIterator)
  typename IteratorB1_,
  /// Iterates over tiles of B operand in shared memory
  /// (concept: WriteableTileIterator | RandomAccessTileIterator)
  typename SmemIteratorB1_,
  /// Data type of accumulator matrix
  typename ElementC_,
  /// Data type of accumulator matrix
  typename LayoutC_,
  /// Output operator for 1st Gemm(concept: epilogue::thread::LinearCombinationClamp, etc...) 
  typename OutputOp_,
  /// Policy describing tuning details (concept: MmaPipelinedPolicy)
  typename Policy0_,
  /// Policy describing tuning details (concept: MmaPipelinedPolicy)
  typename Policy1_,
  /// Transformation applied to A0 operand
  typename TransformA0_ = NumericArrayConverter<
    typename SmemIteratorA0_::Element, 
    typename IteratorA0_::Element, 
    IteratorA0_::Fragment::kElements>,
  ///
  /// Transformation applied to B0 operand
  typename TransformB0_ = NumericArrayConverter<
    typename SmemIteratorB0_::Element, 
    typename IteratorB0_::Element, 
    IteratorB0_::Fragment::kElements>,
  ///
  /// Transformation applied to B1 operand
  typename TransformB1_ = NumericArrayConverter<
    typename SmemIteratorB1_::Element, 
    typename IteratorB1_::Element, 
    IteratorB1_::Fragment::kElements>,
  /// Used for partial specialization
  typename Enable = bool
>
class B2bMmaPipelined : 
  public B2bMmaBase<Shape0_, Shape1_, Policy0_, Policy1_, 2> {
public:

  ///< Base class
  using Base = B2bMmaBase<Shape0_, Shape1_, Policy0_, Policy1_, 2>;

  using Shape0 = Shape0_;             ///< Size of the Gemm problem - concept: gemm::GemmShape<>
  using IteratorA0 = IteratorA0_;     ///< Iterates over tiles of A operand in global memory
  using IteratorA = IteratorA0;
  using IteratorB0 = IteratorB0_;     ///< Iterates over tiles of B operand in global memory
  using IteratorB = IteratorB0;
  using Policy0 = Policy0_;           ///< Policy describing tuning details

  using SmemIteratorA0 = SmemIteratorA0_;
  using SmemIteratorB0 = SmemIteratorB0_;

  using Shape1 = Shape1_;             ///< Size of the Gemm problem - concept: gemm::GemmShape<>
  using FragmentIteratorA1 = FragmentIteratorA1_; ///< Iterates over intermediate accumulator tile
  using IteratorAccumulatorScaleBias = IteratorAccumulatorScaleBias_;   ///< Iterates over tiles of the scale and bias vectors in global memory
  using FragmentIteratorA1ScaleBias = 
    FragmentIteratorA1ScaleBias_;     ///< WarpIterator to load Scale or Bias vector from the threadblock fragment
  using IteratorB1 = IteratorB1_;     ///< Iterates over tiles of B operand in global memory
  using Policy1 = Policy1_;           ///< Policy describing tuning details
  using Policy = Policy1;             ///< Export Policy1 as the threadblock-level Mma's policy
  using Shape = Shape1;

  using SmemIteratorB1 = SmemIteratorB1_;


  using ElementC = ElementC_;       ///< Data type of accumulator matrix
  using LayoutC = LayoutC_;         ///< Layout of accumulator matrix
  
  using OutputOp = OutputOp_;       ///< Epilogue after 1st Gemm

  static const bool PerChannelScale = (OutputOp::kScale ==
      epilogue::thread::ScaleType::OnlyAlphaPerChannelScaling);

  using TransformA0 = TransformA0_;
  using TransformB0 = TransformB0_;
  using TransformB1 = TransformB1_;

  //
  // Dependent types
  //

  /// Fragment of operand A loaded from global memory
  using FragmentA0 = typename IteratorA0::Fragment;

  /// Fragment of operand B loaded from global memory
  using FragmentB0 = typename IteratorB0::Fragment;

  /// Fragment of accumulator tile
  using FragmentC0 = typename Policy0::Operator::FragmentC;

  /// Warp-level Mma
  using Operator0 = typename Policy0::Operator;
  
  /// Fragment of Scale and Bias loaded from global memory
  using FragmentA1ScaleBias = typename IteratorAccumulatorScaleBias::Fragment;

  /// Fragment of operand B loaded from global memory
  using FragmentB1 = typename IteratorB1::Fragment;

  /// Fragment of accumulator tile
  using FragmentC1 = typename Policy1::Operator::FragmentC;

  /// Warp-level Mma
  using Operator1 = typename Policy1::Operator;
 
  /// Obtain the arch tag from the warp-level operator
  using ArchTag = typename Policy0::Operator::ArchTag;

  /// Complex transform on A0 operand
  static ComplexTransform const kTransformA0 = Operator0::kTransformA;

  /// Complex transform on B0 operand
  static ComplexTransform const kTransformB0 = Operator0::kTransformB;

  /// Complex transform on B1 operand
  static ComplexTransform const kTransformB1 = Operator1::kTransformB;

  /// Complex transform exports needed by higher-level kernels
  static ComplexTransform const kTransformA = kTransformA0;
  static ComplexTransform const kTransformB = kTransformB0;

  /// staticaly assert kStages for MmaPipelined is two (Double-buffered pipeline)
  static_assert((Base::kStages==2), "MmaPipelined requires kStages set to value 2");

private:

  using WarpFragmentA0 = typename Operator0::FragmentA;
  using WarpFragmentB0 = typename Operator0::FragmentB;
  /// Warp Fragment of operand A1 loaded from accmulator tile
  using WarpFragmentA1 = typename FragmentIteratorA1::Fragment;
  /// Warp Fragment of operand A1 scale and bias loaded from threadblock fragment
  using WarpFragmentA1ScaleBias =
      typename FragmentIteratorA1ScaleBias::Fragment;
  using WarpFragmentB1 = typename Operator1::FragmentB;

protected:

  /// Iterator to write threadblock-scoped tile of A operand to shared memory
  SmemIteratorA0 smem_iterator_A_;

  /// Iterator to write threadblock-scoped tile of B0 operand to shared memory
  SmemIteratorB0 smem_iterator_B0_;

  /// Iterator to write threadblock-scoped tile of B1 operand to shared memory
  SmemIteratorB1 smem_iterator_B1_;

public:

  /// Construct from tensor references
  CUTLASS_DEVICE
  B2bMmaPipelined(
    typename Base::B2bMmaSharedStorage &shared_storage, ///< Shared storage needed for internal use by threadblock-scoped GEMM
    int thread_idx,                                     ///< ID within the threadblock
    int warp_idx,                                       ///< ID of warp
    int lane_idx,                                       ///< ID of each thread within a warp
    int problem_size_0_n                                ///< GEMM0 N is used for accumulator extent
  ):
    Base(shared_storage, thread_idx, warp_idx, lane_idx),
    smem_iterator_A_(shared_storage.shared_storage0.operand_A_ref(), thread_idx), 
    smem_iterator_B0_(shared_storage.shared_storage0.operand_B_ref(), thread_idx),
    smem_iterator_B1_(shared_storage.shared_storage1.operand_B_ref(), thread_idx) {


    // Compute warp location within threadblock tile by mapping the warp_id to three coordinates:
    //   _m: the warp's position within the threadblock along the M dimension
    //   _n: the warp's position within the threadblock along the N dimension
    //   _k: the warp's position within the threadblock along the K dimension

    //These should stay the same across different GEMM layers
    int warp_idx_mn = warp_idx % (Base::WarpCount0::kM * Base::WarpCount0::kN);
    int warp_idx_k = warp_idx / (Base::WarpCount0::kM * Base::WarpCount0::kN);

    int warp_idx_m = warp_idx_mn % Base::WarpCount0::kM;
    int warp_idx_n = warp_idx_mn / Base::WarpCount0::kM;

    //These may change across different GEMM layers
    int tile_offset_k_0 = Base::kWarpGemmIterations0 * warp_idx_k;
    int tile_offset_k_1 = Base::kWarpGemmIterations1 * warp_idx_k;

    // Add per-warp offsets in units of warp-level tiles
    this->warp_tile_iterator_A0_.add_tile_offset({warp_idx_m, tile_offset_k_0});
    this->warp_tile_iterator_B0_.add_tile_offset({tile_offset_k_0, warp_idx_n});
    this->warp_tile_iterator_B1_.add_tile_offset({tile_offset_k_1, warp_idx_n});
  }

  /// Perform a threadblock-scoped matrix multiply-accumulate
  CUTLASS_DEVICE
  void operator()(
    int gemm_k_iterations_0,                             ///< number of iterations of the mainloop
    FragmentC1 &accum,                                   ///< destination accumulator tile
    IteratorA0 iterator_A,                               ///< iterator over A operand in global memory
    IteratorB0 iterator_B0,                              ///< iterator over B0 operand in global memory
    IteratorAccumulatorScaleBias iterator_A1_scale,    ///< iterator over A1 operand scale vectors in global memory
    IteratorAccumulatorScaleBias iterator_A1_bias,     ///< iterator over A1 operand bias vectors in global memory
    IteratorB1 iterator_B1,                              ///< iterator over B1 operand in global memory  
    FragmentC0 const &src_accum,                         ///< source accumulator tile
    OutputOp output_op_0,                                ///< epilogue operation after 1st Gemm
    TransformA0 transform_A0 = TransformA0(),            ///< transformation applied to A0 fragment
    TransformB0 transform_B0 = TransformB0(),            ///< transformation applied to B0 fragment
    TransformB1 transform_B1 = TransformB1()) {          ///< transformation applied to B1 fragment

    //
    // Prologue
    //

    // Perform accumulation in the 'd' output operand
    FragmentC0 accum0 = src_accum;

    FragmentA0 tb_frag_A;
    FragmentB0 tb_frag_B0;

    tb_frag_A.clear();
    tb_frag_B0.clear();

    // The last kblock is loaded in the prolog
    iterator_A.load(tb_frag_A);
    iterator_B0.load(tb_frag_B0);

    ++iterator_A;
    ++iterator_B0;

    this->smem_iterator_A_.store(transform_A0(tb_frag_A));
    this->smem_iterator_B0_.store(transform_B0(tb_frag_B0));

    ++this->smem_iterator_A_;
    ++this->smem_iterator_B0_;

    __syncthreads();

    // Pair of fragments used to overlap shared memory loads and math instructions
    WarpFragmentA0 warp_frag_A0[2];
    WarpFragmentB0 warp_frag_B0[2];

    this->warp_tile_iterator_A0_.set_kgroup_index(0);
    this->warp_tile_iterator_B0_.set_kgroup_index(0);

    this->warp_tile_iterator_A0_.load(warp_frag_A0[0]);
    this->warp_tile_iterator_B0_.load(warp_frag_B0[0]);

    ++this->warp_tile_iterator_A0_;
    ++this->warp_tile_iterator_B0_;

    Operator0 warp_mma0;

    int smem_write_stage_idx = 1;

    // Avoid reading out of bounds
    iterator_A.clear_mask(gemm_k_iterations_0 <= 1);
    iterator_B0.clear_mask(gemm_k_iterations_0 <= 1);

    // Issue loads during the first warp-level matrix multiply-add *AFTER* issuing 
    // shared memory loads (which have the tightest latency requirement).

    //
    // Mainloop
    //

    // Note: The main loop does not support Base::kWarpGemmIterations == 2.
    CUTLASS_GEMM_LOOP
    for (; gemm_k_iterations_0 > 0; --gemm_k_iterations_0) {
      //
      // Loop over GEMM K dimension
      //

      CUTLASS_PRAGMA_UNROLL
      for (int warp_mma_k = 0; warp_mma_k < Base::kWarpGemmIterations0; ++warp_mma_k) {

        // Load warp-level tiles from shared memory, wrapping to k offset if this is the last group
        // as the case may be.

        if (warp_mma_k == Base::kWarpGemmIterations0 - 1) {

          // Write fragments to shared memory
          this->smem_iterator_A_.store(transform_A0(tb_frag_A));

          this->smem_iterator_B0_.store(transform_B0(tb_frag_B0));

          __syncthreads();
          
          ++this->smem_iterator_A_;
          ++this->smem_iterator_B0_;

          // Add negative offsets to return iterators to the 'start' of the circular buffer in shared memory
          if (smem_write_stage_idx == 1) {
            this->smem_iterator_A_.add_tile_offset({0, -Base::kStages});
            this->smem_iterator_B0_.add_tile_offset({-Base::kStages, 0});
          }
          else {
            this->warp_tile_iterator_A0_.add_tile_offset(
                {0, -Base::kStages * Policy0::kPartitionsK * Base::kWarpGemmIterations0});
            this->warp_tile_iterator_B0_.add_tile_offset(
                {-Base::kStages * Policy0::kPartitionsK * Base::kWarpGemmIterations0,
                 0});
          }

          smem_write_stage_idx ^= 1;
        }

        this->warp_tile_iterator_A0_.set_kgroup_index((warp_mma_k + 1) % Base::kWarpGemmIterations0);
        this->warp_tile_iterator_B0_.set_kgroup_index((warp_mma_k + 1) % Base::kWarpGemmIterations0);
        
        this->warp_tile_iterator_A0_.load(warp_frag_A0[(warp_mma_k + 1) % 2]);
        this->warp_tile_iterator_B0_.load(warp_frag_B0[(warp_mma_k + 1) % 2]);

        ++this->warp_tile_iterator_A0_;
        ++this->warp_tile_iterator_B0_;

        if (warp_mma_k == 0) {

          iterator_A.load(tb_frag_A);
          iterator_B0.load(tb_frag_B0);
          ++iterator_A;
          ++iterator_B0;

          // Avoid reading out of bounds if this was the last loop iteration
          iterator_A.clear_mask(gemm_k_iterations_0 <= 2);
          iterator_B0.clear_mask(gemm_k_iterations_0 <= 2);
        }

        warp_mma0(accum0, warp_frag_A0[warp_mma_k % 2],
                  warp_frag_B0[warp_mma_k % 2], accum0);
      }
    }

    //2nd Gemm

    /// Iterator to load a warp-scoped tile of A1 operand from intermediate accumulator tile
    FragmentIteratorA1 warp_tile_iterator_A1_(accum0);

    //
    // Prologue
    //

    FragmentA1ScaleBias tb_frag_A1_scale;
    FragmentA1ScaleBias tb_frag_A1_bias;
    FragmentIteratorA1ScaleBias warp_tile_iterator_A1_scale_(tb_frag_A1_scale);
    FragmentIteratorA1ScaleBias warp_tile_iterator_A1_bias_(tb_frag_A1_bias);
    FragmentB1 tb_frag_B1;

    if(PerChannelScale)
        tb_frag_A1_scale.clear();
    tb_frag_A1_bias.clear();
    tb_frag_B1.clear();

    // The last kblock is loaded in the prolog
    if(PerChannelScale)
        iterator_A1_scale.load(tb_frag_A1_scale);
    iterator_A1_bias.load(tb_frag_A1_bias);
    iterator_B1.load(tb_frag_B1);

    if(PerChannelScale)
        ++iterator_A1_scale;
    ++iterator_A1_bias;
    ++iterator_B1;

    this->smem_iterator_B1_.store(transform_B1(tb_frag_B1));

    ++this->smem_iterator_B1_;

    __syncthreads();

    // Pair of fragments used to overlap shared memory loads and math instructions
    WarpFragmentA1ScaleBias warp_frag_A1_scale[2];
    WarpFragmentA1ScaleBias warp_frag_A1_bias[2];
    WarpFragmentA1 warp_frag_A1[2];
    WarpFragmentB1 warp_frag_B1[2];

    this->warp_tile_iterator_B1_.set_kgroup_index(0);

    if(PerChannelScale)
        warp_tile_iterator_A1_scale_.load(warp_frag_A1_scale[0]);
    warp_tile_iterator_A1_bias_.load(warp_frag_A1_bias[0]);
    warp_tile_iterator_A1_.load(warp_frag_A1[0], warp_frag_A1_scale[0],
        warp_frag_A1_bias[0], output_op_0);
    this->warp_tile_iterator_B1_.load(warp_frag_B1[0]);

    ++warp_tile_iterator_A1_;
    if(PerChannelScale)
        ++warp_tile_iterator_A1_scale_;
    ++warp_tile_iterator_A1_bias_;
    ++this->warp_tile_iterator_B1_;

    Operator1 warp_mma1;

    smem_write_stage_idx = 1;

    int gemm_k_iterations_1 = FragmentIteratorA1::Policy::kIterations / Base::kWarpGemmIterations1;

    // Avoid reading out of bounds
    iterator_B1.clear_mask(gemm_k_iterations_1 <= 1);

    //
    // Mainloop
    //

    // Note: The main loop does not support Base::WarpGemmIterations == 2.
    CUTLASS_PRAGMA_UNROLL
    for (; gemm_k_iterations_1 > 0; --gemm_k_iterations_1) {

      //
      // Loop over GEMM K dimension
      //

      CUTLASS_PRAGMA_UNROLL
      for (int warp_mma_k = 0; warp_mma_k < Base::kWarpGemmIterations1; ++warp_mma_k) {

        // Load warp-level tiles from shared memory, wrapping to k offset if this is the last group
        // as the case may be.

        if (warp_mma_k == Base::kWarpGemmIterations1 - 1) {

          // Write fragments to shared memory
          this->smem_iterator_B1_.store(transform_B1(tb_frag_B1));

          __syncthreads();
          ++this->smem_iterator_B1_;

          // Add negative offsets to return iterators to the 'start' of the circular buffer in shared memory
          if (smem_write_stage_idx == 1) {
            this->smem_iterator_B1_.add_tile_offset({-Base::kStages, 0});
          }
          else {
            this->warp_tile_iterator_B1_.add_tile_offset(
                {-Base::kStages * Policy1::kPartitionsK *
                     Base::kWarpGemmIterations1,
                 0});
          }

          smem_write_stage_idx ^= 1;

          if(PerChannelScale) {
              tb_frag_A1_scale.clear();
              iterator_A1_scale.load(tb_frag_A1_scale);
              ++iterator_A1_scale;
            }
            tb_frag_A1_bias.clear();
            iterator_A1_bias.load(tb_frag_A1_bias);
            ++iterator_A1_bias;
        }

        this->warp_tile_iterator_B1_.set_kgroup_index((warp_mma_k + 1) % Base::kWarpGemmIterations1);

        if(PerChannelScale)
          warp_tile_iterator_A1_scale_.load(warp_frag_A1_scale[(warp_mma_k + 1) % 2]);
        warp_tile_iterator_A1_bias_.load(warp_frag_A1_bias[(warp_mma_k + 1) % 2]);
        warp_tile_iterator_A1_.load(warp_frag_A1[(warp_mma_k + 1) % 2], 
            warp_frag_A1_scale[(warp_mma_k + 1) % 2], 
            warp_frag_A1_bias[(warp_mma_k + 1) % 2], 
            output_op_0);
        this->warp_tile_iterator_B1_.load(warp_frag_B1[(warp_mma_k + 1) % 2]);

        if(PerChannelScale)
          ++warp_tile_iterator_A1_scale_;
        ++warp_tile_iterator_A1_bias_;
        ++warp_tile_iterator_A1_;
        ++this->warp_tile_iterator_B1_;

        if (warp_mma_k == 0) {

          iterator_B1.load(tb_frag_B1);
          ++iterator_B1;

          // Avoid reading out of bounds if this was the last loop iteration
          iterator_B1.clear_mask(gemm_k_iterations_1 <= 2);
        }

        warp_mma1(accum, warp_frag_A1[warp_mma_k % 2], 
                  warp_frag_B1[warp_mma_k % 2], accum);
      }
    }
  }
};

/////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace threadblock
} // namespace gemm
} // namespace cutlass
