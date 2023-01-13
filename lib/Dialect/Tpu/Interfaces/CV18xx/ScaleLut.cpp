//===----------------------------------------------------------------------===//
//
// Copyright (c) 2020-2030 by Sophgo Technologies Inc. All rights reserved.
//
// Licensed under the Apache License v2.0.
// See http://www.apache.org/licenses/LICENSE-2.0 for license information.
// SPDX-License-Identifier: Apache-2.0
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Backend/CV18xx/CV18xx.h"
#include "tpu_mlir/Backend/CV18xx/CV18xx_global_api.h"
#include "tpu_mlir/Dialect/Tpu/IR/TpuOps.h"
#include "tpu_mlir/Support/Module.h"




using namespace tpu_mlir::backend;
// =========================================
// GlobalGenInterface
// =========================================

void tpu::ScaleLutOp::codegen_global_cv18xx(int64_t layer_id) {

  //ScaleLutOp only support int8
  assert(module::isUniformQuantized(getOutput()));
  int64_t n, c, h, w;
  module::getNCHW(getOutput(), n, c, h, w);
  gaddr_t input_gaddr = module::getAddress(getInput());
  gaddr_t table_gaddr = module::getAddress(getTable());
  gaddr_t output_gaddr = module::getAddress(getOutput());
  cvi_backend_tg_scale_lut_kernel(layer_id, // layer_id,
                                  input_gaddr, output_gaddr, table_gaddr, n, c,
                                  h, w, CVK_FMT_I8);
}

// =========================================
// LocalGenInterface
// =========================================

int64_t tpu::ScaleLutOp::getBufferSize_cv18xx(int64_t in_lmem_bytes,
                                         int64_t out_lmem_bytes,
                                         int64_t in_nslice, int64_t in_hslice,
                                         int64_t out_nslice,
                                         int64_t out_hslice) {
  llvm_unreachable("Not supported now");
}

void tpu::ScaleLutOp::codegen_local_cv18xx(int64_t n_step, int64_t h_step, int64_t layer_id) {
  llvm_unreachable("Not supported now");
}
