//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Conversion/TopToTpu/LoweringBM1684X.h"

namespace tpu_mlir {
namespace bm1684x {
template <typename TyOp>
struct ShapeArithConvert : public OpRewritePattern<TyOp> {
  using OpRewritePattern<TyOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(TyOp op,
                                PatternRewriter &rewriter) const override {
    Value out = op.getOutput();
    if (isa<ReturnOp>(op))
      return failure();

    for (uint32_t idx = 0; idx < op.getNumOperands(); idx++) {
      Value opd = op.getOperand(idx);
      auto def_op = opd.getDefiningOp();
      if (!def_op || !def_op->hasTrait<trait::ShapeProducer>())
        return failure();
    }
    // Check whether the next op is d2s, if so, delete the next d2s
    auto users = op->getUsers();
    for (auto i = users.begin(); i != users.end(); ++i) {
      auto user = *i;
      if (!isa<tpu::Device2HostOp>(user)) {
        continue;
      }
      auto next_d2sOp = dyn_cast<tpu::Device2HostOp>(user);
      next_d2sOp.getOutput().replaceAllUsesWith(next_d2sOp.getInput());
      rewriter.eraseOp(next_d2sOp);
    }

    std::vector<NamedAttribute> attrs;
    std::string op_name = op.getOperationName().str();
    int pos = op_name.find("top.");
    op_name = op_name.erase(pos, 4);
    attrs.emplace_back(
        rewriter.getNamedAttr("type", rewriter.getStringAttr(op_name)));
    for (auto &attr : op->getAttrs()) {
      attrs.push_back(attr);
    }
    rewriter.replaceOpWithNewOp<tpu::ShapeArithOp>(op, out.getType(),
                                                   op.getOperands(), attrs);
    return success();
  }
};

void populateTopCfOpToTpuConversionPatterns(RewritePatternSet &patterns,
                                            TypeConverter &typeConverter,
                                            MLIRContext *ctx) {
  patterns.insert<IfOpLowering, LoopOpLowering>(typeConverter, ctx);
}

void populateTopShapeToTpuConversionPatterns(RewritePatternSet *patterns) {
  patterns->add<
      // clang-format off
      ShapeTryLowering,
      ConcatTryLowering,
      UnsqueezeTryLowering,
      SqueezeTryLowering,
      SliceTryLowering,
      MinConstTryLowering,
      MaxConstTryLowering,
      CompareConstTryLowering,
      MulTryLowering,
      DivTryLowering,
      AddConstTryLowering,
      SubConstTryLowering,
      MulConstTryLowering,
      DivConstTryLowering
      // clang-format on
      >(patterns->getContext());
  // TODO: GT LT GE LE MIN MAX SQRT ...
  patterns->add<ShapeArithConvert<top::AddOp>, ShapeArithConvert<top::SubOp>,
                ShapeArithConvert<top::MulOp>, ShapeArithConvert<top::DivOp>>(
      patterns->getContext());
}

void populateTopToTpuConversionPatterns(RewritePatternSet *patterns) {
  patterns->add<
      // clang-format off
      AbsLowering,
      AddLowering,
      ArccosLowering,
      ArctanhLowering,
      ArgLowering,
      AddConstLowering,
      AvgPoolLowering,
      CastLowering,
      CeilLowering,
      ClipLowering,
      ConcatLowering,
      ConstantFillLowering,
      ConvLowering,
      CosLowering,
      CoshLowering,
      CustomLowering,
      CumSumLowering,
      DeconvLowering,
      DeformConv2DLowering,
      DepackRawLowering,
      Depth2SpaceLowering,
      DivLowering,
      EluLowering,
      ExpLowering,
      FloorLowering,
      GatherLowering,
      GatherElementsLowering,
      GridSamplerLowering,
      GRULowering,
      GELULowering,
      IndexPutLowering,
      LeakyReluLowering,
      LogLowering,
      LogBLowering,
      LRNLowering,
      LSTMLowering,
      LutLowering,
      MatMulLowering,
      MaxLowering,
      MaxConstLowering,
      MaxPoolLowering,
      MaxPoolWithMaskLowering,
      MaxUnpoolLowering,
      MinLowering,
      MinConstLowering,
      MishLowering,
      MulLowering,
      MulConstLowering,
      NonZeroLowering,
      PadLowering,
      PermuteLowering,
      PReluLowering,
      PreprocessLowering,
      PowLowering,
      Pow2Lowering,
      ReciprocalLowering,
      ReluLowering,
      RemainderLowering,
      ReshapeLowering,
      RoiAlignLowering,
      RoundLowering,
      ScaleLowering,
      ScaleLutLowering,
      ScatterElementsLowering,
      ScatterNDLowering,
      SinLowering,
      SinhLowering,
      SigmoidLowering,
      SignLowering,
      SiLULowering,
      SliceLowering,
      SoftmaxLowering,
      SoftplusLowering,
      SoftsignLowering,
      SortLowering,
      SwapChannelLowering,
      TileLowering,
      UnsqueezeLowering,
      UpsampleLowering,
      InterpLowering,
      StridedSliceLowering,
      ReduceLowering,
      PackLowering,
      SubLowering,
      SubConstLowering,
      SqrtLowering,
      SqueezeLowering,
      SwapDimInnerLowering,
      WhereLowering,
      MaskedFillLowering,
      CompareLowering,
      CompareConstLowering,
      ErfLowering,
      HardSigmoidLowering,
      HardSwishLowering,
      LayerNormLowering,
      TanLowering,
      TanhLowering,
      TopKLowering,
      AttentionLowering,
      ReverseLowering,
      PixelNormLowering,
      YoloDetectionLowering,
      InstanceNormLowering,
      GroupNormLowering,
      DetectionOutputLowering,
      ShuffleChannelLowering,
      NmsLowering,
      RMSNormLowering,
      LayerNormTrainLowering,
      LayerNormBwdLowering,
      BatchNormTrainLowering,
      BatchNormBwdLowering,
      EmbDenseBwdLowering,
      SoftmaxBwdLowering,
      WeightReorderLowering,
      RangeLowering,
      ConvBwdWeightLowering,
      GatherNDLowering,
      TriluLowering,
      RequantIntLowering,
      DequantIntLowering,
      CopyLowering,
      RsqrtLowering,
      RequantFpLowering,
      BinaryShiftLowering,
      BinaryConstShiftLowering,
      MeanRstdLowering,
	  GroupNormTrainLowering,
      Yuv2rgbFormulaLowering,
	  LogicalAndLowering,
      MeanStdScaleLowering
      // clang-format on
      >(patterns->getContext());
}
} // namespace bm1684x
} // namespace tpu_mlir
