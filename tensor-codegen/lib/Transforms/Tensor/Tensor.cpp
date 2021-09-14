//===-------------------------- Tensor.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pass to convert tensor function calls into tensor intrinsics for tensor
// type information and tensor operations.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PostOrderIterator.h"

#include <vector>

using namespace llvm;

#define DEBUG_TYPE "tensor"

namespace {
  struct TensorPass : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    TensorPass() : ModulePass(ID) {}

private:
  bool runOnFunction(Function &F);

  void getAnalysisUsage(AnalysisUsage &AU) const {}

public:
  bool runOnModule(Module &M) override {
    bool Changed = false;
    for(auto &F : M) {
      Changed |= runOnFunction(F);
    }
    return Changed;
  }

};
}

static Function *getIntrinsicDeclaration(CallInst *CI, std::vector<Type *> &ArgsTy,
                                      DenseMap<Value *, Value *> &FakeTypeToTokenTypeVal) {
  auto *M = CI->getModule();
  StringRef CalledFuncName = CI->getCalledFunction()->getName();

  // Account for typeinfo call
  if(CalledFuncName.contains(StringRef("tensor_typeinfo"))) {
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_typeinfo,
                        ArrayRef<Type *>(ArgsTy));
   }

   // Account for element-wise tensor op
   if(CalledFuncName.contains(StringRef("tensor_relu"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_relu,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_tanh"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_tanh,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_sigmoid"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_sigmoid,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_sin"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_sin,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_cos"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_cos,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_exp"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_exp,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_exp2"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_exp2,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_log"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_log,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_log2"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_log2,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_log10"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_log10,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_fabs"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_fabs,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_floor"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_floor,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_ceil"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_ceil,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_sqrt"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_sqrt,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_broadcast"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_broadcast,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }

   // Account for reduction operation
   if(CalledFuncName.contains(StringRef("tensor_reduce_max"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_reduce_max,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType(),
                              CI->getOperand(0)->getType(),
                              CI->getOperand(1)->getType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_reduce_min"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_reduce_min,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType(),
                              CI->getOperand(0)->getType(),
                              CI->getOperand(1)->getType()}));
   }
   if(CalledFuncName.contains(StringRef("tensor_reduce_and"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_reduce_and,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType(),
                              CI->getOperand(0)->getType(),
                              CI->getOperand(1)->getType()}));
   }
  if(CalledFuncName.contains(StringRef("tensor_reduce_or"))) {
    auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
    assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
    return Intrinsic::getDeclaration(M, Intrinsic::tensor_reduce_or,
            ArrayRef<Type*>({CI->getCalledFunction()->getReturnType(),
                            CI->getOperand(0)->getType(),
                            CI->getOperand(1)->getType()}));
  }
  if(CalledFuncName.contains(StringRef("tensor_reduce_xor"))) {
    auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
    assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
    return Intrinsic::getDeclaration(M, Intrinsic::tensor_reduce_xor,
            ArrayRef<Type*>({CI->getCalledFunction()->getReturnType(),
                            CI->getOperand(0)->getType(),
                            CI->getOperand(1)->getType()}));
  }
  if(CalledFuncName.contains(StringRef("tensor_reduce_add"))) {
    auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
    assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
    return Intrinsic::getDeclaration(M, Intrinsic::tensor_reduce_add,
            ArrayRef<Type*>({CI->getCalledFunction()->getReturnType(),
                            CI->getOperand(0)->getType(),
                            CI->getOperand(1)->getType()}));
  }
  if(CalledFuncName.contains(StringRef("tensor_reduce_mul"))) {
    auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
    assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
    return Intrinsic::getDeclaration(M, Intrinsic::tensor_reduce_mul,
            ArrayRef<Type*>({CI->getCalledFunction()->getReturnType(),
                            CI->getOperand(0)->getType(),
                            CI->getOperand(1)->getType()}));
  }

  // Transpose
   if(CalledFuncName.contains(StringRef("tensor_transpose"))) {
     auto *TypeInfoIntrinsic = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic) && "Tensor ops' operands must come from typeinfo");
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_transpose,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }

   // Account for matmul call
   if(CalledFuncName.contains(StringRef("tensor_matmul"))) {
     auto *TypeInfoIntrinsic1 = FakeTypeToTokenTypeVal[CI->getArgOperand(0)];
     auto *TypeInfoIntrinsic2 = FakeTypeToTokenTypeVal[CI->getArgOperand(1)];
     assert(dyn_cast<CallInst>(TypeInfoIntrinsic1) && dyn_cast<CallInst>(TypeInfoIntrinsic2)
         && "Tensor ops' operands must come from typeinfo");
     errs() << "DEALING WITH MATMUL\n";
     return Intrinsic::getDeclaration(M, Intrinsic::tensor_matmul,
              ArrayRef<Type*>({CI->getCalledFunction()->getReturnType()}));
   }

   return nullptr;
}

