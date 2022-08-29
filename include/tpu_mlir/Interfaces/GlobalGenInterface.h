//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Operation.h"

#include "tpu_mlir/Support/Helper/Module.h"
using namespace tpu_mlir::helper;

/// Include the ODS generated interface header files.
#include "tpu_mlir/Interfaces/GlobalGenInterface.h.inc"
