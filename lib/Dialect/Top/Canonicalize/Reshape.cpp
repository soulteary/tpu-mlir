//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Support/Module.h"
#include "tpu_mlir/Support/Patterns.h"

using namespace tpu_mlir::top;

// reshape (in == out)
struct TopFuseReshape2 : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {
    auto shape0 = module::getShape(op.getOutput());
    auto shape1 = module::getShape(op.getInput());
    if (shape0 != shape1) {
      return failure();
    }
    op.getOutput().replaceAllUsesWith(op.getInput());
    rewriter.eraseOp(op);
    return success();
  }
};

// add + reshape + add + reshape
struct TopFuseReshape3 : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {
    auto in = op.getInput();
    auto add_op = dyn_cast<AddOp>(in.getDefiningOp());
    if (!(add_op && add_op->hasOneUse() && in.hasOneUse())) {
      return failure();
    }
    if (add_op.getNumOperands() != 2) {
      return failure();
    }
    auto a_in = add_op.getInputs()[0];
    auto b_in = add_op.getInputs()[1];
    if (!module::isWeight(b_in)) {
      return failure();
    }
    if (!a_in.hasOneUse()) {
      return failure();
    }
    if (!b_in.hasOneUse()) {
      return failure();
    }
    if (!isa<ReshapeOp>(a_in.getDefiningOp())) {
      return failure();
    }
    std::vector<int64_t> shape0 = module::getShape(op.getInput());
    std::vector<int64_t> shape1 = module::getShape(op.getOutput());
    if (shape0.size() != 1 + shape1.size()) {
      return failure();
    }
    if (!std::equal(shape0.begin() + 1, shape0.end(), shape1.begin())) {
      return failure();
    }
    if (shape0[0] != 1) {
      return failure();
    }
    std::vector<int64_t> a_shape = module::getShape(a_in);
    std::vector<int64_t> b_shape = module::getShape(b_in);
    if (a_shape[0] != 1 || b_shape[0] != 1) {
      return failure();
    }
    a_shape.erase(a_shape.begin());
    b_shape.erase(b_shape.begin());
    shape0.erase(shape0.begin());
    auto b_type = RankedTensorType::get(b_shape, module::getElementType(b_in));
    b_in.setType(b_type);
    auto a_type = RankedTensorType::get(a_shape, module::getElementType(a_in));
    a_in.setType(a_type);
    auto in_type = RankedTensorType::get(shape0, module::getElementType(in));
    in.setType(in_type);
    return success();
  }
};

// reshape<(0,ng,-1)> + instance_norm -> group_norm<ng> + reshape
struct ReshapeInstanceNormPattern : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {
    // check param
    auto output = op.getOutput();
    if (!output.hasOneUse())
      return failure();
    auto next_op_ = *output.user_begin();
    if (!isa<InstanceNormOp>(next_op_))
      return failure();
    auto next_op = dyn_cast<InstanceNormOp>(next_op_);
    auto ishape = module::getShape(op.getInput());
    auto oshape = module::getShape(op.getOutput());
    if (ishape[0] != oshape[0])
      return failure();
    if (ishape[1] < oshape[1])
      return failure();
    // rewrite now !
    const auto num_groups = oshape[1];
    auto input = op.getInput();
    std::vector<NamedAttribute> attrs;
    next_op->setAttr("num_groups", rewriter.getI64IntegerAttr(num_groups));
    for (auto &attr : next_op->getAttrs()) {
      attrs.push_back(attr);
    }

    auto gn_out_type =
      RankedTensorType::get(ishape, module::getElementType(input));
    auto loc = NameLoc::get(
      rewriter.getStringAttr(module::getName(input).str() + "_GroupNorm"));

    auto groupnorm_filter_broadcast =
        [](const std::vector<int64_t> &filter_shape, const void *filter_orig,
           void *filter_trans, const int num_groups) -> void {
      int c = filter_shape[1];
      for (int kc = 0; kc < c; kc++) {
        *((float *)filter_trans + kc) =
            *((float *)filter_orig + kc / (c / num_groups));
      }
    };
    // broadcast for weight and bias
    std::vector<Value> gn_opds = {input, next_op->getOperand(1),
                                  next_op->getOperand(2)};
    int new_filter_count = ishape[1];
    auto out_type = module::getStorageType(next_op.getOutput());
    if (ishape.size() <= 2)
      return failure();
    std::vector<int64_t> new_filter_shape(ishape.size(), 1);
    new_filter_shape[1] = ishape[1];
    if (!module::isNone(next_op.getWeight())) {
      auto filterOp = next_op.getWeight().getDefiningOp<top::WeightOp>();
      auto weight_data = filterOp.read_as_byte();
      auto new_weight =
          std::make_shared<std::vector<float>>(new_filter_count, 0);
      groupnorm_filter_broadcast(new_filter_shape, weight_data->data(),
                                 new_weight->data(), num_groups);
      auto new_w_type = RankedTensorType::get(new_filter_shape, out_type);
      auto new_weightOp =
          top::WeightOp::create(next_op.getWeight().getDefiningOp(), "reorderd",
                                *new_weight, new_w_type);
      gn_opds[1] = new_weightOp;
    }