static bool isTensorCall(CallInst *CI) {
  StringRef CalledFuncName = CI->getCalledFunction()->getName();
  return (CalledFuncName.contains(StringRef("tensor_typeinfo"))
       || CalledFuncName.contains(StringRef("tensor_relu"))
       || CalledFuncName.contains(StringRef("tensor_tanh"))
       || CalledFuncName.contains(StringRef("tensor_sigmoid"))
       || CalledFuncName.contains(StringRef("tensor_sin"))
       || CalledFuncName.contains(StringRef("tensor_cos"))
       || CalledFuncName.contains(StringRef("tensor_exp"))
       || CalledFuncName.contains(StringRef("tensor_exp2"))
       || CalledFuncName.contains(StringRef("tensor_log"))
       || CalledFuncName.contains(StringRef("tensor_log2"))
       || CalledFuncName.contains(StringRef("tensor_log10"))
       || CalledFuncName.contains(StringRef("tensor_sqrt"))
       || CalledFuncName.contains(StringRef("tensor_fabs"))
       || CalledFuncName.contains(StringRef("tensor_floor"))
       || CalledFuncName.contains(StringRef("tensor_ceil"))
       || CalledFuncName.contains(StringRef("tensor_matmul"))
       || CalledFuncName.contains(StringRef("tensor_broadcast"))
       || CalledFuncName.contains(StringRef("tensor_transpose"))
       || CalledFuncName.contains(StringRef("tensor_reduce_max"))
       || CalledFuncName.contains(StringRef("tensor_reduce_min"))
       || CalledFuncName.contains(StringRef("tensor_reduce_and"))
       || CalledFuncName.contains(StringRef("tensor_reduce_or"))
       || CalledFuncName.contains(StringRef("tensor_reduce_xor"))
       || CalledFuncName.contains(StringRef("tensor_reduce_add"))
       || CalledFuncName.contains(StringRef("tensor_reduce_mul"))
       );

}

static bool isLLVMTensorInstruction(Instruction *I) {
  if(dyn_cast<UnaryOperator>(I) || dyn_cast<BinaryOperator>(I)
  || dyn_cast<SelectInst>(I) || dyn_cast<CmpInst>(I)) {
    return true;
  }
  return false;
}

static bool isTensorValuePHI(PHINode *PHI) {
  // The value operands must be vector type
  if(PHI->getIncomingValue(0)->getType()->isVectorTy()) {
    // Weed out the static shape, layout and padding cases.
    // If the incoming vector values are constant, skip them.
    for(unsigned i = 0; i < PHI->getNumOperands(); i++) {
      if(!dyn_cast<ConstantVector>(PHI->getIncomingValue(i))) {
        return true; 
      }
    }
  }
  return false;
}

static bool isTensorTokenPHI(PHINode *PHI) {
  return false;
  // The value operands must be vector type
  if(PHI->getIncomingValue(0)->getType()->isIntegerTy()) {
    // Weed out the static shape, layout and padding cases.
    // If the incoming vector values are constant, skip them.
    for(unsigned i = 0; i < PHI->getNumOperands(); i++) {
      if(!dyn_cast<ConstantVector>(PHI->getIncomingValue(i))) {
        return true; 
      }
    }
  }
  return false;
}

static bool isTensorPHI(PHINode *PHI) {
  return (isTensorTokenPHI(PHI) || isTensorValuePHI(PHI));
}

static bool isTensorInstruction(Instruction *I) {
  if(auto *CI = dyn_cast<CallInst>(I)) {
    return isTensorCall(CI);
  }

  if(auto *PHI = dyn_cast<PHINode>(I)) {
    return isTensorPHI(PHI);
  }
  
  return isLLVMTensorInstruction(I);
}

static SmallVector<Value *, 3> getPropertyInfoForTensorPHI(PHINode *PHI, 
                        DenseMap<Value *, SmallVector<Value *, 3>> &ValToPropertyMap) {
  errs() << "GET PROPERTY INFO FOR TENSOR PHI\n";
  // Now that we have to find this shape, layout and padding info
  // in some typeinfo.

  // Find a uses of this instruction in a typeinfo instuction
  SmallVector<Instruction *, 4> Worklist;
  Worklist.push_back(PHI);
  while(!Worklist.empty()) {
    auto *Inst = Worklist.back();
    Worklist.pop_back();
    for(auto *User : Inst->users()) {
      auto *I = dyn_cast<Instruction>(User);
      errs() << "USER: " << *I << "\n";

      if(auto *CI = dyn_cast<CallInst>(I)) {
        errs() << "CALL INSTRUCTION: " << *CI << "\n";
        const StringRef &CalledFuncName = CI->getCalledFunction()->getName();

        // Account for typeinfo call
        if(CalledFuncName.contains(StringRef("tensor_typeinfo"))) {
          SmallVector<Value *, 3> PropertyArray;
          PropertyArray.push_back(CI->getArgOperand(1));
          PropertyArray.push_back(CI->getArgOperand(2));
          PropertyArray.push_back(CI->getArgOperand(3));
          return PropertyArray;
        }

        // Account for element-wise tensor op
        if(CalledFuncName.contains(StringRef("tensor_relu"))
        || CalledFuncName.contains(StringRef("tensor_tanh"))
        || CalledFuncName.contains(StringRef("tensor_sigmoid"))
        || CalledFuncName.contains(StringRef("tensor_sin"))
        || CalledFuncName.contains(StringRef("tensor_cos"))
        || CalledFuncName.contains(StringRef("tensor_exp"))
        || CalledFuncName.contains(StringRef("tensor_exp2"))
        || CalledFuncName.contains(StringRef("tensor_log"))
        || CalledFuncName.contains(StringRef("tensor_log2"))
        || CalledFuncName.contains(StringRef("tensor_log10"))
        || CalledFuncName.contains(StringRef("tensor_sqrt"))
        || CalledFuncName.contains(StringRef("tensor_fabs"))
        || CalledFuncName.contains(StringRef("tensor_floor"))
        || CalledFuncName.contains(StringRef("tensor_ceil"))
        || CalledFuncName.contains(StringRef("tensor_broadcast"))) {
          Worklist.push_back(CI);
          continue;
        }

        if(CalledFuncName.contains(StringRef("tensor_matmul"))
        || CalledFuncName.contains(StringRef("tensor_transpose"))
        || CalledFuncName.contains(StringRef("tensor_reduce_max"))
        || CalledFuncName.contains(StringRef("tensor_reduce_min"))
        || CalledFuncName.contains(StringRef("tensor_reduce_and"))
        || CalledFuncName.contains(StringRef("tensor_reduce_or"))
        || CalledFuncName.contains(StringRef("tensor_reduce_xor"))
        || CalledFuncName.contains(StringRef("tensor_reduce_add"))
        || CalledFuncName.contains(StringRef("tensor_reduce_mul"))) {
          // We use the map information of PHI's operands. 
          // We are keeping things simple here for now.
          // The assumption here is that the PHI's operands
          // have the same tensor properties.
          return ValToPropertyMap[PHI->getIncomingValue(0)];
        }
      }

      if(auto *PHIInst = dyn_cast<PHINode>(I)) {
        errs() << "PHI INSTRUCTION\n";
        assert(isTensorPHI(PHIInst) && "Should be a tensor PHI node");
        return getPropertyInfoForTensorPHI(PHIInst, ValToPropertyMap);
      }

      if(isLLVMTensorInstruction(I)) {
        Worklist.push_back(I);
        continue;
      }

      if(dyn_cast<ReturnInst>(I)) {
        continue;
      }
      
      assert(false && "Cannot reach here");
    }
  }

  // Return an empty vector
  return SmallVector<Value *, 3>();
}

