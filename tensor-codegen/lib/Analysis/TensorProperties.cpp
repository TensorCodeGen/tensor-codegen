//===- TensorProperties.cpp---------------------------------------*- C++ -*-===//
 //
 // Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 // See https://llvm.org/LICENSE.txt for license information.
 // SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 //
//===----------------------------------------------------------------------===//
//
// This pass infers information like shape, padding and layout information
// for tensors in LLVM IR.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/PassManager.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"

#include "llvm/Analysis/TensorProperties.h"
#include "llvm/IR/TensorType.h"

using namespace llvm;

bool TensorInfo::isTensorInstruction(Instruction *I) {
   if(find(TensorValuesSet, I) != TensorValuesSet.end()) {
     return true;
   }

    if(auto *II = dyn_cast<IntrinsicInst>(I)) {
        switch(II->getIntrinsicID()) {
            case Intrinsic::tensor_typeinfo:
            case Intrinsic::tensor_relu:
            case Intrinsic::tensor_tanh:
            case Intrinsic::tensor_sigmoid:
            case Intrinsic::tensor_sin:
            case Intrinsic::tensor_cos:
            case Intrinsic::tensor_exp:
            case Intrinsic::tensor_exp2:
            case Intrinsic::tensor_log:
            case Intrinsic::tensor_log2:
            case Intrinsic::tensor_log10:
            case Intrinsic::tensor_sqrt:
            case Intrinsic::tensor_fabs:
            case Intrinsic::tensor_floor:
            case Intrinsic::tensor_ceil:
            case Intrinsic::tensor_matmul:
            case Intrinsic::tensor_broadcast:
            case Intrinsic::tensor_transpose:
                return true;
            default:
                return false;
        }
        return false;
    }

    if(dyn_cast<UnaryOperator>(I) || dyn_cast<BinaryOperator>(I)
    || dyn_cast<SelectInst>(I) || dyn_cast<CmpInst>(I)) {
      // If their operands are tensor operations, then these
      // instructions can be understood to be tensor operations.
      if(auto *OpInst = dyn_cast<Instruction>(I->getOperand(0))) {
          if(I->getOperand(0)->getType()->isVectorTy()) {
            return isTensorInstruction(OpInst);
          }
      }
      return false;
    }

    if(auto *PHI = dyn_cast<PHINode>(I)) {
        if(PHI->getType()->isVectorTy()) {
            for(unsigned i = 0; i < PHI->getNumOperands(); i++) {
                if(dyn_cast<ConstantVector>(PHI->getIncomingValue(i))) {
                    return false;
                }
            }
        }
        return false;
    }

    return false;
}

