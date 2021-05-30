//===- TensorType.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This keeps track of the tensor properties such as shape, layout and padding.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_LOWER_TENSOR_H
#define LLVM_IR_LOWER_TENSOR_H

#include "llvm/Analysis/TensorProperties.h"
#include "llvm/IR/TensorType.h"

namespace llvm {

class LowerTensorIntrinsicsLegacyPass : public FunctionPass {
public:
    static char ID;

    LowerTensorIntrinsicsLegacyPass() : FunctionPass(ID) {
        initializeLowerTensorIntrinsicsLegacyPassPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F);

    void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<TargetTransformInfoWrapperPass>();
        AU.addRequired<DominatorTreeWrapperPass>();
        AU.addPreserved<DominatorTreeWrapperPass>();
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addPreserved<LoopInfoWrapperPass>();
        AU.addRequired<TensorInfoWrapperPass>();
    }
};

}   // end of namespace llvm

#endif //LLVM_IR_LOWER_TENSOR_H