static bool addTypeInfoAfterTensorPHI(PHINode *PHI,
                          DenseMap<Value *, SmallVector<Value *, 3>> &ValToPropertyMap)  {
  errs() << "PHI INSTRUCTION: " << *PHI << "\n";
  SmallVector<Value *, 3> TensorProperties = getPropertyInfoForTensorPHI(PHI, ValToPropertyMap);
  if(TensorProperties.empty()) {
    return false;
  }

  // Now we have to deal with trying to get the tensor value for this "token" PHI node.
  // Get the PHI operand first. It must be coming from typeinfo.
  SmallVector<Instruction *, 4> TensorValues;
  SmallVector<BasicBlock *, 4> TensorBlocks;
  for(unsigned i = 0; i < PHI->getNumOperands(); i++) {
    auto *I = dyn_cast<Instruction>(dyn_cast<Instruction>(PHI->getIncomingValue(i))->getOperand(0));
    TensorValues.push_back(I);
    TensorBlocks.push_back(PHI->getIncomingBlock(i));
  }

  // Go through all the PHIs in this basic block and see if a PHI node has all the
  // incoming tensor values.
  Instruction *TensorValue = nullptr;
  for(auto &PHIRef : PHI->getParent()->phis()) {
    PHINode *PHIInst = dyn_cast<PHINode>(&PHIRef);
    errs() << "CONSIDERING PHI: " << *PHIInst << "\n";
    if(isTensorValuePHI(PHIInst)) {
      unsigned i = 0;
      for(; i < PHIInst->getNumOperands(); i++) {
        if(find(TensorValues, PHIInst->getIncomingValue(i)) == TensorValues.end()) {
          errs() << "NOT THE PHI\n";
          break;
        }
      }
      if(i == PHIInst->getNumOperands()) {
        TensorValue = PHIInst;
        break;
      }
    }
  }

  if(!TensorValue) {
    if(TensorValues.size() > 1) {
      // No PHI node found, so we will have to add one to the IR.
      PHINode *TensorPHI = PHINode::Create(TensorValues[0]->getType(), PHI->getNumIncomingValues(),
                                  "", PHI->getParent()->getFirstNonPHI());

      for(unsigned i = 0; i < PHI->getNumIncomingValues(); i++) {
        TensorPHI->addIncoming(TensorValues[i], TensorBlocks[i]);
      }
      TensorValue = TensorPHI;
    } else {
      // There is no need for a PHI node.
      TensorValue = TensorValues[0];
    }
  }
  ValToPropertyMap[TensorValue] = TensorProperties;
  errs() << "TENSOR INSTRUCTION INSERTED: " << *TensorValue << "\n";

  std::vector<Type *> ArgsTy = {TensorValue->getType(), TensorProperties[0]->getType(),
                    TensorProperties[1]->getType(), TensorProperties[2]->getType()};
  std::vector<Value *> Args = {TensorValue, TensorProperties[0], TensorProperties[1], TensorProperties[2]};
  auto *TypeInfo = TensorValue->getModule()->getFunction("tensor_typeinfo");
  auto *CI = CallInst::Create(TypeInfo->getFunctionType(), TypeInfo, ArrayRef<Value *>(Args), 
                         "", TensorValue->getParent()->getFirstNonPHI());
  errs() << "CALL INSTRUCTION INSERTED: " << *CI << "\n";
  
  // Replace uses of PHI instruction with this new call yet.
  PHI->replaceAllUsesWith(CI);

  ValToPropertyMap[CI] = TensorProperties;

  errs() << "PRINTING FUNCTION: \n";
  errs() << *(PHI->getParent()->getParent());

  return true;
}