TensorType TensorInfo::getPropertyInfoWithForwardAnalysis(Instruction *I) {
  // Check if this instruction is a typeinfo
  if(auto *II = dyn_cast<IntrinsicInst>(I)) {
    // Account for typeinfo call
    if(II->getIntrinsicID() == Intrinsic::tensor_typeinfo) {
      TensorType PropertyList(II->getArgOperand(1), II->getArgOperand(2), II->getArgOperand(3));
      return PropertyList;
    }
  }

  // Find a uses of this instruction in a typeinfo instuction
  SmallVector<Instruction *, 4> Worklist;
  Worklist.push_back(I);
  while(!Worklist.empty()) {
    auto *Inst = Worklist.back();
    Worklist.pop_back();
    const auto &It = ValToPropertyMap.find(I);
    for(auto *User : Inst->users()) {
      auto *UserInst = dyn_cast<Instruction>(User);

      if(auto *II = dyn_cast<IntrinsicInst>(UserInst)) {
        const StringRef &CalledFuncName = II->getCalledFunction()->getName();

        // Account for typeinfo call
        if(II->getIntrinsicID() == Intrinsic::tensor_typeinfo) {
          TensorType PropertyList(II->getArgOperand(1), II->getArgOperand(2), 
                                           II->getArgOperand(3));
          return PropertyList;
        }

        // Account for element-wise tensor op
        if(II->getIntrinsicID() == Intrinsic::tensor_relu
        || II->getIntrinsicID() == Intrinsic::tensor_tanh
        || II->getIntrinsicID() == Intrinsic::tensor_sigmoid
        || II->getIntrinsicID() == Intrinsic::tensor_sin
        || II->getIntrinsicID() == Intrinsic::tensor_cos
        || II->getIntrinsicID() == Intrinsic::tensor_exp
        || II->getIntrinsicID() == Intrinsic::tensor_exp2
        || II->getIntrinsicID() == Intrinsic::tensor_log
        || II->getIntrinsicID() == Intrinsic::tensor_log2
        || II->getIntrinsicID() == Intrinsic::tensor_log10
        || II->getIntrinsicID() == Intrinsic::tensor_floor
        || II->getIntrinsicID() == Intrinsic::tensor_ceil
        || II->getIntrinsicID() == Intrinsic::tensor_fabs
        || II->getIntrinsicID() == Intrinsic::tensor_sqrt
        || II->getIntrinsicID() == Intrinsic::tensor_broadcast) {
          Worklist.push_back(II);
          continue;
        }
      }

      if(auto *PHIInst = dyn_cast<PHINode>(UserInst)) {
        assert(isTensorInstruction(PHIInst) && "Should be a tensor PHI node");
        return getPropertyInfoWithForwardAnalysis(PHIInst);
      }

      if(isTensorInstruction(UserInst)) {
        Worklist.push_back(I);
        continue;
      }

      if(dyn_cast<ReturnInst>(UserInst)) {
        continue;
      }
      
      assert(false && "Cannot reach here");
    }
  }

  // Return an empty tensor type
  return TensorType();
}

TensorType TensorInfo::getMatMulOuputProperties(LLVMContext &Ctx,
                                TensorType &Input1, TensorType &Input2) {
    errs() << "GETTING MATMUL OUTPUT PROPERTIES\n";
    // Get the shape of the output tensor of matmul
    auto *Int32Ty = Type::getInt32Ty(Ctx);
    unsigned NumDims = Input1.getNumDimensions();
    std::vector<Constant *> ShapeVec;
    std::vector<Constant *> LayoutVec;
    std::vector<Constant *> PaddingVec;
    for(unsigned i = 0; i < (NumDims - 1); i++) {
        unsigned DimVal = Input1.getShapeDimensionVal(i);
        ShapeVec.push_back(ConstantInt::get(Int32Ty, DimVal));
        LayoutVec.push_back(ConstantInt::get(Int32Ty, i));
        PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));
    }

    // Add the final padding layout dimensions
    ShapeVec.push_back(ConstantInt::get(Int32Ty, Input2.getShapeDimensionVal(NumDims - 1)));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, NumDims - 1));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));

    // The output of matmul is always assumed to be regular layout.
    Value *Shape = ConstantVector::get(ArrayRef<Constant *>(ShapeVec));
    Value *Layout = ConstantVector::get(ArrayRef<Constant *>(LayoutVec));
    Value *Padding = ConstantVector::get(ArrayRef<Constant *>(PaddingVec));

    errs() << "DONE\n";
    return TensorType(Shape, Layout, Padding);
}

