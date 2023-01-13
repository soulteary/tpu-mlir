//===----------------------------------------------------------------------===//
//
// Copyright (c) 2020-2030 by Sophgo Technologies Inc. All rights reserved.
//
// Licensed under the Apache License v2.0.
// See http://www.apache.org/licenses/LICENSE-2.0 for license information.
// SPDX-License-Identifier: Apache-2.0
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Conversion/TopToTpu/LoweringCV18xx.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "lowering-slice"
namespace tpu_mlir {
namespace cv18xx {
void SliceLowering::LoweringINT8(PatternRewriter &rewriter, top::SliceOp op,
                                bool asymmetric) const {
  lowering_common_int8<tpu::SliceOp>(rewriter, op, asymmetric);
}

void SliceLowering::LoweringBF16(PatternRewriter &rewriter,
                                top::SliceOp op) const {
  auto out = op.getOutput();
  if (module::isCalibratedType(out)) {
    //For fuse_preprocess(crop image) use, it should be lowered to uint8.
    auto qtype = module::getCalibratedType(out);
    auto max = qtype.getMax();
    auto min = qtype.getMin();
    if (min == 0 && max == 255) {
      lowering_common_int8<tpu::SliceOp>(rewriter, op, false);
      return;
    }
  }
  lowering_common_bf16<tpu::SliceOp>(rewriter, op);
}

} // namespace cv18xx
} // namespace tpu_mlir