SmallVector<Value *, 3> getMatMulOuputProperties(LLVMContext &Ctx,
                                                 SmallVector<Value *, 3> &Input1, 
                                                 SmallVector<Value *, 3> &Input2) {

    auto getShapeDimensionVal = [](Value *Shape, unsigned Index) {
      if(auto *CV = dyn_cast<ConstantVector>(Shape)) {
        auto *C = CV->getAggregateElement(Index);
        return dyn_cast<ConstantInt>(C)->getZExtValue();
      }
    };

    // Get the shape of the output tensor of matmul
    auto *Int32Ty = Type::getInt32Ty(Ctx);
    //unsigned NumDims = Input1.getNumDimensions();
    unsigned NumDims = dyn_cast<FixedVectorType>(Input1[0]->getType())->getNumElements();
    std::vector<Constant *> ShapeVec;
    std::vector<Constant *> LayoutVec;
    std::vector<Constant *> PaddingVec;
    for(unsigned i = 0; i < (NumDims - 1); i++) {
        unsigned DimVal = getShapeDimensionVal(Input1[0], i);
        ShapeVec.push_back(ConstantInt::get(Int32Ty, DimVal));
        LayoutVec.push_back(ConstantInt::get(Int32Ty, i));
        PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));
    }

    // Add the final padding layout dimensions
    ShapeVec.push_back(ConstantInt::get(Int32Ty, getShapeDimensionVal(Input2[0], NumDims - 1)));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, NumDims - 1));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));

    // The output of matmul is always assumed to be regular layout.
    Value *Shape = ConstantVector::get(ArrayRef<Constant *>(ShapeVec));
    Value *Layout = ConstantVector::get(ArrayRef<Constant *>(LayoutVec));
    Value *Padding = ConstantVector::get(ArrayRef<Constant *>(PaddingVec));

    SmallVector<Value *, 3> PropertyArray;
    PropertyArray.push_back(Shape);
    PropertyArray.push_back(Layout);
    PropertyArray.push_back(Padding);

    return PropertyArray;
}

SmallVector<Value *, 3> getTransposeOutputProperties(LLVMContext &Ctx,
                                                 SmallVector<Value *, 3> &Input) {

    auto getVectorElemVal = [](Value *Vect, unsigned Index) {
      if(auto *CV = dyn_cast<ConstantVector>(Vect)) {
        auto *C = CV->getAggregateElement(Index);
        return dyn_cast<ConstantInt>(C)->getZExtValue();
      }
    };

    // Get the properties of the output tensor of transpose
    auto *Int32Ty = Type::getInt32Ty(Ctx);
    //unsigned NumDims = Input1.getNumDimensions();
    unsigned NumDims = dyn_cast<FixedVectorType>(Input[0]->getType())->getNumElements();
    std::vector<Constant *> ShapeVec;
    std::vector<Constant *> LayoutVec;
    std::vector<Constant *> PaddingVec;
    for(unsigned i = 0; i < (NumDims - 2); i++) {
        unsigned DimVal = getVectorElemVal(Input[0], i);
        ShapeVec.push_back(ConstantInt::get(Int32Ty, DimVal));
        unsigned LayoutVal = getVectorElemVal(Input[1], i);
        LayoutVec.push_back(ConstantInt::get(Int32Ty, LayoutVal));
         unsigned PaddingVal = getVectorElemVal(Input[2], i);
        PaddingVec.push_back(ConstantInt::get(Int32Ty, PaddingVal));
    }

    // Add the last two shape, layout and padding info
    ShapeVec.push_back(ConstantInt::get(Int32Ty, getVectorElemVal(Input[0], NumDims - 1)));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, getVectorElemVal(Input[1], NumDims - 1)));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, getVectorElemVal(Input[2], NumDims - 1)));

    ShapeVec.push_back(ConstantInt::get(Int32Ty, getVectorElemVal(Input[0], NumDims - 2)));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, getVectorElemVal(Input[1], NumDims - 2)));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, getVectorElemVal(Input[2], NumDims - 2)));

    // The output of transpose is always assumed to be regular layout.
    Value *Shape = ConstantVector::get(ArrayRef<Constant *>(ShapeVec));
    Value *Layout = ConstantVector::get(ArrayRef<Constant *>(LayoutVec));
    Value *Padding = ConstantVector::get(ArrayRef<Constant *>(PaddingVec));

    SmallVector<Value *, 3> PropertyArray;
    PropertyArray.push_back(Shape);
    PropertyArray.push_back(Layout);
    PropertyArray.push_back(Padding);

    return PropertyArray;
}