TensorType TensorInfo::getTransposeOuputProperties(LLVMContext &Ctx, TensorType &Input) {
    // Get the properties of the output tensor of transpose
    auto *Int32Ty = Type::getInt32Ty(Ctx);
    unsigned NumDims = Input.getNumDimensions();
    std::vector<Constant *> ShapeVec;
    std::vector<Constant *> LayoutVec;
    std::vector<Constant *> PaddingVec;
    for(unsigned i = 0; i < (NumDims - 1); i++) {
        unsigned DimVal = Input.getShapeDimensionVal(i);
        ShapeVec.push_back(ConstantInt::get(Int32Ty, DimVal));
        unsigned LayoutVal = Input.getLayoutVal(i);
        LayoutVec.push_back(ConstantInt::get(Int32Ty, LayoutVal));
        unsigned PaddingVal = Input.getPaddingVal(i);
        PaddingVec.push_back(ConstantInt::get(Int32Ty, 0));
    }

    // Add the last two shape, layout and padding info
    ShapeVec.push_back(ConstantInt::get(Int32Ty, Input.getShapeDimensionVal(NumDims - 1)));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, Input.getLayoutVal(NumDims - 1)));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, Input.getPaddingVal(NumDims - 1)));

    ShapeVec.push_back(ConstantInt::get(Int32Ty, Input.getShapeDimensionVal(NumDims - 2)));
    LayoutVec.push_back(ConstantInt::get(Int32Ty, Input.getLayoutVal(NumDims - 2)));
    PaddingVec.push_back(ConstantInt::get(Int32Ty, Input.getPaddingVal(NumDims - 2)));

    // Construct the tensor type info
    Value *Shape = ConstantVector::get(ArrayRef<Constant *>(ShapeVec));
    Value *Layout = ConstantVector::get(ArrayRef<Constant *>(LayoutVec));
    Value *Padding = ConstantVector::get(ArrayRef<Constant *>(PaddingVec));

    return TensorType(Shape, Layout, Padding);
}

