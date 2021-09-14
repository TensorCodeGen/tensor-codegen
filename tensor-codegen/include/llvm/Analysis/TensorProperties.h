//===- TensorProperties.h ----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This keeps track of the tensor properties such as shape, layout and padding
// for different tensor values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_TENSORINFO_H
#define LLVM_TRANSFORMS_TENSORINFO_H

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/TensorType.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <utility>


namespace llvm {

class TensorInfo {

  DenseMap<Value *, TensorType> ValToPropertyMap;

  // List of tensor values
  std::set<Value *> TensorValuesSet;

  // Mapping tensor values to the buffer allocations.s
  DenseMap<Value *, Instruction *> TensorMapToMemPtrMap;

  // Mapping tensor values to the actual allocated sizes
  DenseMap<Value *, unsigned> TensorToAllocSizeMap;

private:
  bool mapTensorValToProperty(
      Instruction *I, SmallSet<Instruction *, 4> &TensorWaitlist);

  TensorType getPropertyInfoWithForwardAnalysis(Instruction *);

  TensorType getMatMulOuputProperties(
      LLVMContext &Ctx, TensorType &Input1, TensorType &Input2);

  TensorType getTransposeOuputProperties(LLVMContext &Ctx, TensorType &Input);

  TensorType getReduceOutputProperties(LLVMContext &Ctx, TensorType &Input,
          SmallVector<unsigned, 4> &WindowShape, SmallVector<unsigned, 4> &WindowStrides);

public:
  TensorInfo() = default;

  const DenseMap<Value *, TensorType> &getTensorToPropertiesMap() const {
    return ValToPropertyMap;
  }

  const std::set<Value *> &getTensorValueSet() const { return TensorValuesSet; }

  TensorType &getTensorTypeInfoFor(Value *V) { return ValToPropertyMap[V]; }

  Instruction *getMemPtrFor(Value *V) { return TensorMapToMemPtrMap[V]; }

  // Get the tensor information for a given tensor value
  Value *getShapeFor(Value *V) { return ValToPropertyMap[V].getShape(); }
  Value *getLayoutFor(Value *V) { return ValToPropertyMap[V].getLayout(); }
  Value *getPaddingFor(Value *V) { return ValToPropertyMap[V].getPadding(); }

  SmallVector<unsigned, 4> &getShapeVectorFor(Value *V) {
    return ValToPropertyMap[V].getShapeVector();
  }
  SmallVector<unsigned, 4> &getLayoutVectorFor(Value *V) {
    return ValToPropertyMap[V].getLayoutVector();
  }
  SmallVector<unsigned, 4> &getPaddingVectorFor(Value *V) {
    return ValToPropertyMap[V].getPaddingVector();
  }

  unsigned getTensorSize(Value *V) {
    LLVM_DEBUG(dbgs() << "VALUE: " << *V << "\n");
    return ValToPropertyMap[V].getTensorSize();
  }

  unsigned getTensorAllocSize(Value *V) {
    LLVM_DEBUG(dbgs() << "VALUE: " << *V << "\n");
    return TensorToAllocSizeMap[V];
  }

  Value *getTensorOperand(Value *V, unsigned Index) {
    if (find(TensorValuesSet, V) == TensorValuesSet.end()) {
      return nullptr;
    }
    if (auto *II = dyn_cast<IntrinsicInst>(V)) {
      // Get the operand from typeinfo's first operand
      return dyn_cast<IntrinsicInst>(II->getOperand(Index))->getOperand(0);
    }
    return dyn_cast<Instruction>(V)->getOperand(Index);
  }

  bool isTensorValue(Value *V) {
    return (find(TensorValuesSet, V) != TensorValuesSet.end());
  }

  void addMemPtrForTensorVal(Value *V, Instruction *MemPtr) {
    TensorMapToMemPtrMap[V] = MemPtr;
  }

  bool isTensorInstruction(Instruction *I);

  bool analyze(Function &F);