SmallVector<Value *, 3> getReduceOutputProperties(LLVMContext &Ctx,
          SmallVector<Value *, 3> &Input, SmallVector<unsigned, 4> &WindowShape,
          SmallVector<unsigned, 4> &WindowStrides) {

    auto getShapeDimensionVal = [](Value *Shape, unsigned Index) {
      if(auto *CV = dyn_cast<ConstantVector>(Shape)) {
        auto *C = CV->getAggregateElement(Index);
        return dyn_cast<ConstantInt>(C)->getZExtValue();
      }
    };

    // Get the shape of the output tensor of reduction
    auto *Int32Ty = Type::getInt32Ty(Ctx);
    unsigned NumInDims = dyn_cast<FixedVectorType>(Input[0]->getType())->getNumElements();
    std::vector<Constant *> ShapeVec;
    std::vector<Constant *> LayoutVec;
    std::vector<Constant *> PaddingVec;
    for(unsigned i = 0; i < (NumInDims - 2); i++) {
        unsigned DimVal = getShapeDimensionVal(Input[0], i);
        ShapeVec.push_back(ConstantInt::get(Int32Ty, DimVal));
        LayoutVec.push_back(ConstantInt::get(Int32Ty, i));
        PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));
    }
    unsigned NumWinDims = WindowShape.size();
    unsigned OutputSize = ((WindowShape[NumWinDims - 2] 
                - getShapeDimensionVal(Input[0], NumInDims - 2)) / WindowStrides[NumWinDims - 2]) + 1;
    ShapeVec.push_back(ConstantInt::get(Int32Ty, OutputSize));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, NumInDims - 2));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));

    OutputSize = ((WindowShape[NumWinDims - 1] 
                - getShapeDimensionVal(Input[0], NumInDims - 1)) / WindowStrides[NumWinDims - 1]) + 1;
    ShapeVec.push_back(ConstantInt::get(Int32Ty, OutputSize));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, NumInDims - 1));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));

    // The output of reduction is always assumed to be regular layout.
    Value *Shape = ConstantVector::get(ArrayRef<Constant *>(ShapeVec));
    Value *Layout = ConstantVector::get(ArrayRef<Constant *>(LayoutVec));
    Value *Padding = ConstantVector::get(ArrayRef<Constant *>(PaddingVec));

    SmallVector<Value *, 3> PropertyArray;
    PropertyArray.push_back(Shape);
    PropertyArray.push_back(Layout);
    PropertyArray.push_back(Padding);

    return PropertyArray;
}

