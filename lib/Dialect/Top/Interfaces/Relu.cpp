//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Dialect/Top/IR/TopOps.h"
#include "tpu_mlir/Support/Dnnl/Dnnl.h"
#include "tpu_mlir/Support/Helper/Module.h"
#include "tpu_mlir/Support/MathUtils.h"

using namespace tpu_mlir;
using namespace tpu_mlir::helper;
using namespace mlir;

int64_t top::ReluOp::getFLOPs() { return Module::getNumElements(output()); }

LogicalResult top::ReluOp::init(InferenceParameter &p) { return success(); }
void top::ReluOp::deinit(InferenceParameter &p) {}

LogicalResult top::ReluOp::inference(InferenceParameter &p) {
  auto limit = relu_limit().convertToDouble();
  function_relu(p.inputs[0], p.outputs[0], Module::getNumElements(input()),
                limit);
  return success();
}