    if (!module::isNone(next_op.getBias())) {
      auto biasOp = next_op.getBias().getDefiningOp<top::WeightOp>();
      auto bias_data = biasOp.read_as_byte();
      auto new_bias = std::make_shared<std::vector<float>>(new_filter_count, 0);
      groupnorm_filter_broadcast(new_filter_shape, bias_data->data(),
                                 new_bias->data(), num_groups);
      auto new_b_type = RankedTensorType::get(new_filter_shape, out_type);
      auto new_biasOp = top::WeightOp::create(
          next_op.getBias().getDefiningOp(), "reorderd", *new_bias, new_b_type);
      gn_opds[2] = new_biasOp;
    }

    Value insertpoint = next_op.getOutput();
    rewriter.setInsertionPointAfterValue(insertpoint);

    auto gn_op = rewriter.create<GroupNormOp>(
      loc, gn_out_type, gn_opds, attrs);
    rewriter.replaceOp(op, gn_op);
    auto gn_output = gn_op.getOutput();
    rewriter.setInsertionPointAfterValue(gn_output);
    auto new_reshape_out_type = next_op.getResult().getType();
    rewriter.replaceOpWithNewOp<ReshapeOp>(
      next_op, new_reshape_out_type, gn_output, std::vector<NamedAttribute>());
    return success();
  }
};

// merge some tanh and power(x,3) comprised gelu to gelu, first found in pytorch traced gpt2
struct MergeGeluPattern : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {
    MulOp mul_op = dyn_cast<MulOp>(op.getInput().getDefiningOp());
    if (mul_op == NULL || !mul_op.getOutput().hasOneUse())
      return failure();

    MulConstOp mulconst_op = NULL;
    AddConstOp addconst_op = NULL;

    for (auto in : mul_op.getInputs()) {
      if (isa<MulConstOp>(in.getDefiningOp()))
        mulconst_op = dyn_cast<MulConstOp>(in.getDefiningOp());
      else if (isa<AddConstOp>(in.getDefiningOp()))
        addconst_op = dyn_cast<AddConstOp>(in.getDefiningOp());
      else
        return failure();
    }
    if (!mulconst_op.getOutput().hasOneUse() || !addconst_op.getOutput().hasOneUse())
      return failure();

    TanhOp tanh_op = NULL;
    if (!isa<TanhOp>(addconst_op.getInput().getDefiningOp()))
      return failure();
    else
      tanh_op = dyn_cast<TanhOp>(addconst_op.getInput().getDefiningOp());
    if (!tanh_op.getOutput().hasOneUse())
      return failure();

    MulConstOp mulconst_op1 = NULL;
    AddOp add_op = NULL;
    if (!isa<MulConstOp>(tanh_op.getInput().getDefiningOp()))
      return failure();
    else
      mulconst_op1 = dyn_cast<MulConstOp>(tanh_op.getInput().getDefiningOp());
    if (!isa<AddOp>(mulconst_op1.getInput().getDefiningOp()))
      return failure();
    else
      add_op = dyn_cast<AddOp>(mulconst_op1.getInput().getDefiningOp());
    if (!mulconst_op1.getOutput().hasOneUse() || !add_op.getOutput().hasOneUse())
      return failure();

    MulConstOp mulconst_op2 = NULL;
    PowOp pow_op = NULL;
    ReshapeOp reshape_op = NULL;
    for (auto in : add_op.getInputs()) {
      if (isa<MulConstOp>(in.getDefiningOp()))
        mulconst_op2 = dyn_cast<MulConstOp>(in.getDefiningOp());
      else if (isa<ReshapeOp>(in.getDefiningOp()))
        reshape_op = dyn_cast<ReshapeOp>(in.getDefiningOp());
      else
        return failure();
    }
    if (!isa<PowOp>(mulconst_op2.getInput().getDefiningOp()))
      return failure();
    else
        pow_op = dyn_cast<PowOp>(mulconst_op2.getInput().getDefiningOp());
    if (!mulconst_op2.getOutput().hasOneUse() || !pow_op.getOutput().hasOneUse())
      return failure();