static bool mapTensorValToProperty(Instruction *I, 
                                   DenseMap<Value *, SmallVector<Value *, 3>> &ValToPropertyMap,
                                   SmallSet<Instruction *, 4> &TensorWaitlist) {
  errs() << "==== MAPPING TENSOR VALUE TO PROPERTY: " << *I << "\n";
  if(auto *CI = dyn_cast<CallInst>(I)) {
    const StringRef &CalledFuncName = CI->getCalledFunction()->getName();

    // Account for typeinfo call
    if(CalledFuncName.contains(StringRef("tensor_typeinfo"))) {
      SmallVector<Value *, 3> PropertyArray;
      PropertyArray.push_back(CI->getArgOperand(1));
      PropertyArray.push_back(CI->getArgOperand(2));
      PropertyArray.push_back(CI->getArgOperand(3));
      ValToPropertyMap[CI] = PropertyArray;
      ValToPropertyMap[CI->getArgOperand(0)] = PropertyArray;
      errs() << "CALL OPERAND POINTER: " << CI << "\n";
      SmallVector<Value *, 3> PropertyList = ValToPropertyMap[CI];
      errs() << "PROPERTY LIST: " << PropertyList[0] << " " << PropertyList[1] << " " << PropertyList[2] << "\n";
      return true;
    }

    // Account for element-wise tensor op
    if(CalledFuncName.contains(StringRef("tensor_relu"))
    || CalledFuncName.contains(StringRef("tensor_tanh"))
    || CalledFuncName.contains(StringRef("tensor_sigmoid"))
    || CalledFuncName.contains(StringRef("tensor_sin"))
    || CalledFuncName.contains(StringRef("tensor_cos"))
    || CalledFuncName.contains(StringRef("tensor_exp"))
    || CalledFuncName.contains(StringRef("tensor_exp2"))
    || CalledFuncName.contains(StringRef("tensor_log"))
    || CalledFuncName.contains(StringRef("tensor_log2"))
    || CalledFuncName.contains(StringRef("tensor_log10"))
    || CalledFuncName.contains(StringRef("tensor_sqrt"))
    || CalledFuncName.contains(StringRef("tensor_fabs"))
    || CalledFuncName.contains(StringRef("tensor_floor"))
    || CalledFuncName.contains(StringRef("tensor_ceil"))
    || CalledFuncName.contains(StringRef("tensor_broadcast"))) {
      // If the input tensor value's propeties have not been resolved yet,
      // we will resolve them later.
      const auto &It = ValToPropertyMap.find(CI->getArgOperand(0));
      errs() << "CALL OPERAND: " << *(CI->getArgOperand(0)) << "\n";
      if(It == ValToPropertyMap.end()) {
        errs() << "CALL OPERAND NOT MAPPED\n";
        // The input tensor value must be in the wait list.
        //assert(TensorWaitlist.find(CI->getArgOperand(0)) != TensorWaitlist.end()
        //    && "Tensor with unresolved properties must be in thje wait list.");
        auto *CallArgInst = dyn_cast<Instruction>(CI->getArgOperand(0));
        TensorWaitlist.insert(CallArgInst);

        // Try to find the input tensor value's properties now.
        errs() << "TRY AGAIN\n";
        if(!mapTensorValToProperty(CallArgInst, ValToPropertyMap, TensorWaitlist)) {
          // Since we still could not resolve the properties, put this call in the wait list
          errs() << "FAILED AGAIN\n";
          TensorWaitlist.insert(CI);
          return false;
        }
      }
     
      SmallVector<Value *, 3> PropertyList = ValToPropertyMap[CI->getArgOperand(0)];
      errs() << "CALL OPERAND POINTER: " << CI->getArgOperand(0) << "\n";
      errs() << "PROPERTY LIST: " << PropertyList[0] << " " << PropertyList[1] << " " << PropertyList[2] << "\n";

      ValToPropertyMap[CI] = ValToPropertyMap[CI->getArgOperand(0)];//PropertyList;
      
      // If this call is in the tensor wait list, this is good time to remove it!
      TensorWaitlist.erase(CI);
      return true;
    }

    if(CalledFuncName.contains(StringRef("tensor_matmul"))) {
        SmallVector<SmallVector<Value *, 3>, 2> OperandProperties;
        for(unsigned i = 0; i < 2; i++) {
            const auto &It = ValToPropertyMap.find(CI->getArgOperand(i));
            errs() << "CALL OPERAND: " << *(CI->getArgOperand(i)) << "\n";
            if(It == ValToPropertyMap.end()) {
                errs() << "CALL OPERAND NOT MAPPED\n";
                // The input tensor value must be in the wait list.
                //assert(TensorWaitlist.find(CI->getArgOperand(0)) != TensorWaitlist.end()
                //    && "Tensor with unresolved properties must be in thje wait list.");
                auto *CallArgInst = dyn_cast<Instruction>(CI->getArgOperand(i));
                TensorWaitlist.insert(CallArgInst);

                // Try to find the input tensor value's properties now.
                errs() << "TRY AGAIN\n";
                if(!mapTensorValToProperty(CallArgInst, ValToPropertyMap, TensorWaitlist)) {
                    // Since we still could not resolve the properties, put this call in the wait list
                    errs() << "FAILED AGAIN\n";
                    TensorWaitlist.insert(CI);
                    return false;
                }
            }
            //SmallVector<Value *, 3> PropertyList = ValToPropertyMap[II->getArgOperand(i)];
            errs() << "CALL OPERAND POINTER: " << CI->getArgOperand(i) << "\n";
            //PropertyList.print(errs());

            OperandProperties.push_back(ValToPropertyMap[CI->getArgOperand(i)]);
        }
        
        // Add the output tensor's properties
        ValToPropertyMap[CI] = getMatMulOuputProperties(CI->getModule()->getContext(), 
                                                  OperandProperties[0], OperandProperties[1]);

        // If this call is in the tensor wait list, this is good time to remove it!
        TensorWaitlist.erase(CI);

        return true;
    }

    if(CalledFuncName.contains(StringRef("tensor_transpose"))) {
      // If the input tensor value's propeties have not been resolved yet,
      // we will resolve them later.
      const auto &It = ValToPropertyMap.find(CI->getArgOperand(0));
      errs() << "CALL OPERAND: " << *(CI->getArgOperand(0)) << "\n";
      if(It == ValToPropertyMap.end()) {
        errs() << "CALL OPERAND NOT MAPPED\n";
        // The input tensor value must be in the wait list.
        //assert(TensorWaitlist.find(CI->getArgOperand(0)) != TensorWaitlist.end()
        //    && "Tensor with unresolved properties must be in thje wait list.");
        auto *CallArgInst = dyn_cast<Instruction>(CI->getArgOperand(0));
        TensorWaitlist.insert(CallArgInst);

        // Try to find the input tensor value's properties now.
        errs() << "TRY AGAIN\n";
        if(!mapTensorValToProperty(CallArgInst, ValToPropertyMap, TensorWaitlist)) {
          // Since we still could not resolve the properties, put this call in the wait list
          errs() << "FAILED AGAIN\n";
          TensorWaitlist.insert(CI);
          return false;
        }
      }

      // Add the output tensor's properties
      ValToPropertyMap[CI] = getTransposeOutputProperties(CI->getModule()->getContext(), 
                                                  ValToPropertyMap[CI->getArgOperand(0)]);
      
      // If this call is in the tensor wait list, this is good time to remove it!
      TensorWaitlist.erase(CI);

      return true;
    }

    if(CalledFuncName.contains(StringRef("tensor_reduce_max"))
    || CalledFuncName.contains(StringRef("tensor_reduce_min"))
    || CalledFuncName.contains(StringRef("tensor_reduce_and"))
    || CalledFuncName.contains(StringRef("tensor_reduce_or"))
    || CalledFuncName.contains(StringRef("tensor_reduce_xor"))
    || CalledFuncName.contains(StringRef("tensor_reduce_add"))
    || CalledFuncName.contains(StringRef("tensor_reduce_mul"))) {
      // If the input tensor value's propeties have not been resolved yet,
      // we will resolve them later.
      const auto &It = ValToPropertyMap.find(CI->getArgOperand(2));
      errs() << "CALL OPERAND: " << *(CI->getArgOperand(2)) << "\n";
      if(It == ValToPropertyMap.end()) {
        errs() << "CALL OPERAND NOT MAPPED\n";
        // The input tensor value must be in the wait list.
        //assert(TensorWaitlist.find(CI->getArgOperand(0)) != TensorWaitlist.end()
        //    && "Tensor with unresolved properties must be in thje wait list.");
        auto *CallArgInst = dyn_cast<Instruction>(CI->getArgOperand(2));
        TensorWaitlist.insert(CallArgInst);

        // Try to find the input tensor value's properties now.
        errs() << "TRY AGAIN\n";
        if(!mapTensorValToProperty(CallArgInst, ValToPropertyMap, TensorWaitlist)) {
          // Since we still could not resolve the properties, put this call in the wait list
          errs() << "FAILED AGAIN\n";
          TensorWaitlist.insert(CI);
          return false;
        }
      }

      // Get the strides and window shape
      SmallVector<unsigned, 4> WinShape;
      auto *WinShapeVal = CI->getArgOperand(0);
      auto *ShapeVectorTy = dyn_cast<FixedVectorType>(WinShapeVal->getType());
      auto *ShapeCV = dyn_cast<ConstantDataVector>(WinShapeVal);
      for(unsigned I = 0; I < ShapeVectorTy->getNumElements(); I++) {
          auto *C = ShapeCV->getAggregateElement(I);
          WinShape.push_back(dyn_cast<ConstantInt>(C)->getZExtValue());
      }
      SmallVector<unsigned, 4> Strides;
      auto *StridesVal = CI->getArgOperand(1);
      auto *StrideVectorTy = dyn_cast<FixedVectorType>(StridesVal->getType());
      auto *StrideCV = dyn_cast<ConstantDataVector>(StridesVal);
      for(unsigned I = 0; I < StrideVectorTy->getNumElements(); I++) {
          auto *C = StrideCV->getAggregateElement(I);
          Strides.push_back(dyn_cast<ConstantInt>(C)->getZExtValue());
      }

      // Add the output tensor's properties
      ValToPropertyMap[CI] = getReduceOutputProperties(CI->getModule()->getContext(), 
                                                  ValToPropertyMap[CI->getArgOperand(2)],
                                                  WinShape, Strides);
      
      // If this call is in the tensor wait list, this is good time to remove it!
      TensorWaitlist.erase(CI);

      return true;
    }

    return false;
  }

  // Resolve the properties of the operands of the instruction first
  Value *Shape = nullptr;
  Value *Layout = nullptr;
  Value *Padding = nullptr;
  for(auto &Op : I->operands()) {
    auto *Inst = dyn_cast<Instruction>(&Op);
    if(!Inst) {
      // We only care about instructions here and not arguments, etc.
      return false;
    }
    errs() << "OPERAND: " << *Inst << "\n";

    // If the input tensor value's propeties have not been resolved yet,
    // we will resolve them later.
    const auto &It = ValToPropertyMap.find(Inst);
    if(It == ValToPropertyMap.end()) {
      errs() << "OPERAND NOT MAPPED\n";
      // The input tensor value must be in the wait list.
      //ssert(TensorWaitlist.find(Inst) != TensorWaitlist.end()
       //      && "Tensor with unresolved properties must be in thje wait list.");
      TensorWaitlist.insert(Inst);

      // Try to find the input tensor value's properties now.
      errs() << "TRY AGAIN\n";
      if(!mapTensorValToProperty(Inst, ValToPropertyMap, TensorWaitlist)) {
        // Since we still could not resolve the properties, put this call in the wait list
        errs() << "FAILED AGAIN\n";
        TensorWaitlist.insert(I);
        return false;
      }
    }

    SmallVector<Value *, 3> PropertyList = ValToPropertyMap[Inst];
    if(!dyn_cast<PHINode>(I)) {
      //auto PropertyList = ValToPropertyMap[Inst];
      if(!Shape) {
        Shape = PropertyList[0];
      }
      if(!Layout) {
        Layout = PropertyList[1];
      }
      if(!Padding) {
        Padding = PropertyList[2];
      }
      assert(Shape == PropertyList[0]  && "Tensor shape of operand must match.");
      assert(Layout == PropertyList[1] && "Tensor layout of operand must match.");
    }

    errs() << "--INSTRUCTION: " << *Inst << " MAPPED TO " << *PropertyList[0] << " " 
                              << *PropertyList[1] << " " << *PropertyList[2] << "\n";
  }
  
  // Deal with PHIs later
  if(auto *PHI = dyn_cast<PHINode>(I)) {
    // If this PHI node is for a tensor value, 
    if(isTensorValuePHI(PHI)) {
      ValToPropertyMap[PHI] = getPropertyInfoForTensorPHI(PHI, ValToPropertyMap);
      TensorWaitlist.erase(PHI);
      return true;
    }

    // If the uses of the PHI instrucion already include a Typeinfo instruction,
    // we do not need to add a call ourself.
    for(auto *User : PHI->users()) {
      auto *Inst = dyn_cast<Instruction>(User);
      if(auto *CI = dyn_cast<CallInst>(Inst)) {
        if(CI->getCalledFunction()->getName().contains(StringRef("tensor_typeinfo"))) {
          return true;
        }
      }
    }

    // Add the typeinfo call if necessary
    return addTypeInfoAfterTensorPHI(PHI, ValToPropertyMap);
  }

  SmallVector<Value *, 3> PropertyArray;
  PropertyArray.push_back(Shape);
  PropertyArray.push_back(Layout);
  PropertyArray.push_back(Padding);
  ValToPropertyMap[I] = PropertyArray;
  errs() << "INSTRCUTION: " << *I << " MAPPED TO " << *Shape << " " << *Layout << " " << *Padding << "\n";

  // If this instruction is in the tensor wait list, this is good time to remove it!
  TensorWaitlist.erase(I);

  return true;
}