bool TensorInfo::mapTensorValToProperty(Instruction *I, 
                                   SmallSet<Instruction *, 4> &TensorWaitlist) {
  errs() << "MAP TENSOR VAL TO PROPERTY\n";
  errs() << "INSTRUCTION: " << *I << "\n";
  if(auto *II = dyn_cast<IntrinsicInst>(I)) {
    // Account for typeinfo call
    if(II->getIntrinsicID() == Intrinsic::tensor_typeinfo) {
      TensorType PropertyList(II->getArgOperand(1), II->getArgOperand(2), II->getArgOperand(3));
      ValToPropertyMap[II] = PropertyList;
      ValToPropertyMap[II->getArgOperand(0)] = PropertyList;
      PropertyList.print(errs()); 

      // If the typeinfo operand is a pointer to tensor, put that in map too
      if(II->getArgOperand(0)->getType()->isPointerTy()) {
        errs() << "========================================= MAPPING STORE\n";
        // Look for the store that is a use of this pointer value
        for(auto *User : II->getArgOperand(0)->users()) {
          errs() << "USER: " << *User << "\n";
          if(auto *SI = dyn_cast<StoreInst>(User)) {
            errs() << "STORE FOUND\n";
            // Put the value being stored in the map
            ValToPropertyMap[SI->getValueOperand()] = PropertyList;
            break;
          }
        }
      }
      return true;
    }

    // Account for element-wise tensor op
    if(II->getIntrinsicID() == Intrinsic::tensor_relu
    || II->getIntrinsicID() == Intrinsic::tensor_tanh
    || II->getIntrinsicID() == Intrinsic::tensor_sigmoid
    || II->getIntrinsicID() == Intrinsic::tensor_sin
    || II->getIntrinsicID() == Intrinsic::tensor_cos
    || II->getIntrinsicID() == Intrinsic::tensor_exp
    || II->getIntrinsicID() == Intrinsic::tensor_exp2
    || II->getIntrinsicID() == Intrinsic::tensor_log
    || II->getIntrinsicID() == Intrinsic::tensor_log2
    || II->getIntrinsicID() == Intrinsic::tensor_log10
    || II->getIntrinsicID() == Intrinsic::tensor_floor
    || II->getIntrinsicID() == Intrinsic::tensor_ceil
    || II->getIntrinsicID() == Intrinsic::tensor_fabs
    || II->getIntrinsicID() == Intrinsic::tensor_sqrt
    || II->getIntrinsicID() == Intrinsic::tensor_broadcast) {
      // If the input tensor value's propeties have not been resolved yet,
      // we will resolve them later.
      const auto &It = ValToPropertyMap.find(II->getArgOperand(0));
      if(It == ValToPropertyMap.end()) {
        // The input tensor value must be in the wait list.
        //assert(TensorWaitlist.find(CI->getArgOperand(0)) != TensorWaitlist.end()
        //    && "Tensor with unresolved properties must be in thje wait list.");
        auto *CallArgInst = dyn_cast<Instruction>(II->getArgOperand(0));
        TensorWaitlist.insert(CallArgInst);

        // Try to find the input tensor value's properties now.
        if(!mapTensorValToProperty(CallArgInst, TensorWaitlist)) {
          // Since we still could not resolve the properties, put this call in the wait list
          TensorWaitlist.insert(II);
          return false;
        }
      }
     
      TensorType PropertyList = ValToPropertyMap[II->getArgOperand(0)];
      PropertyList.print(errs());

      ValToPropertyMap[II] = ValToPropertyMap[II->getArgOperand(0)];
      
      // If this call is in the tensor wait list, this is good time to remove it!
      TensorWaitlist.erase(II);
      return true;
    }

    if(II->getIntrinsicID() == Intrinsic::tensor_matmul) {
      errs() << "MATMUL\n";
        SmallVector<TensorType, 2> OperandProperties;
        for(unsigned i = 0; i < 2; i++) {
            const auto &It = ValToPropertyMap.find(II->getArgOperand(i));
            if(It == ValToPropertyMap.end()) {
                // The input tensor value must be in the wait list.
                //assert(TensorWaitlist.find(CI->getArgOperand(0)) != TensorWaitlist.end()
                //    && "Tensor with unresolved properties must be in thje wait list.");
                auto *CallArgInst = dyn_cast<Instruction>(II->getArgOperand(i));
                TensorWaitlist.insert(CallArgInst);

                // Try to find the input tensor value's properties now.
                if(!mapTensorValToProperty(CallArgInst, TensorWaitlist)) {
                    // Since we still could not resolve the properties, put this call in the wait list
                    TensorWaitlist.insert(II);
                    return false;
                }
            }
            TensorType PropertyList = ValToPropertyMap[II->getArgOperand(i)];
            PropertyList.print(errs());

            OperandProperties.push_back(ValToPropertyMap[II->getArgOperand(i)]);
        }
        
        // Add the output tensor's properties
        ValToPropertyMap[II] = getMatMulOuputProperties(II->getModule()->getContext(), 
                                        OperandProperties[0], OperandProperties[1]);

        // If this call is in the tensor wait list, this is good time to remove it!
        TensorWaitlist.erase(II);

        return true;
    }

    if(II->getIntrinsicID() == Intrinsic::tensor_transpose) {
      // If the input tensor value's propeties have not been resolved yet,
      // we will resolve them later.
      const auto &It = ValToPropertyMap.find(II->getArgOperand(0));
      if(It == ValToPropertyMap.end()) {
        // The input tensor value must be in the wait list.
        //assert(TensorWaitlist.find(CI->getArgOperand(0)) != TensorWaitlist.end()
        //    && "Tensor with unresolved properties must be in thje wait list.");
        auto *CallArgInst = dyn_cast<Instruction>(II->getArgOperand(0));
        TensorWaitlist.insert(CallArgInst);

        // Try to find the input tensor value's properties now.
        if(!mapTensorValToProperty(CallArgInst, TensorWaitlist)) {
          // Since we still could not resolve the properties, put this call in the wait list
          TensorWaitlist.insert(II);
          return false;
        }
      }

      // Add the output tensor's properties
      ValToPropertyMap[II] = getTransposeOuputProperties(II->getModule()->getContext(), 
                                            ValToPropertyMap[II->getArgOperand(0)]);

      // If this call is in the tensor wait list, this is good time to remove it!
      TensorWaitlist.erase(II);

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
      // We only care about instructions here and not arguments.
      return false;
    }

    // If the input tensor value's propeties have not been resolved yet,
    // we will resolve them later.
    const auto &It = ValToPropertyMap.find(Inst);
    if(It == ValToPropertyMap.end()) {
      // The input tensor value must be in the wait list.
      //ssert(TensorWaitlist.find(Inst) != TensorWaitlist.end()
       //      && "Tensor with unresolved properties must be in thje wait list.");
      TensorWaitlist.insert(Inst);

      // Try to find the input tensor value's properties now.
      if(!mapTensorValToProperty(Inst, TensorWaitlist)) {
        // Since we still could not resolve the properties, put this call in the wait list
        TensorWaitlist.insert(I);
        return false;
      }
    }

    TensorType PropertyList = ValToPropertyMap[Inst];
    if(!dyn_cast<PHINode>(I)) {
      //auto PropertyList = ValToPropertyMap[Inst];
      if(!Shape) {
        Shape = PropertyList.getShape();
      }
      if(!Layout) {
        Layout = PropertyList.getLayout();
      }
      if(!Padding) {
        Padding = PropertyList.getPadding();
      }
      assert(Shape == PropertyList.getShape()  && "Tensor shape of operand must match.");
      assert(Layout == PropertyList.getLayout() && "Tensor layout of operand must match.");
    }
    PropertyList.print(errs());
  }
  
  // Deal with PHIs later
  if(auto *PHI = dyn_cast<PHINode>(I)) {
    // If this PHI node is for a tensor value, 
    if(isTensorInstruction(PHI)) {
      TensorType PropertyArray = getPropertyInfoWithForwardAnalysis(PHI);
      assert(PropertyArray.isValidTensorType() && "Invalid tensor type");
      ValToPropertyMap[PHI] = PropertyArray;
      PropertyArray.print(errs());
      TensorWaitlist.erase(PHI);
      return true;
    }

    // If the uses of the PHI instrucion already include a Typeinfo instruction,
    // we do not need to add a call ourself.
    for(auto *User : PHI->users()) {
      auto *Inst = dyn_cast<Instruction>(User);
      if(auto *II = dyn_cast<IntrinsicInst>(Inst)) {
        if(II->getIntrinsicID() == Intrinsic::tensor_typeinfo) {
          return true;
        }
      }
    }
  }

  ValToPropertyMap[I] = TensorType(Shape, Layout, Padding);

  // If this instruction is in the tensor wait list, this is good time to remove it!
  TensorWaitlist.erase(I);

  return true;
}