    if (pow_op.getInput().getDefiningOp() != reshape_op || mulconst_op.getInput().getDefiningOp() != reshape_op)
      return failure();
    int cnt = 0;
    int all = 0;
    for (auto out : reshape_op.getOutput().getUsers()) {
      if (out == mulconst_op || out == pow_op || out == add_op)
        cnt++;
      all++;
    }
    if (cnt != 3 || all != 3)
      return failure();
    if (pow_op.getExponent().convertToDouble() != 3.0 || fabs(mulconst_op2.getConstVal().convertToDouble()-0.044714998453855515)>1e-4 ||
        addconst_op.getConstVal().convertToDouble() != 1.0 || fabs(mulconst_op1.getConstVal().convertToDouble()-0.79788458347320556)>1e-4 ||
        fabs(mulconst_op.getConstVal().convertToDouble()-0.5)>1e-4)
      return failure();
    rewriter.replaceOpWithNewOp<GELUOp>(op, op.getResult().getType(),
             ValueRange{reshape_op.getInput()});
    return success();
  }
};

/**
 * Op1 -> reshape -> next  => Op1 -> next -> reshape
 * copied from Permute.cpp
 **/
struct ReshapeMovePattern : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {
    // check topo
    // have one user only
    if (!op.getOutput().hasOneUse()) {
      return failure();
    }
    // move trait
    auto nextOp = *op.getOutput().user_begin();
    // ops that support permute move should also support reshape move
    if (!nextOp->hasTrait<trait::SupportPermuteMove>()) {
      return failure();
    }
    // permute only accept one argument
    // thus the output of 'next' should be exactly one
    // otherwise, we need to construct new permutation op
    if (nextOp->getResults().size() != 1) {
      return failure();
    }

    // rewrite
    auto input = op.getInput();
    auto inputShape = module::getShape(input);
    auto outputType = nextOp->getResult(0).getType();
    // input -> next
    rewriter.updateRootInPlace(nextOp, [&] {
      nextOp->setOperands(input);
      // should be the same type as the input
      module::setShape(nextOp->getResult(0), inputShape);
      // rewrite loc for tests
      auto loc = NameLoc::get(
          rewriter.getStringAttr(module::getName(input).str() + "_" +
                                 nextOp->getName().getStringRef()));
      nextOp->setLoc(loc);
    });
    // replace all uses of next to perm
    rewriter.replaceAllUsesWith(nextOp->getResult(0), op->getResult(0));
    // next -> perm
    rewriter.updateRootInPlace(op, [&] {
      std::vector<Value> operands;
      operands.push_back(nextOp->getResult(0));
      if (op.getShapeT()) {
        operands.push_back(op.getShapeT());
      }
      op->setOperands(operands);
      op->getResult(0).setType(outputType);
      // linear IR, tweak order
      op->moveAfter(nextOp);
      // rewrite loc for tests
      auto loc = NameLoc::get(
          rewriter.getStringAttr(module::getName(nextOp).str() + "_" +
                                 op->getName().getStringRef()));
      op->setLoc(loc);
    });
    return success();
  }
};

/**
 * Reshape(tensor<1xf32>) -> tensor<f32>
 * Unsqueeze(tensor<f32>) -> tensor<1xf32>
 **/
struct InValidReshapeMergePattern : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {
    // check topo
    // have one user only
    if (!op.getOutput().hasOneUse()) {
      return failure();
    }

    auto shape = module::getShape(op.getOutput());
    if (shape.size() > 0) {
      return failure();
    }

    // move trait
    for (auto nextOp : op.getResult().getUsers()) {
      // ops that support permute move should also support reshape move
      if (auto unsqueezeOp = dyn_cast<top::UnsqueezeOp>(nextOp)) {
        unsqueezeOp.replaceAllUsesWith(op.getInput());
        rewriter.eraseOp(nextOp);
      } else {
        llvm_unreachable("not supported this situation!");
      }
      // if (!isa<top::UnsqueezeOp>(nextOp)) {
      // }
    }

    rewriter.eraseOp(op);

    return success();
  }
};