  void bufferAlloc(bool InitTensorsWithMemCpy) {
    LLVM_DEBUG(dbgs() << "ALLOCATING BUFFERS\n");
    for (Value *V : TensorValuesSet) {
      LLVM_DEBUG(dbgs() << "TENSOR VALUE SET: " << *V << "\n");
      if (auto *PHI = dyn_cast<PHINode>(V)) {
        // Create a malloc call first
        auto *VectTy = dyn_cast<FixedVectorType>(PHI->getType());
        auto *AllocTy = VectTy->getElementType();
        auto *ArraySize = ConstantInt::get(
            Type::getInt32Ty(PHI->getModule()->getContext()),
            VectTy->getNumElements());
        Instruction *Malloc = CallInst::CreateMalloc(
            PHI->getParent()->getFirstNonPHI(), AllocTy, AllocTy,
            ConstantInt::get(AllocTy, 1), ArraySize, nullptr, "");

        // TI->addMemPtrForTensorVal(PHI, Malloc);
        TensorMapToMemPtrMap[PHI] = Malloc;
        continue;
      }

      // We do not create output buffers for tensor intrinsics unless typeinfo
      // for them is present. In all other cases, we let the legalizer deal with
      // the regular vectors. We do not allocate buffers for regular LLVM
      // instructions that are understood to operate on vectors.
      if (auto *II = dyn_cast<IntrinsicInst>(V)) {
        if (II->getIntrinsicID() == Intrinsic::tensor_typeinfo) {
          LLVM_DEBUG(
              dbgs() << "FUNCTION: " << *(II->getParent()->getParent())
                     << "\n");
          // Create a malloc call first
          const auto &DL = II->getModule()->getDataLayout();
          auto &Ctx = II->getModule()->getContext();
          Type *ArgType = II->getArgOperand(0)->getType();
          unsigned TensorSize = getTensorSize(II->getArgOperand(0));
          if (II->getArgOperand(0)->getType()->isPointerTy()) {
            auto *PTy = dyn_cast<PointerType>(II->getArgOperand(0)->getType());
            ArgType = PTy->getElementType();

            // The tensor size is equal to the alloca size
            auto *VectTy = dyn_cast<FixedVectorType>(ArgType);
            TensorSize = VectTy->getNumElements();
          }
          auto *VectTy = dyn_cast<FixedVectorType>(ArgType);
          auto *ElemTy = VectTy->getElementType();
          unsigned ElemAllocSize = DL.getTypeAllocSize(ElemTy).getFixedSize();
          unsigned TensorAllocSize = TensorSize * ElemAllocSize;
          auto *AllocTy = VectTy->getElementType();
          auto *ArraySize =
              ConstantInt::get(Type::getInt32Ty(Ctx), TensorAllocSize);
          Instruction *InsertBefore = II;
          if (auto *I = dyn_cast<Instruction>(II->getArgOperand(0))) {
            InsertBefore = I;
          }
          Instruction *Malloc = CallInst::CreateMalloc(
              InsertBefore, AllocTy, AllocTy, ConstantInt::get(AllocTy, 1),
              ArraySize, nullptr, "");

          // Get the memcpy intrinsic
          unsigned AS =
              dyn_cast<PointerType>(Malloc->getType())->getAddressSpace();
          auto *Ptrty8 = PointerType::get(Type::getInt8Ty(Ctx), AS);
          std::vector<Type *> ArgsTy = {Ptrty8, Ptrty8, Type::getInt32Ty(Ctx)};
          Function *MemCpy = Intrinsic::getDeclaration(
              II->getModule(), Intrinsic::memcpy, ArrayRef<Type *>({ArgsTy}));
          auto *MemCpyIsVolatile = ConstantInt::get(Type::getInt1Ty(Ctx), 0);

          LLVM_DEBUG(dbgs() << "MEMCPY: " << *MemCpy << "\n");
          LLVM_DEBUG(
              dbgs() << "FUNCTION: " << *(II->getParent()->getParent())
                     << "\n");

          // TI->addMemPtrForTensorVal(II, Malloc);
          TensorMapToMemPtrMap[InsertBefore] = Malloc;
          TensorToAllocSizeMap[InsertBefore] = TensorSize;
          if (InsertBefore != II) {
            TensorMapToMemPtrMap[II] = Malloc;
            TensorToAllocSizeMap[II] = TensorSize;
            // If the typeinfo operand is a pointer to tensor, put that in map
            // too
            if (II->getArgOperand(0)->getType()->isPointerTy()) {
              LLVM_DEBUG(
                  dbgs() << "========================================= MAPPING "
                            "STORE\n");
              // Look for the store that is a use of this pointer value
              for (auto *User : II->getArgOperand(0)->users()) {
                LLVM_DEBUG(dbgs() << "USER: " << *User << "\n");
                if (auto *SI = dyn_cast<StoreInst>(User)) {
                  LLVM_DEBUG(dbgs() << "STORE FOUND\n");
                  // Put the value being stored in the map
                  TensorMapToMemPtrMap[SI->getValueOperand()] = Malloc;
                  TensorToAllocSizeMap[SI->getValueOperand()] = TensorSize;
                  break;
                }
              }
            }
            if (II->getArgOperand(0)->getType()->isPointerTy()) {
              // Find the value associated with the store to this alloca
              for (auto *User : II->getArgOperand(0)->users()) {
                if (auto *SI = dyn_cast<StoreInst>(User)) {
                  unsigned AS = dyn_cast<PointerType>(Malloc->getType())
                                    ->getAddressSpace();
                  if (!InitTensorsWithMemCpy) {
                    // Initialize the tensor
                    auto *VectTy = FixedVectorType::get(AllocTy, TensorSize);
                    auto *CastMallocPtr = CastInst::CreatePointerCast(
                        Malloc, PointerType::get(VectTy, AS), "malloc.cast",
                        II);
                    new StoreInst(
                        SI->getValueOperand(), CastMallocPtr, false, {}, II);

                  } else {
                    LLVM_DEBUG(dbgs() << "INSERTING MEMCPY\n");
                    LLVM_DEBUG(dbgs() << "STORE INST: " << *SI << "\n");
                    if (auto *LI = dyn_cast<LoadInst>(SI->getValueOperand())) {
                      auto *SrcPtr = LI->getOperand(0);
                      LLVM_DEBUG(dbgs() << "LOAD INST: " << *LI << "\n");
                      SrcPtr = CastInst::CreatePointerCast(
                          SrcPtr, PointerType::get(Type::getInt8Ty(Ctx), AS),
                          "load.cast", II);
                      LLVM_DEBUG(dbgs() << "CAST: " << *SrcPtr << "\n");
                      LLVM_DEBUG(
                          dbgs() << "Malloc->getOperand(0): "
                                 << *(Malloc->getOperand(0)) << "\n");
                      unsigned WriteAllocSize =
                          ElemAllocSize * getTensorSize(II->getArgOperand(0));
                      auto *WriteAllocSizeVal =
                          ArraySize; // ConstantInt::get(Type::getInt32Ty(Ctx),
                                     // WriteAllocSize);
                      std::vector<Value *> Args = {
                          Malloc->getOperand(0), SrcPtr, WriteAllocSizeVal,
                          MemCpyIsVolatile};
                      auto *Memcpycall = CallInst::Create(
                          MemCpy->getFunctionType(), MemCpy,
                          ArrayRef<Value *>(Args), "", II);
                      LLVM_DEBUG(dbgs() << "MEMCPY: " << *Memcpycall << "\n");
                    }
                  }
                }
              }
            }
          } else {
            TensorMapToMemPtrMap[II->getArgOperand(0)] = Malloc;
            TensorToAllocSizeMap[II->getArgOperand(0)] = TensorSize;
            if (II->getArgOperand(0)->getType()->isPointerTy()) {
              // Find the value associated with the store to this alloca
              for (auto *User : II->getArgOperand(0)->users()) {
                if (auto *SI = dyn_cast<StoreInst>(User)) {
                  unsigned AS = dyn_cast<PointerType>(Malloc->getType())
                                    ->getAddressSpace();
                  if (!InitTensorsWithMemCpy) {
                    // Initialize the tensor
                    auto *VectTy = FixedVectorType::get(AllocTy, TensorSize);
                    auto *CastMallocPtr = CastInst::CreatePointerCast(
                        Malloc, PointerType::get(VectTy, AS), "malloc.cast",
                        InsertBefore);
                    new StoreInst(
                        SI->getValueOperand(), CastMallocPtr, false, {},
                        InsertBefore);
                  } else {
                    if (auto *LI = dyn_cast<LoadInst>(SI->getValueOperand())) {
                      auto *SrcPtr = LI->getOperand(0);
                      LLVM_DEBUG(dbgs() << "LOAD INST: " << *LI << "\n");
                      SrcPtr = CastInst::CreatePointerCast(
                          SrcPtr, PointerType::get(Type::getInt8Ty(Ctx), AS),
                          "load.cast", InsertBefore);
                      unsigned WriteAllocSize =
                          ElemAllocSize * getTensorSize(II->getArgOperand(0));
                      auto *WriteAllocSizeVal =
                          ArraySize; // ConstantInt::get(Type::getInt32Ty(Ctx),
                                     // WriteAllocSize);
                      LLVM_DEBUG(dbgs() << "CAST: " << *SrcPtr << "\n");
                      std::vector<Value *> Args = {
                          Malloc->getOperand(0), SrcPtr, WriteAllocSizeVal,
                          MemCpyIsVolatile};
                      CallInst::Create(
                          MemCpy->getFunctionType(), MemCpy,
                          ArrayRef<Value *>(Args), "", InsertBefore);
                    }
                  }
                }
              }
            } else {
              auto *VectTy = FixedVectorType::get(AllocTy, TensorSize);
              unsigned AS =
                  dyn_cast<PointerType>(Malloc->getType())->getAddressSpace();
              auto *CastMallocPtr = CastInst::CreatePointerCast(
                  Malloc, PointerType::get(VectTy, AS), "malloc.cast",
                  InsertBefore);
              new StoreInst(
                  II->getArgOperand(0), CastMallocPtr, false, {}, InsertBefore);
            }
          }
          continue;
        }
      }
    }
  }
};

class TensorInfoWrapperPass : public FunctionPass {
  DenseMap<Function *, TensorInfo> TensorInfoMap;

public:
  static char ID;

  TensorInfoWrapperPass();

  bool runOnFunction(Function &F) override;

  TensorInfo &getTensorInfo(Function *F) { return TensorInfoMap[F]; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {}

  void verifyAnalysis() const override {}

  void print(raw_ostream &OS, const Module *M = nullptr) const override {}
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_TENSORINFO_H