bool TensorPass::runOnFunction(Function &F) {
  if(F.isDeclaration()) {
    return false;
  }

  errs() << "FUNCTION: " << F.getName() << "\n";
  errs() << "\n\nPRINTING FUNCTION: " << F << "\n";

  SmallVector<CallInst *, 4> CallInstVect;
  SmallVector<CallInst *, 4> ReplaceCallsUses;
  DenseMap<Value *, Value *> FakeTypeToTokenTypeVal;
  DenseMap<Value *, SmallVector<Value *, 3>> ValToPropertyMap;
  SmallSet<Instruction *, 4> TensorWaitlist;
  SmallVector<PHINode *, 4> PHIToBeRemoved;

  // Traverse the CFG in a reverse-post-order fashion.
  ReversePostOrderTraversal<Function *> RPOT(&F);
  for(auto *BB : RPOT) {
    for (auto &I : *BB) {
      Instruction *Inst = &I;
      if(isTensorInstruction(Inst)) {
        errs() << "TENSOR INSTRUCTION: " << *Inst << "\n";
        bool Mapped = mapTensorValToProperty(Inst, ValToPropertyMap, TensorWaitlist);
        errs() << "MAPPED: " << Mapped << "\n";
        if (auto *CI = dyn_cast<CallInst>(Inst)) {
          
          CallInst* TypeInfoCI = dyn_cast<CallInst>(CI->getArgOperand(0)); 
          // In case a new vector PHI for typeinfo was
          // inserted, not previously visited in RPOT
          if(Mapped && TypeInfoCI && find(CallInstVect,TypeInfoCI) == CallInstVect.end()){
              Function* CF = TypeInfoCI->getCalledFunction();
              if(CF && CF->getName().contains(StringRef("tensor_typeinfo"))) {
                  errs()<<" NEW CALL INSTRUCTION (PHI) Created\n";
                  errs()<<*TypeInfoCI<<"\n";
                  CallInstVect.push_back(TypeInfoCI);

                } 
          }

          errs() << "CALL INSTRUCTION FOUND\n";
          errs() << CI->getCalledFunction()->getName() << "\n";
          errs() << "CALL INSTRUCTION FOUND: " << *CI << "\n";
          CallInstVect.push_back(CI);
        }
      }
    }
  }

  for (auto *CI  : CallInstVect) {
     std::vector<Value *> Args;
     std::vector<Type *> ArgsTy;
     errs() << "CALL INSTRUCTION: " << *CI << "\n";
     for (unsigned i = 0; i < CI->getNumArgOperands(); i++) {
      if(auto *TypeInfoCI = dyn_cast<CallInst>(CI->getArgOperand(i))) {
        if(TypeInfoCI->getCalledFunction()->getName().contains(StringRef("tensor_typeinfo"))) {
          errs() << "CORRESPONDING CALL INST: " << *TypeInfoCI << "\n";
          errs() << "CORRESPONDING CALL INTRINSIC: " << *FakeTypeToTokenTypeVal[TypeInfoCI] << "\n";
          Args.push_back(FakeTypeToTokenTypeVal[TypeInfoCI]);
          ArgsTy.push_back(FakeTypeToTokenTypeVal[TypeInfoCI]->getType());
          continue;
        }
      }
      Args.push_back(CI->getArgOperand(i));
      ArgsTy.push_back(CI->getArgOperand(i)->getType());
     }
     for(auto *Ty : ArgsTy) {
       errs() << "--TYPE: " << *Ty << "\n";
     }
     auto *FI = getIntrinsicDeclaration(CI, ArgsTy, FakeTypeToTokenTypeVal);
     assert(FI && "Appropriate tensor intrinsic must be used!");
     errs() << "ARG TYPE0: " << *(ArgsTy[0]) << "\n";
     errs() << "INTRINSIC FUNCTION: " << *FI << "\n";
     auto *NewCI = CallInst::Create(FI, Args, "", CI);
     errs() << "NEW INSTRINSIC CALLS: " << *NewCI << "\n";
     if(CI->getCalledFunction()->getName().contains(StringRef("tensor_typeinfo"))) {
      FakeTypeToTokenTypeVal[CI] = NewCI;

      if(find(ReplaceCallsUses, CI) == ReplaceCallsUses.end()) {
        ReplaceCallsUses.push_back(CI);
      }

      // Just put all the PHI users of this function call into a vector to
      // be deleted later.
      for(auto *User : CI->users()) {
        if(auto *Inst = dyn_cast<PHINode>(User)) {
          if(find(PHIToBeRemoved, Inst) == PHIToBeRemoved.end()) {
            PHIToBeRemoved.push_back(Inst);
          }
        }
      }
     } else {
      CI->replaceAllUsesWith(NewCI);
      CI->eraseFromParent();
    }
  }
  errs() << "\n\nPRINTING FUNCTION: " << F << "\n";
  errs() << "ERASING REST OF THE TYPE INFOS\n";

  // Remove the PHI instructions first
  for(auto *PHI : PHIToBeRemoved) {
    errs() << "ERASING PHI: " << *PHI << "\n";
    PHI->eraseFromParent();
  }

  // Remove TypeInfo function calls
  for (auto *CI  : ReplaceCallsUses) {
    errs() << "ERASING INSTRUCTION: " << *CI << "\n";
    
    // Erasing this 
    CI->eraseFromParent();
  }
 
  return true;
}


char TensorPass::ID = 0;
static RegisterPass<TensorPass> X("tensor", "Pass to replace tensor function calls with intrinsics");