//  Do:
//     A                                          A + Reshape
//       + Add + Reshape + LayerNorm/Matmul -->>              + Add + LayerNorm/Matmul
//     B                                          B + Reshape
// swint
struct TopAddReshapeSwap : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {
    auto storage_type = module::getStorageType(op.getOutput());
    if (!storage_type.isF32() && !storage_type.isF16()) {
      return failure();
    }
    auto in = op.getInput();
    auto add_op = dyn_cast<AddOp>(in.getDefiningOp());
    if (!add_op || !add_op.getOutput().hasOneUse()) {
      return failure();
    }
    bool add_can_merge = false;
    for (auto nextOp : op.getOutput().getUsers()) {
      if (isa<LayerNormOp, MatMulOp>(nextOp)) {
        add_can_merge = true;
        break;
      }
    }
    if (!add_can_merge) {return failure();}
    auto add_out_elements = module::getNumElements(add_op.getOutput());
    for (auto add_in : add_op.getInputs()) {
      if (add_in.hasOneUse() && isa<LayerNormOp, MatMulOp>(add_in.getDefiningOp())) {
        return failure();
      }
      auto add_in_elements = module::getNumElements(add_in);
      if (add_in_elements != add_out_elements) {
        return failure();
      }
    }

    // fix bug for qwen
    auto in_shape = module::getShape(op.getInput());
    auto out_shape = module::getShape(op.getOutput());
    if (in_shape.size() == 4 && out_shape.size() == 4 && in_shape[0] == 1 &&
        in_shape[1] == 1 && out_shape[0] == 1 && out_shape[2] == 1) {
      return failure();
    }

    std::vector<Value> operands;
    for (auto add_in : add_op.getInputs()) {
      std::string in_name = module::getName(add_in).str() + "_reshape";
      auto loc = NameLoc::get(rewriter.getStringAttr(in_name));
      rewriter.setInsertionPoint(add_op);
      auto reshape_op = rewriter.create<ReshapeOp>(loc, op.getOutput().getType(), ValueRange{add_in});
      operands.push_back(reshape_op);
    }
    rewriter.replaceOpWithNewOp<AddOp>(op, op.getType(), operands,
                                       add_op->getAttrs());
    rewriter.eraseOp(add_op);
    return success();
  }
};

// Reshape + Reshape -->> Reshape
// swint
struct TopReshapeFuse : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {

    auto in = op.getInput();
    auto pre_op = dyn_cast<ReshapeOp>(in.getDefiningOp());
    if (!pre_op) {
      return failure();
    }
    if (!in.hasOneUse()) {
      return failure();
    }
    op.setOperand(0, pre_op.getInput());
    rewriter.eraseOp(pre_op);
    return success();
  }
};

//           OP            Reshape + Op
// Reshape + Reshape  -->> Reshape + Reshape
struct TopReshapeFuse2 : public OpRewritePattern<ReshapeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ReshapeOp op,
                                PatternRewriter &rewriter) const override {

    auto in = op.getInput();
    auto pre_op = dyn_cast<ReshapeOp>(in.getDefiningOp());
    if (!pre_op) {
      return failure();
    }
    if (in.hasOneUse()) {
      return failure();
    }
    auto shape0 = module::getShape(op.getOutput());
    auto shape1 = module::getShape(pre_op.getInput());
    if (shape0 != shape1) {
      return failure();
    }
    int32_t index = 0;
    for (auto nextOp : pre_op.getResult().getUsers()) {
      std::string in_name = module::getName(in).str() + "_" + std::to_string(index++);
      auto loc = NameLoc::get(rewriter.getStringAttr(in_name));
      rewriter.setInsertionPoint(pre_op);
      auto reshape_op = rewriter.create<ReshapeOp>(loc, pre_op.getOutput().getType(), ValueRange{pre_op.getInput()});
      nextOp->setOperand(0, reshape_op.getOutput());
    }
    // rewriter.eraseOp(pre_op);
    return success();
  }
};

void ReshapeOp::getCanonicalizationPatterns(RewritePatternSet &results,
                                            MLIRContext *context) {
  results.insert<patterns::FuseRepeatPattern<top::ReshapeOp>, TopFuseReshape2,
                 TopFuseReshape3, ReshapeInstanceNormPattern, MergeGeluPattern,
                 ReshapeMovePattern, InValidReshapeMergePattern,
                 TopAddReshapeSwap, TopReshapeFuse, TopReshapeFuse2>(context);
}

OpFoldResult ReshapeOp::fold(FoldAdaptor adaptor) {
  Operation *op = *this;
  auto weightOp = op->getOperand(0).getDefiningOp<top::WeightOp>();
  if (weightOp) {
    if (!weightOp.getOperation()->hasOneUse()) {
      return {};
    }
    auto data = weightOp.read_as_float();
    auto shape = module::getShape(this->getOutput());
    auto storage_type = module::getStorageType(getOutput());
    auto new_op =
        WeightOp::create_float(weightOp.getOperation(), "folder", *data,
                               shape.vec(), storage_type);
    return new_op;
  } else {
    return {};
  }
}