bool TensorInfo::analyze(Function &F) {
  errs() << "ANALYZING TENSOR FUNCTION\n";
   errs() << "PRINTING FUNCTION: " << F << "\n";

  SmallSet<Instruction *, 4> TensorWaitlist;
  ReversePostOrderTraversal<Function *> RPOT(&F);
  for(auto *BB : RPOT) {
    for (auto &I : *BB) {
      if(isTensorInstruction(&I)) {
        mapTensorValToProperty(&I, TensorWaitlist);

        // Add the tensor value to the tensor value set
        TensorValuesSet.insert(&I);

        // If the instruction is typeinfo, we also want to
        // include the first tensor argument.
        if(auto *II = dyn_cast<IntrinsicInst>(&I)) {
          if(II->getIntrinsicID() == Intrinsic::tensor_typeinfo) {
            TensorValuesSet.insert(II->getArgOperand(0));
          }
        }
      }
    }
  }

  // Now empty the tensor waitlist.
  for(auto *I : TensorWaitlist) {
    mapTensorValToProperty(I, TensorWaitlist);
  }
  

   errs() << "---PRINTING FUNCTION: " << F << "\n";

  return false;
}

bool TensorInfoWrapperPass::runOnFunction(Function &F) {
    if(F.isDeclaration()) {
        return false;
    }
    return TensorInfoMap[&F].analyze(F);
}

char TensorInfoWrapperPass::ID = 0;

 TensorInfoWrapperPass::TensorInfoWrapperPass() : FunctionPass(ID) {
   initializeTensorInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

 INITIALIZE_PASS(TensorInfoWrapperPass, "tensor-analysis", "Pass to inferring tensor properties",
                       true, true)
