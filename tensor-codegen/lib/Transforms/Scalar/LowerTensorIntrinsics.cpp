//===- LowerTensorIntrinsics.cpp -  Lower tensor intrinsics -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lower tensor intrinsics to scalar/vector/tensor instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/JSON.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/IR/Verifier.h"

#include "llvm/Analysis/TensorProperties.h"
#include "llvm/IR/TensorType.h"
#include "llvm/Transforms/Scalar/LowerTensorIntrinsics.h"

#include <fstream>
#include <limits>

using namespace llvm;

static cl::opt<std::string> PrintKnobsTo(
    "print-knobs-to",
    cl::desc("If set, only print knobs to the given file (doesn't actually "
             "lower the instructions)"));
static cl::opt<std::string> ReadKnobsFrom(
    "read-knobs-from", cl::desc("If set, read knob values from the given file "
                                "and lower instructions with these values"));

//struct CodeGenKnobs {
  unsigned TileSize_M = 4;
  unsigned TileSize_N = 4;
  unsigned TileSize_K = 10;

  unsigned TileSize = 2;

  bool FuseTransposeAndMatmul = false;

  bool InitTensorsWithMemCpy = true;

  unsigned InnerLoopUnrollFactor = 0;

  bool LowerToVectorIntrinsics = false;

  bool LowerToTileIntrinsics = true;
//};

struct TiledLoopNestInfo {
  /// Loop Bounds from outermost loop to innermost loop
  SmallVector<unsigned, 4> LoopBounds;

  /// Loop steps from outermost loop to innermost loop
  SmallVector<unsigned, 4> LoopSteps;

  /// Loop start indices
  SmallVector<unsigned, 4> LoopStartIndices;

  /// Loop latches from outermost loop to innermost loop
  SmallVector<BasicBlock *, 4> LoopLatches;

  /// Loop headers from outermost loop to innermost loop
  SmallVector<BasicBlock *, 4> LoopHeaders;

  /// Preheaders for loops in loop nest
  SmallVector<BasicBlock *, 4> LoopPreheaders;

  /// The loop nest indices vector
  SmallVector<Value *, 4> LoopIndices;

  /// The innermost block of the loop nest
  BasicBlock *InnerLoopBody = nullptr;

  TiledLoopNestInfo(
      SmallVector<unsigned, 4> LoopBounds, SmallVector<unsigned, 4> LoopSteps,
      SmallVector<unsigned, 4> LoopStartIndices)
      : LoopBounds(LoopBounds), LoopSteps(LoopSteps),
        LoopStartIndices(LoopStartIndices) {}

  TiledLoopNestInfo() = default;
};

BasicBlock *CreateLoop(
    BasicBlock *Preheader, BasicBlock *Exit, Value *Bound, Value *Step,
    Value *StartIndex, bool MustHaveBody, StringRef Name, DomTreeUpdater &DTU,
    Loop *L, LoopInfo &LI) {
  LLVMContext &Ctx = Preheader->getContext();
  auto *I32Ty = Type::getInt32Ty(Ctx);
  auto *Header = BasicBlock::Create(
      Preheader->getContext(), Name + ".header", Preheader->getParent(), Exit);
  BasicBlock *Body = nullptr;
  if (MustHaveBody) {
    Body = BasicBlock::Create(
        Header->getContext(), Name + ".body", Header->getParent(), Exit);
  }
  auto *Latch = BasicBlock::Create(
      Header->getContext(), Name + ".latch", Header->getParent(), Exit);
  if (MustHaveBody) {
    BranchInst::Create(Body, Header);
    BranchInst::Create(Latch, Body);
  } else {
    BranchInst::Create(Latch, Header);
  }

  auto *IV = PHINode::Create(I32Ty, 2, Name + ".iv", Header->getTerminator());
  IV->addIncoming(StartIndex, Preheader);

  Value *Inc =
      BinaryOperator::Create(Instruction::Add, IV, Step, Name + ".step", Latch);
  Value *Cond = CmpInst::Create(
      Instruction::ICmp, ICmpInst::ICMP_NE, Inc, Bound, Name + ".step", Latch);
  BranchInst::Create(Header, Exit, Cond, Latch);
  IV->addIncoming(Inc, Latch);

  auto *PreheaderBr = cast<BranchInst>(Preheader->getTerminator());
  BasicBlock *Tmp = PreheaderBr->getSuccessor(0);
  PreheaderBr->setSuccessor(0, Header);

  if(MustHaveBody) {
    DTU.applyUpdatesPermissive({
        {DominatorTree::Delete, Preheader, Tmp},
        {DominatorTree::Insert, Header, Body},
        {DominatorTree::Insert, Body, Latch},
        {DominatorTree::Insert, Latch, Header},
        {DominatorTree::Insert, Latch, Exit},
        {DominatorTree::Insert, Preheader, Header},
    });
    L->addBasicBlockToLoop(Header, LI);
    L->addBasicBlockToLoop(Body, LI);
    L->addBasicBlockToLoop(Latch, LI);

    return Body;
  } else {
    DTU.applyUpdatesPermissive({
        {DominatorTree::Delete, Preheader, Tmp},
        {DominatorTree::Insert, Header, Latch},
        {DominatorTree::Insert, Latch, Header},
        {DominatorTree::Insert, Latch, Exit},
        {DominatorTree::Insert, Preheader, Header},
    });
    L->addBasicBlockToLoop(Header, LI);
    L->addBasicBlockToLoop(Latch, LI);

    return Header;
  }

  return nullptr;
}

/// Creates the following loop nest skeleton:
///  for m = 0; m < M; m += TileSize_M
///    for n = 0; n < N; n += TileSize_N
///      for k = 0; k < K ; k += TileSize_K
///         ...
void CreateTiledLoops(
    BasicBlock *Start, BasicBlock *End, DomTreeUpdater &DTU, LoopInfo &LI,
    TiledLoopNestInfo &TI, bool MustHaveBody = false) {
  SmallVector<Loop *, 4> Loops;
  for (unsigned I = 0; I < TI.LoopBounds.size(); I++) {
    Loops.push_back(LI.AllocateLoop());
  }
  for (unsigned I = 0; I < Loops.size() - 1; I++) {
    Loops[I]->addChildLoop(Loops[I + 1]);
  }
  if (Loop *ParentL = LI.getLoopFor(Start)) {
    ParentL->addChildLoop(Loops[0]);
  } else {
    LI.addTopLevelLoop(Loops[0]);
  }

  auto &Ctx = Start->getContext();
  auto *Int32Ty = Type::getInt32Ty(Ctx);

  unsigned NumLoops = Loops.size();
  BasicBlock *Body = Start;
  BasicBlock *Latch = End;
  for (unsigned I = 0; I < NumLoops; I++) {
    TI.LoopPreheaders.push_back(Body);
    MustHaveBody = (I == NumLoops - 1) ? true : MustHaveBody;
    Body = CreateLoop(
        Body, Latch, ConstantInt::get(Int32Ty, TI.LoopBounds[I]),
        ConstantInt::get(Int32Ty, TI.LoopSteps[I]),
        ConstantInt::get(Int32Ty, TI.LoopStartIndices[I]), MustHaveBody, "loop",
        DTU, Loops[I], LI);
    Latch = Body->getSingleSuccessor();
    BasicBlock *Header;
    if (MustHaveBody) {
      Header = Body->getSinglePredecessor();
    } else {
      Header = Body;
    }
    TI.LoopLatches.push_back(Latch);
    TI.LoopHeaders.push_back(Header);
    TI.LoopIndices.push_back(&*(Header)->begin());
  }
  TI.InnerLoopBody = Body;
}

static Constant *GetConstantValue(LLVMContext &Ctx, Type *Ty, int64_t Val) {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID:
    return ConstantInt::get(Type::getInt32Ty(Ctx), (int)Val);
  case Type::FloatTyID:
    return ConstantFP::get(Type::getFloatTy(Ctx), (float)Val);
  case Type::DoubleTyID:
    return ConstantFP::get(Type::getDoubleTy(Ctx), (double)Val);
  case Type::HalfTyID:
  case Type::BFloatTyID:
  default:
    assert(false && "Invalid element type.");
  }
  return nullptr;
}

static Value *ConvertToFloat(Value *V, Instruction *InsertBefore) {
  switch (V->getType()->getTypeID()) {
  case Type::IntegerTyID:
    return new SIToFPInst(
        V, Type::getFloatTy(InsertBefore->getParent()->getContext()), "",
        InsertBefore);
  case Type::FloatTyID:
  case Type::DoubleTyID:
    return V;
  case Type::HalfTyID:
  case Type::BFloatTyID:
  default:
    assert(false && "Invalid element type.");
  }
  return nullptr;
}

static int64_t GetMaxFor(Type *Ty) {
  switch(Ty->getTypeID()) {
    case Type::IntegerTyID: 
      switch(Ty->getIntegerBitWidth()) {
        case 1:
          return 1;
        case 8:
          return (int64_t)std::numeric_limits<int8_t>::max();
        case 16:
          return (int64_t)std::numeric_limits<int16_t>::max();
        case 32:
          return (int64_t)std::numeric_limits<int32_t>::max();
        case 64:
          return (int64_t)std::numeric_limits<int64_t>::max();
        default:
          assert(false && "Get max for valid integer type.");
      }
    case Type::FloatTyID:
      return (int64_t)std::numeric_limits<float>::max();
    case Type::DoubleTyID:
      return (int64_t)std::numeric_limits<double>::max();
    default:
      assert(false && "Get max for valid type.");
  }
}

static int64_t GetMinFor(Type *Ty) {
  switch(Ty->getTypeID()) {
    case Type::IntegerTyID: 
      switch(Ty->getIntegerBitWidth()) {
        case 1:
          return 0;
        case 8:
          return (int64_t)std::numeric_limits<int8_t>::min();
        case 16:
          return (int64_t)std::numeric_limits<int16_t>::min();
        case 32:
          return (int64_t)std::numeric_limits<int32_t>::min();
        case 64:
          return (int64_t)std::numeric_limits<int64_t>::min();
        default:
          assert(false && "Get min for valid integer type.");
      }
    case Type::FloatTyID:
      return (int64_t)std::numeric_limits<float>::min();
    case Type::DoubleTyID:
      return (int64_t)std::numeric_limits<double>::min();
    default:
      assert(false && "Get min for valid type.");
  }
}


class LowerTensorIntrinsics {
public:
  Function &Func;
  const DataLayout &DL;
  const TargetTransformInfo &TTI;
  DominatorTree *DT;
  LoopInfo *LI;
  TensorInfo *TI;

private:
  // Track the instructions that need to be removed
  SmallPtrSet<Instruction *, 16> ToBeRemoved;

  class TargetRegInfo {

    // Target register info for 2D tile registers
    std::vector<TensorType> TileRegTypeInfo;

  public:
    TargetRegInfo() {}

    // Find the appropriate tile register that works with the given
    // cache block.
    TensorType &getAptTileRegTensorType(TensorType &CacheTileType) {
      unsigned CacheNumRows = CacheTileType.getShapeVector()[0];
      unsigned CacheNumCols = CacheTileType.getShapeVector()[1];
      for(auto &RegTileType : TileRegTypeInfo) {
        unsigned RegNumRows = RegTileType.getShapeVector()[0];
        unsigned RegNumCols = RegTileType.getShapeVector()[1];
        if(CacheTileType.getLayout() == RegTileType.getLayout()) {
          if(CacheNumRows % RegNumRows == 0 && CacheNumCols % RegNumCols == 0) 
            return RegTileType;
        } else {
          if(CacheNumRows % RegNumCols == 0 && CacheNumCols % RegNumRows == 0) 
            return RegTileType;
        }
      }
      llvm_unreachable("Set the cache dimensions appropriately.");
    }
  };

  // This tracks information about the target registers
  TargetRegInfo TTRegInfo;

public:
  LowerTensorIntrinsics(
      Function &F, TargetTransformInfo &TTI, DominatorTree *DT, LoopInfo *LI,
      TensorInfo *TI)
      : Func(F), DL(F.getParent()->getDataLayout()), TTI(TTI), DT(DT), LI(LI),
        TI(TI) {
          TTRegInfo = TargetRegInfo();
  }

  class CommonTensorInfo {
  public:

    bool isRowMajor(TensorType &Tensor) {
      SmallVector<unsigned, 4> &LayoutVector = Tensor.getLayoutVector();
      unsigned NumDims = LayoutVector.size();
      return (
          (LayoutVector[NumDims - 1] == (NumDims - 1)) &&
          (LayoutVector[NumDims - 2] == (NumDims - 2)));
    }

    bool isColumnMajor(TensorType &Tensor) {
      SmallVector<unsigned, 4> &LayoutVector = Tensor.getLayoutVector();
      unsigned NumDims = LayoutVector.size();
      return (
          (LayoutVector[NumDims - 1] == (NumDims - 2)) &&
          (LayoutVector[NumDims - 2] == (NumDims - 1)));
    }

    unsigned getNumRows(TensorType &Tensor) {
      SmallVector<unsigned, 4> &ShapeVector = Tensor.getShapeVector();
      return ShapeVector[ShapeVector.size() - 2];
    }

    unsigned getNumColumns(TensorType &Tensor) {
      SmallVector<unsigned, 4> &ShapeVector = Tensor.getShapeVector();
      return ShapeVector[ShapeVector.size() - 1];
    }

    unsigned getNumElems(TensorType &Tensor) {
      return getNumRows(Tensor) * getNumColumns(Tensor);
    }

    unsigned getStride(TensorType &Tensor) { return getNumColumns(Tensor); }

    virtual unsigned getNumOutputTiles() const = 0;

    virtual TensorType &getOutputTensor() = 0;

    virtual TensorType &getOutputTile() = 0;

    virtual Value *getOutputTileVector(unsigned Index) = 0;

    virtual Value *getOutput2DTile(unsigned HIndex, unsigned VIndex) = 0;

    virtual SmallVector<Value *, 4> &getOutTensorIndices() = 0;
  };

  class MatMulInfo : public CommonTensorInfo {
  public:
    // Input tensor dimensions
    unsigned LTensorDim;
    unsigned RTensorDim;
    unsigned CommonDim;

    // Input and output tensor type infomation
    TensorType LTensor;
    TensorType RTensor;
    TensorType OutputTensor;

    // Block dimensions
    unsigned LBlockDim;
    unsigned RBlockDim;
    unsigned BlockCommonDim;

    // Indices for the input and output tensors
    // These are useful for indexing into tensors to access blocks.
    SmallVector<Value *, 4> LTensorIndices;
    SmallVector<Value *, 4> RTensorIndices;
    SmallVector<Value *, 4> OutTensorIndices;

    // 1-d Tile vectors
    SmallVector<Value *, 16> LTileVector;
    SmallVector<Value *, 16> RTileVector;
    SmallVector<Value *, 16> OutTiles;
    SmallVector<PHINode *, 16> TilePHIs;

    // Maps for 2D tile registers
    DenseMap<unsigned, std::vector<Value *>> LTileMap;
    DenseMap<unsigned, std::vector<Value *>> RTileMap;
    DenseMap<unsigned, std::vector<Value *>> Out2DTiles;
    DenseMap<unsigned, std::vector<PHINode *>> Tiles2DPHIs;

    // Tensor type info for the block
    TensorType LTile;
    TensorType RTile;
    TensorType OutTile;

    // Target register info for this operation
    TensorType L2DTileReg;
    TensorType R2DTileReg;
    TensorType Out2DTileReg;

    // Number of tile registers along different block dimensions
    unsigned Num2DRegTileRows;
    unsigned Num2DRegTileCols;
    unsigned Num2DRegTileCommon;

    // Loop nest info
    TiledLoopNestInfo LoopNestInfo;

    BasicBlock *getInnerLoopBody() { return LoopNestInfo.InnerLoopBody; }

    BasicBlock *getBlockToStoreTile() {
      return LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 2];
    }

    unsigned getNumOutputTiles() const { return OutTiles.size(); }

    TensorType &getOutputTensor() { return OutputTensor; }

    TensorType &getOutputTile() { return OutTile; }

    Value *getOutputTileVector(unsigned Index) { return OutTiles[Index]; }

    Value *getOutput2DTile(unsigned HIndex, unsigned VIndex)  { return Out2DTiles[HIndex][VIndex]; }

    SmallVector<Value *, 4> &getOutTensorIndices() { return OutTensorIndices; }

    MatMulInfo(
        LLVMContext &Ctx, TensorType &LTensor, TensorType &RTensor,
        SmallVector<unsigned, 4> &OutputLayout)
        : LTensor(LTensor), RTensor(RTensor) {
      if (isColumnMajor(LTensor)) {
        LTensorDim = getNumColumns(LTensor);
        CommonDim = getNumRows(LTensor);
      } else {
        LTensorDim = getNumRows(LTensor);
        CommonDim = getNumColumns(LTensor);
      }

      if (isColumnMajor(RTensor)) {
        RTensorDim = getNumRows(RTensor);
        assert(
            getNumColumns(RTensor) == CommonDim &&
            "Matmul operands must have a common dimension.");
      } else {
        RTensorDim = getNumColumns(RTensor);
        assert(
            getNumRows(RTensor) == CommonDim &&
            "Matmul operands must have a common dimension.");
      }

      // Get the output shape and padding
      SmallVector<unsigned, 4> &ShapeVector = LTensor.getShapeVector();
      SmallVector<unsigned, 4> OutTensorShape;
      SmallVector<unsigned, 4> PaddingVector;
      for (unsigned I = 0; I < ShapeVector.size() - 2; I++) {
        OutTensorShape.push_back(ShapeVector[I]);
        PaddingVector.push_back(0);
      }

      unsigned NumDims = OutputLayout.size();
      bool isOutputColumnMajor =
          ((OutputLayout[NumDims - 1] == (NumDims - 2)) &&
           (OutputLayout[NumDims - 2] == (NumDims - 1)));
      if (isOutputColumnMajor) {
        OutTensorShape.push_back(RTensorDim);
        OutTensorShape.push_back(LTensorDim);
      } else {
        OutTensorShape.push_back(LTensorDim);
        OutTensorShape.push_back(RTensorDim);
      }
      PaddingVector.push_back(0);
      PaddingVector.push_back(0);

      OutputTensor =
          TensorType(Ctx, OutTensorShape, OutputLayout, PaddingVector);
    }

    void createLoopNest(
        LowerTensorIntrinsics &LTI, unsigned TileSize_M, unsigned TileSize_N,
        unsigned TileSize_K, Instruction *InsertBefore) {
      // Create the loop nest information
      createLoopNestInfo(TileSize_M, TileSize_N, TileSize_K);

      // Create the main tiling loop nest.
      DomTreeUpdater DTU(LTI.DT, DomTreeUpdater::UpdateStrategy::Lazy);
      auto *Start = InsertBefore->getParent();
      auto *End = SplitBlock(
          InsertBefore->getParent(), InsertBefore, LTI.DT, LTI.LI, nullptr,
          "continue");
      CreateTiledLoops(Start, End, DTU, *(LTI.LI), LoopNestInfo);

      // Set the tile infomation
      setTilesInfo(InsertBefore->getModule()->getContext());

      // Set the indices information
      setIndicesInfo();

      // Set the target register info
      setRegInfo(InsertBefore->getModule()->getContext());
    }

    void insertTilePHIs(Type *ElemType) {
      unsigned TileRows;
      unsigned TileCols;
      if (isColumnMajor(OutputTensor)) {
        TileRows = RBlockDim;
        TileCols = LBlockDim;
      } else {
        TileRows = LBlockDim;
        TileCols = RBlockDim;
      }

      auto *TileVecTy = FixedVectorType::get(ElemType, TileCols);
      unsigned NumHeaders = LoopNestInfo.LoopHeaders.size();
      BasicBlock *InnerLoopHeader = LoopNestInfo.LoopHeaders[NumHeaders - 1];
      unsigned NumPreheaders = LoopNestInfo.LoopPreheaders.size();
      BasicBlock *InnerLoopPreheader =
          LoopNestInfo.LoopPreheaders[NumPreheaders - 1];
      auto *InnerHeaderTerminator = InnerLoopHeader->getTerminator();
      for (unsigned I = 0; I < TileRows; I++) {
        auto *Phi = PHINode::Create(
            TileVecTy, 2, "result.vec." + Twine(I), InnerHeaderTerminator);
        Phi->addIncoming(
            ConstantAggregateZero::get(TileVecTy), InnerLoopPreheader);
        OutTiles.push_back(Phi);
        TilePHIs.push_back(Phi);
      }
    }

    void completeTilePHIs() {
      BasicBlock *InnerLoopLatch =
          LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 1];
      for (unsigned I = 0; I < OutTiles.size(); I++) {
        TilePHIs[I]->addIncoming(OutTiles[I], InnerLoopLatch);
      }
    }

    void insert2DTilePHIs(Type *ElemType, TensorInfo &TI) {
      unsigned NumHeaders = LoopNestInfo.LoopHeaders.size();
      BasicBlock *InnerLoopHeader = LoopNestInfo.LoopHeaders[NumHeaders - 1];
      unsigned NumPreheaders = LoopNestInfo.LoopPreheaders.size();
      BasicBlock *InnerLoopPreheader =
                    LoopNestInfo.LoopPreheaders[NumPreheaders - 1];
      auto *InnerHeaderTerminator = InnerLoopHeader->getTerminator();

      unsigned TileSize = getNumRows(Out2DTileReg) * getNumColumns(Out2DTileReg);
      auto *TileVecTy = FixedVectorType::get(ElemType, TileSize);
      for (unsigned I = 0; I < Num2DRegTileRows; I++) {
        for (unsigned J = 0; J < Num2DRegTileCols; J++) {
          auto *Phi = PHINode::Create(
              TileVecTy, 2, "result.tile." + Twine(I) + "." + Twine(J), 
              InnerHeaderTerminator);
          Phi->addIncoming(
              ConstantAggregateZero::get(TileVecTy), InnerLoopPreheader);
          Tiles2DPHIs[I].push_back(Phi);
        }
      }
      
      // Insert typeinfo intrinsics after the PHIs
      auto RegPropertiesValVect = Out2DTileReg.getTensorPropertiesValueVector();
      auto RegPropertiesTypeVect = Out2DTileReg.getTensorPropertiesTypeVector();
      std::vector<Type *> TyArgs = {TileVecTy};
      TyArgs.insert(TyArgs.end(), RegPropertiesTypeVect.begin(), RegPropertiesTypeVect.end());
      auto *TypeInfoFunc = Intrinsic::getDeclaration(InnerHeaderTerminator->getModule(), 
                    Intrinsic::tensor_typeinfo, ArrayRef<Type *>(TyArgs));
      errs() << "TYPEINFO INTRINSIC: " << *TypeInfoFunc << "\n";
      for (unsigned I = 0; I < Num2DRegTileRows; I++) {
        for (unsigned J = 0; J < Num2DRegTileCols; J++) {
          std::vector<Value *> Args = {Tiles2DPHIs[I][J]};
          Args.insert(Args.end(), RegPropertiesValVect.begin(), RegPropertiesValVect.end());
          auto *TypeInfo = CallInst::Create(TypeInfoFunc, Args,
                              "tile.phi.typeinfo." + Twine(I) + "." + Twine(J), 
                              InnerHeaderTerminator);
          Out2DTiles[I].push_back(TypeInfo);

          // Add typeinfo info in tensor info
          TI.addTensorInfoFor(Tiles2DPHIs[I][J], Out2DTileReg);
          TI.addTensorInfoFor(TypeInfo, Out2DTileReg);
        }
      }
    }
    
    void complete2DTilePHIs() {
      errs() << "COMPLETE TILE PHIS\n";
      BasicBlock *InnerLoopLatch =
          LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 1];
      for (unsigned I = 0; I < Num2DRegTileRows; I++) {
        for (unsigned J = 0; J < Num2DRegTileCols; J++) {
          // Now get the MMA value. The output values tracked using the map
          // are token values from typeinfo.
          auto *II = dyn_cast<IntrinsicInst>(Out2DTiles[I][J]);
          assert(II && II->getIntrinsicID() == Intrinsic::tensor_typeinfo);
          auto *TileMMA = II->getArgOperand(0);
          Tiles2DPHIs[I][J]->addIncoming(TileMMA, InnerLoopLatch);
          errs() << "--PHI: " << Tiles2DPHIs[I][J] << "\n";
        }
      }
    }

  private:
    void createLoopNestInfo(
        unsigned TileSize_M, unsigned TileSize_N, unsigned TileSize_K) {
      SmallVector<unsigned, 4> LoopStartIndices;
      SmallVector<unsigned, 4> LoopSteps;
      SmallVector<unsigned, 4> LoopBounds;
      SmallVector<unsigned, 4> &OutTensorShape = OutputTensor.getShapeVector();
      for (unsigned I = 0; I < OutTensorShape.size() - 2; I++) {
        LoopBounds.push_back(OutTensorShape[I]);
        LoopSteps.push_back(1);
        LoopStartIndices.push_back(0);
      }
      LoopBounds.insert(LoopBounds.end(), {LTensorDim, RTensorDim, CommonDim});
      LoopSteps.insert(LoopSteps.end(), {TileSize_M, TileSize_N, TileSize_K});
      LoopStartIndices.insert(LoopStartIndices.end(), {0, 0, 0});

      LoopNestInfo = TiledLoopNestInfo(LoopBounds, LoopSteps, LoopStartIndices);

      LBlockDim = TileSize_M;
      RBlockDim = TileSize_N;
      BlockCommonDim = TileSize_K;
    }

    void setIndicesInfo() {
      unsigned NumIndices = LoopNestInfo.LoopIndices.size();
      for (unsigned I = 0; I < NumIndices - 3; I++) {
        LTensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
        RTensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
        OutTensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
      }

      if (isColumnMajor(LTensor)) {
        LTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 1]); // K
        LTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 3]); // M
      } else {
        LTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 3]); // M
        LTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 1]); // K
      }

      if (isColumnMajor(RTensor)) {
        RTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 2]); // N
        RTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 1]); // K
      } else {
        RTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 1]); // K
        RTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 2]); // N
      }

      OutTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 3]); // M
      OutTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 2]); // N
    }

    void setTilesInfo(LLVMContext &Ctx) {
      // Set up the shape and layout vectors for left hand side tile
      SmallVector<unsigned, 4> ShapeVector;
      SmallVector<unsigned, 4> LayoutVector;
      if (isColumnMajor(LTensor)) {
        ShapeVector.push_back(BlockCommonDim);
        ShapeVector.push_back(LBlockDim);
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        ShapeVector.push_back(LBlockDim);
        ShapeVector.push_back(BlockCommonDim);
        LayoutVector.push_back(0);
        LayoutVector.push_back(1);
      }

      // Padding is zero. Tiles are not assumed to be padded.
      SmallVector<unsigned, 4> PaddingVector;
      PaddingVector.push_back(0);
      PaddingVector.push_back(0);

      // Set the tensor type information for the left hand side tile
      LTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);

      // Set up the shape and layout vectors for right hand side tile
      ShapeVector.clear();
      LayoutVector.clear();
      if (isColumnMajor(RTensor)) {
        ShapeVector.push_back(RBlockDim);
        ShapeVector.push_back(BlockCommonDim);
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        ShapeVector.push_back(BlockCommonDim);
        ShapeVector.push_back(RBlockDim);
        LayoutVector.push_back(0);
        LayoutVector.push_back(1);
      }

      // Set the tensor type information for the right hand side tile
      RTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);

      // Set the layout and shape information of the output tile
      ShapeVector.clear();
      LayoutVector.clear();
      if (isColumnMajor(OutputTensor)) {
        // Column major case
        ShapeVector.push_back(RBlockDim);
        ShapeVector.push_back(LBlockDim);
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        // Row major case
        ShapeVector.push_back(LBlockDim);
        ShapeVector.push_back(RBlockDim);
        LayoutVector.push_back(0);
        LayoutVector.push_back(1);
      }

      // Set the tensor type information for the output tile
      OutTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);
    }

    void setRegInfo(LLVMContext &Ctx) {
        SmallVector<unsigned, 4> ShapeVector {2, 2};
        SmallVector<unsigned, 4> LayoutVector {0, 1};
        SmallVector<unsigned, 4> PaddingVector {0, 0};
        L2DTileReg = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);
        R2DTileReg = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);
        Out2DTileReg = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);
        //L2DTileReg = TTRegInfo.getAptTileRegTensorType(LTile);
        //R2DTileReg = TTRegInfo.getAptTileRegTensorType(RTile);
        //Out2DTileReg = TTRegInfo.getAptTileRegTensorType(OutTile);

        Num2DRegTileRows = getNumRows(OutTile) / getNumRows(Out2DTileReg);
        Num2DRegTileCols = getNumColumns(OutTile) / getNumColumns(Out2DTileReg);
        Num2DRegTileCommon = getNumColumns(LTile) / getNumColumns(L2DTileReg);
    }
  };

  class ElementWiseInfo : public CommonTensorInfo {
  public:
    // Input/output tensor type infomation
    TensorType Tensor;

    // Tile size
    unsigned TileSize;

    // Tiled loop nest info
    TiledLoopNestInfo LoopNestInfo;

    // PHI node repreesenting an intermediate tensor
    PHINode *PHITensor;

    // Indices for indexing into the tensor
    SmallVector<Value *, 4> TensorIndices;

    ElementWiseInfo(TensorType &Tensor) : Tensor(Tensor) {}

    void createLoopNest(
        LowerTensorIntrinsics &LTI, unsigned TileSize,
        Instruction *InsertBefore) {
      // Create the loop nest information
      createLoopNestInfo(TileSize);

      // Create the main tiling loop nest.
      DomTreeUpdater DTU(LTI.DT, DomTreeUpdater::UpdateStrategy::Lazy);
      auto *Start = InsertBefore->getParent();
      auto *End = SplitBlock(
          InsertBefore->getParent(), InsertBefore, LTI.DT, LTI.LI, nullptr,
          "continue");
      CreateTiledLoops(Start, End, DTU, *(LTI.LI), LoopNestInfo);

      // Set the indices information
      setIndicesInfo();
    }

    BasicBlock *getInnerLoopBody() { return LoopNestInfo.InnerLoopBody; }

    unsigned getNumLoopsCollapsed() {
      // All dimensions are collapsed into one for element-wise operatons.
      return (Tensor.getNumDimensions() - 1);
    }

    unsigned getNumOutputTiles() const { return 0; }

    TensorType &getOutputTensor() { return *(new TensorType()); }

    TensorType &getOutputTile() { return *(new TensorType()); }

    Value *getOutputTileVector(unsigned Index) { return nullptr; }

    Value *getOutput2DTile(unsigned HIndex, unsigned VIndex)  { return nullptr; }

    SmallVector<Value *, 4> &getOutTensorIndices() { return TensorIndices; }

    void insertTensorPHI(Value *InputTensor, Type *ElemType) {
      BasicBlock *InnerLoopHeader = LoopNestInfo.LoopHeaders[0];
      BasicBlock *Preheader = LoopNestInfo.LoopPreheaders[0];
      auto *InnerHeaderTerminator = InnerLoopHeader->getTerminator();
      PHITensor = PHINode::Create(
          InputTensor->getType(), 2, "result.vec.", InnerHeaderTerminator);
      PHITensor->addIncoming(InputTensor, Preheader);
    }

    void completeTensorPHI(Value *IncomingVal) {
      BasicBlock *InnerLoopLatch =
          LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 1];
      PHITensor->addIncoming(IncomingVal, InnerLoopLatch);
    }

  private:
    void createLoopNestInfo(unsigned TileSize) {
      // We generate a collapsed loop with all dimensions collapsed
      SmallVector<unsigned, 4> LoopStartIndices;
      SmallVector<unsigned, 4> LoopSteps;
      SmallVector<unsigned, 4> LoopBounds;
      LoopBounds.push_back(Tensor.getTensorSize());
      LoopSteps.push_back(TileSize);
      LoopStartIndices.push_back(0);
      LoopNestInfo = TiledLoopNestInfo(LoopBounds, LoopSteps, LoopStartIndices);

      this->TileSize = TileSize;
    }

    void setIndicesInfo() {
      for (unsigned I = 0; I < LoopNestInfo.LoopIndices.size(); I++) {
        TensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
      }
    }
  };

  class TensorTransformInfo : public CommonTensorInfo {
  public:
    // Input and output tensor type information
    TensorType InputTensor;
    TensorType OutputTensor;

    // Input and output tile tensor type information
    TensorType InTile;
    TensorType OutTile;

    // Tile sizes
    unsigned InputRowTileDim;
    unsigned InputColTileDim;

    // PHI nodes for the input and output tiles
    SmallVector<Value *, 16> InTileVector;
    SmallVector<Value *, 16> OutTiles;

    // Tiled loop nest info
    TiledLoopNestInfo LoopNestInfo;

    // Indices for indexing into the tensors
    SmallVector<Value *, 4> InTensorIndices;
    SmallVector<Value *, 4> OutTensorIndices;

    unsigned getNumOutputTiles() const { return OutTiles.size(); }

    TensorType &getOutputTensor() { return OutputTensor; }

    TensorType &getOutputTile() { return OutTile; }

    Value *getOutputTileVector(unsigned Index) { return OutTiles[Index]; }

    Value *getOutput2DTile(unsigned HIndex, unsigned VIndex)  { return nullptr; }

    SmallVector<Value *, 4> &getOutTensorIndices() { return OutTensorIndices; }

    bool isValidTranspose(TensorType &InTensor, TensorType &OutTensor) {
      // Only simple permulation between rows and columns is supported
      if (isRowMajor(InTensor) == isColumnMajor(OutTensor) ||
          isColumnMajor(InTensor) == isRowMajor(OutTensor)) {
        return true;
      }
      return false;
    }

    TensorTransformInfo(TensorType &InTensor, TensorType &OutTensor)
        : InputTensor(InTensor), OutputTensor(OutTensor) {
      assert(isValidTranspose(InputTensor, OutputTensor) &&
          "Cannot create loop nest for invalid transposes.");
    }

    void createLoopNest(
        LowerTensorIntrinsics &LTI, unsigned TileSize_M, unsigned TileSize_N,
        Instruction *InsertBefore) {
      // Create the loop nest information
      createLoopNestInfo(TileSize_M, TileSize_N);

      // Create the main tiling loop nest.
      DomTreeUpdater DTU(LTI.DT, DomTreeUpdater::UpdateStrategy::Lazy);
      auto *Start = InsertBefore->getParent();
      auto *End = SplitBlock(
          InsertBefore->getParent(), InsertBefore, LTI.DT, LTI.LI, nullptr,
          "continue");
      CreateTiledLoops(Start, End, DTU, *(LTI.LI), LoopNestInfo);

      // Set the indices information
      setIndicesInfo();

      setTilesInfo(InsertBefore->getParent()->getContext());
    }

    BasicBlock *getInnerLoopBody() { return LoopNestInfo.InnerLoopBody; }

    void initOutputTiles(Type *ElemType) {
      LLVM_DEBUG(dbgs() << "INSERTING PHIs FOR TRANSPOSE\n");
      // This is a simple transpose
      unsigned TileRows = InputColTileDim;
      unsigned TileCols = InputRowTileDim;

      auto *TileVecTy = FixedVectorType::get(ElemType, TileCols);
      for (unsigned I = 0; I < TileRows; I++) {
        OutTiles.push_back(UndefValue::get(TileVecTy));
      }
    }

  private:
    void createLoopNestInfo(unsigned TileSize_M, unsigned TileSize_N) {
      SmallVector<unsigned, 4> LoopStartIndices;
      SmallVector<unsigned, 4> LoopSteps;
      SmallVector<unsigned, 4> LoopBounds;
      SmallVector<unsigned, 4> &TensorShape = InputTensor.getShapeVector();
      unsigned NumDims = TensorShape.size();
      for (unsigned I = 0; I < NumDims - 2; I++) {
        LoopBounds.push_back(TensorShape[I]);
        LoopSteps.push_back(1);
        LoopStartIndices.push_back(0);
      }

      // Last innermost loops
      LoopBounds.push_back(TensorShape[NumDims - 2]);
      LoopSteps.push_back(TileSize_M);
      LoopStartIndices.push_back(0);
      LoopBounds.push_back(TensorShape[NumDims - 1]);
      LoopSteps.push_back(TileSize_N);
      LoopStartIndices.push_back(0);

      LoopNestInfo = TiledLoopNestInfo(LoopBounds, LoopSteps, LoopStartIndices);
      InputRowTileDim = TileSize_M;
      InputColTileDim = TileSize_N;
    }

    void setIndicesInfo() {
      unsigned NumLoopIndices = LoopNestInfo.LoopIndices.size();
      for (unsigned I = 0; I < NumLoopIndices; I++) {
        InTensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
      }
      for (unsigned I = 0; I < NumLoopIndices - 2; I++) {
        OutTensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
      }
      OutTensorIndices.push_back(LoopNestInfo.LoopIndices[NumLoopIndices - 1]);
      OutTensorIndices.push_back(LoopNestInfo.LoopIndices[NumLoopIndices - 2]);
    }

    void setTilesInfo(LLVMContext &Ctx) {
      // Set up the shape vector for input tile
      SmallVector<unsigned, 4> ShapeVector;
      ShapeVector.push_back(InputRowTileDim);
      ShapeVector.push_back(InputColTileDim);

      // Set up the layout vector for input tile
      SmallVector<unsigned, 4> LayoutVector;
      if (isColumnMajor(InputTensor)) {
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        LayoutVector.push_back(0);
        LayoutVector.push_back(1);
      }

      // Padding is zero. Tiles are not assumed to be padded.
      SmallVector<unsigned, 4> PaddingVector;
      PaddingVector.push_back(0);
      PaddingVector.push_back(0);

      // Set the tensor type information for the input tile
      InTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);

      // Set the shape information of the output tile
      ShapeVector.clear();
      ShapeVector.push_back(InputColTileDim);
      ShapeVector.push_back(InputRowTileDim);

      // Set the layout information of the output tile
      LayoutVector.clear();
      if (isColumnMajor(OutputTensor)) {
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        LayoutVector.push_back(0);
        LayoutVector.push_back(1);
      }

      // Set the tensor type information for the output tile
      OutTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);
    }
  };

    class ReductionInfo : public CommonTensorInfo {
  public:
    // Input and output tensor type infomation
    TensorType InputTensor;
    TensorType OutputTensor;

    // Sliding window shape and strides
    SmallVector<unsigned, 4> WindowShape;
    SmallVector<unsigned, 4> WindowStrides;

    // Block dimensions
    unsigned NumBlockRows;
    unsigned NumBlockCols;

    // Tensor type info for the block
    TensorType InTile;
    TensorType OutTile;

    // Indices for the input and output tensors
    // These are useful for indexing into tensors to access blocks.
    SmallVector<Value *, 4> InTensorIndices;
    SmallVector<Value *, 4> WinTensorIndices;
    SmallVector<Value *, 4> OutTensorIndices;

    // 1-d Tile vectors
    SmallVector<Value *, 16> InTileVector;
    Value *OutTiles;
    SmallVector<PHINode *, 2> TilePHIs;

    // Loop nest info
    TiledLoopNestInfo LoopNestInfo;

    BasicBlock *getInnerLoopBody() { return LoopNestInfo.InnerLoopBody; }

    BasicBlock *getBlockToStoreTile() {
      return LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 3];
    }

    unsigned getNumOutputTiles() const { return 1; }//OutTiles.size(); }

    TensorType &getOutputTensor() { return OutputTensor; }

    TensorType &getOutputTile() { return OutTile; }

    Value *getOutputTileVector(unsigned Index) { return OutTiles; }//OutTiles[Index]; }

    Value *getOutput2DTile(unsigned HIndex, unsigned VIndex)  { return  nullptr; }

    SmallVector<Value *, 4> &getOutTensorIndices() { return OutTensorIndices; }

    ReductionInfo(
        LLVMContext &Ctx, TensorType &InTensor, Value *WinShape,
        Value *WinStrides, SmallVector<unsigned, 4> &OutputLayout)
        : InputTensor(InTensor) {
      
      assert(dyn_cast<ConstantDataVector>(WinShape) 
          && "Window for reduction must be a constant vector.");
      assert(dyn_cast<ConstantDataVector>(WinStrides) 
          && "Strides for reduction must be a constant vector.");
      
      // Get the window shape and strides
      WindowShape = getVectorFromValue(WinShape);
      WindowStrides = getVectorFromValue(WinStrides);
      
      // Get the output shape and padding
      SmallVector<unsigned, 4> &InShapeVector = InTensor.getShapeVector();
      SmallVector<unsigned, 4> OutTensorShape;
      SmallVector<unsigned, 4> PaddingVector;
      for (unsigned I = 0; I < InShapeVector.size() - 2; I++) {
        OutTensorShape.push_back(InShapeVector[I]);
        PaddingVector.push_back(0);
      }
      PaddingVector.insert(PaddingVector.end(), {0, 0});

      // Use the formula to get the size of the lower 2 dimensions of the output
      unsigned NumWinDims = WindowShape.size();
      unsigned NumInDims = InShapeVector.size();
      unsigned OutputSize = ((InShapeVector[NumInDims - 2]
                  - WindowShape[NumWinDims - 2]) / WindowStrides[NumWinDims - 2]) + 1;
      OutTensorShape.push_back(OutputSize);
      OutputSize = ((InShapeVector[NumInDims - 1]
                  - WindowShape[NumWinDims - 1]) / WindowStrides[NumWinDims - 1]) + 1;
      OutTensorShape.push_back(OutputSize);

      OutputTensor =
          TensorType(Ctx, OutTensorShape, OutputLayout, PaddingVector);
    }

    void createLoopNest(
        LowerTensorIntrinsics &LTI, unsigned TileSize_M, unsigned TileSize_N,
                              Instruction *InsertBefore) {
      // Create the loop nest information
      createLoopNestInfo(TileSize_M, TileSize_N);

      // Create the main tiling loop nest.
      DomTreeUpdater DTU(LTI.DT, DomTreeUpdater::UpdateStrategy::Lazy);
      auto *Start = InsertBefore->getParent();
      auto *End = SplitBlock(
          InsertBefore->getParent(), InsertBefore, LTI.DT, LTI.LI, nullptr,
          "continue");
      CreateTiledLoops(Start, End, DTU, *(LTI.LI), LoopNestInfo);

      // Set the tile infomation
      setTilesInfo(InsertBefore->getModule()->getContext());

      // Set the indices information
      setIndicesInfo();
    }

    void insertTilePHIs(Type *ElemType, int64_t InitVal) {
      // Add the first PHI
      unsigned NumHeaders = LoopNestInfo.LoopHeaders.size();
      unsigned NumPreheaders = LoopNestInfo.LoopPreheaders.size();
      BasicBlock *InnerLoopHeader = LoopNestInfo.LoopHeaders[NumHeaders - 2];
      BasicBlock *InnerLoopPreheader =
          LoopNestInfo.LoopPreheaders[NumPreheaders - 2];
      auto *InnerHeaderTerminator = InnerLoopHeader->getTerminator();
      auto *FirstPHI = PHINode::Create(ElemType, 2, "result.elem.outer", InnerHeaderTerminator);
      FirstPHI->addIncoming(GetConstantValue(InnerHeaderTerminator->getModule()->getContext(),
                                        ElemType, InitVal), InnerLoopPreheader);
      TilePHIs.push_back(FirstPHI);

      // Add the second PHI
      InnerLoopHeader = LoopNestInfo.LoopHeaders[NumHeaders - 1];
      InnerLoopPreheader = LoopNestInfo.LoopPreheaders[NumPreheaders - 1];
      InnerHeaderTerminator = InnerLoopHeader->getTerminator();
      auto *SecPHI = PHINode::Create(ElemType, 2, "result.elem.inner", InnerHeaderTerminator);
      SecPHI->addIncoming(FirstPHI, InnerLoopPreheader);
      TilePHIs.push_back(SecPHI);
      OutTiles = SecPHI;
    }

    void completeTilePHIs() {
      // Now complete the second PHI
      BasicBlock *InnerLoopLatch =
          LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 1];
      TilePHIs[1]->addIncoming(OutTiles, InnerLoopLatch);

      // Now complete the first PHI
      InnerLoopLatch =
          LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 2];
      TilePHIs[0]->addIncoming(OutTiles, InnerLoopLatch);
    }
    
  private:
    void createLoopNestInfo(unsigned TileSize_M, unsigned TileSize_N) {
      auto NumOutRows = getNumRows(OutputTensor);
      auto NumOutCols = getNumColumns(OutputTensor);
      unsigned NumWinRows = WindowShape[WindowShape.size() - 2];
      unsigned NumWinCols = WindowShape[WindowShape.size() - 1];
      SmallVector<unsigned, 4> LoopStartIndices;
      SmallVector<unsigned, 4> LoopSteps;
      SmallVector<unsigned, 4> LoopBounds;
      SmallVector<unsigned, 4> &OutTensorShape = OutputTensor.getShapeVector();
      for (unsigned I = 0; I < OutTensorShape.size() - 2; I++) {
        LoopBounds.push_back(OutTensorShape[I]);
        LoopSteps.push_back(1);
        LoopStartIndices.push_back(0);
      }
      LoopBounds.insert(LoopBounds.end(), {NumOutRows, NumOutCols, 
                                           NumWinRows, NumWinCols});
      LoopSteps.insert(LoopSteps.end(), {1, 1, TileSize_M, TileSize_N});
      LoopStartIndices.insert(LoopStartIndices.end(), {0, 0, 0, 0});

      LoopNestInfo = TiledLoopNestInfo(LoopBounds, LoopSteps, LoopStartIndices);

      NumBlockRows = TileSize_M;
      NumBlockCols = TileSize_N;
    }

    void setIndicesInfo() {
      unsigned NumIndices = LoopNestInfo.LoopIndices.size();
      for (unsigned I = 0; I < NumIndices - 4; I++) {
        InTensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
        OutTensorIndices.push_back(LoopNestInfo.LoopIndices[I]);
      }
      InTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 4]);
      InTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 3]);

      // Indices for the window
      WinTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 2]);
      WinTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 1]);

      // Indices for the output
      OutTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 4]);
      OutTensorIndices.push_back(LoopNestInfo.LoopIndices[NumIndices - 3]);
    }

    void setTilesInfo(LLVMContext &Ctx) {
      // Set up the shape and layout vectors
      SmallVector<unsigned, 4> ShapeVector = { NumBlockRows, NumBlockCols };
      SmallVector<unsigned, 4> LayoutVector = {0, 1};

      // Padding is zero. Tiles are not assumed to be padded.
      SmallVector<unsigned, 4> PaddingVector = {0, 0};

      // Set the tensor type information for the left hand side tile
      InTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);

      // Set the layout and shape information of the output tile
      ShapeVector = {1, 1};
      LayoutVector = {0, 1};

      // Set the tensor type information for the output tile
      OutTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);
    }

    SmallVector<unsigned, 4> getVectorFromValue(Value *Vector) {
      SmallVector<unsigned, 4> Vect;
      auto *VectorTy = dyn_cast<FixedVectorType>(Vector->getType());
      auto *CV = dyn_cast<ConstantDataVector>(Vector);
      for(unsigned I = 0; I < VectorTy->getNumElements(); I++) {
          auto *C = CV->getAggregateElement(I);
          Vect.push_back(dyn_cast<ConstantInt>(C)->getZExtValue());
      }
      return Vect;
    }
  };

  Align getAlignForIndex(
      unsigned Idx, Value *Stride, Type *ElementTy, MaybeAlign A) const {
    Align InitialAlign = DL.getValueOrABITypeAlignment(A, ElementTy);
    if (Idx == 0)
      return InitialAlign;

    TypeSize ElementSizeInBits = DL.getTypeSizeInBits(ElementTy);
    if (auto *ConstStride = dyn_cast<ConstantInt>(Stride)) {
      uint64_t StrideInBytes =
          ConstStride->getZExtValue() * ElementSizeInBits / 8;
      return commonAlignment(InitialAlign, Idx * StrideInBytes);
    }
    return commonAlignment(InitialAlign, ElementSizeInBits / 8);
  }

  Value *computeVectorAddr(
      Value *BasePtr, Value *Index, Value *TensorStride, unsigned NumElements,
      Type *EltType, Instruction *InsertBefore) {
    assert(
        (!dyn_cast<ConstantInt>(TensorStride) ||
         dyn_cast<ConstantInt>(TensorStride)->getZExtValue() >= NumElements) &&
        "Stride must be >= the number of elements in the result vector.");

    // Get pointer to the start of the selected vector. Skip GEP creation,
    // if we select vector 0.
    Instruction *VecStart;
    if (dyn_cast<ConstantInt>(Index) &&
        dyn_cast<ConstantInt>(Index)->isZero()) {
      VecStart = dyn_cast<Instruction>(BasePtr);
    } else {
      auto *Offset = BinaryOperator::Create(
          Instruction::Mul, Index, TensorStride, "vec.start", InsertBefore);
      VecStart = GetElementPtrInst::Create(
          EltType, BasePtr, Offset, "vec.gep", InsertBefore);
    }

    // Cast elementwise vector start pointer to a pointer to a vector
    // (EltType x NumElements)*.
    if(NumElements == 1)
      return VecStart;
    unsigned AS = dyn_cast<PointerType>(BasePtr->getType())->getAddressSpace();
    auto *VecPtrType = PointerType::get(
        FixedVectorType::get(EltType, NumElements), AS);
    return CastInst::CreatePointerCast(
        VecStart, VecPtrType, "vec.cast", InsertBefore);
  }

  Value *computeTileAddr(
      Value *BasePtr, Value *ColIndex, Value *RowIndex, Value *TensorStride,
      unsigned NumElements, Type *EltType, Instruction *InsertBefore) {
    assert(
        (!dyn_cast<ConstantInt>(TensorStride) ||
         dyn_cast<ConstantInt>(TensorStride)->getZExtValue() >= NumElements) &&
        "Stride must be >= the number of elements in the result vector.");

    // Get pointer to the start of the selected vector. Skip GEP creation,
    // if we select vector 0.
    Instruction *VecStart;
    if (dyn_cast<ConstantInt>(ColIndex)->getZExtValue() == 0) {
      if(dyn_cast<ConstantInt>(RowIndex)->getZExtValue() == 0) {
        VecStart = dyn_cast<Instruction>(BasePtr);
      } else {
        VecStart = GetElementPtrInst::Create(
          EltType, BasePtr, ColIndex, "tile.gep", InsertBefore);
      }
    } else {
      Value *Offset;
      if(dyn_cast<ConstantInt>(RowIndex)->getZExtValue() != 0) {
        Offset = BinaryOperator::Create(
            Instruction::Mul, RowIndex, TensorStride, "tile.stride", InsertBefore);
        Offset = BinaryOperator::Create(
            Instruction::Add, ColIndex, Offset, "tile.offset", InsertBefore);
      } else {
        Offset = ColIndex;
      }
      VecStart = GetElementPtrInst::Create(
          EltType, BasePtr, Offset, "tile.gep", InsertBefore);
    }

    // Cast elementwise vector start pointer to a pointer to a vector i8*.
    unsigned AS = dyn_cast<PointerType>(BasePtr->getType())->getAddressSpace();
    auto *PtrTy = PointerType::get(
            Type::getInt8Ty(InsertBefore->getParent()->getContext()), AS);
    return CastInst::CreatePointerCast(
            VecStart, PtrTy, "tile.cast", InsertBefore);
  }

  /// Indices to index into the given tensor are assumed to be from outermost
  /// dimensions to innermost dimensions.
  Value *computeIndex(
      TensorType &Tensor, SmallVector<Value *, 4> &InductionVars,
      unsigned NumCollapsedLoops, Instruction *InsertBefore) {
    SmallVector<unsigned, 4> &Shape = Tensor.getShapeVector();
    unsigned NumDims = Shape.size();
    assert(InductionVars.size() == (NumDims - NumCollapsedLoops) &&
        "The number indices provided must be same as number of dimensions.");

    // If there is only one index, then we just return the index
    unsigned NumIndices = InductionVars.size();
    if (NumIndices == 1) {
      return InductionVars[0];
    }

    // Get the running product of the tensor dimensions
    unsigned ProdDims = Shape[NumDims - 1];
    for (unsigned I = 0; I < NumCollapsedLoops; I++) {
      ProdDims *= Shape[NumDims - I - 2];
    }

    // First compute the index into the feature map
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    unsigned Coefficient = ProdDims;
    auto *ConstCoefficient = ConstantInt::get(Int32Ty, Coefficient);
    auto *Prod = BinaryOperator::Create(
        Instruction::Mul, InductionVars[NumIndices - 2], ConstCoefficient,
        "input.stride", InsertBefore);
    auto *Offset = BinaryOperator::Create(
        Instruction::Add, Prod, InductionVars[NumIndices - 1], "input.offset",
        InsertBefore);

    // Iterate over rest of the feature maps
    for (int I = NumIndices - 3; I >= 0; I--) {
      Coefficient *= Shape[I + 1];
      ConstCoefficient = ConstantInt::get(Int32Ty, Coefficient);
      Prod = BinaryOperator::Create(
          Instruction::Mul, InductionVars[I], ConstCoefficient, "input.stride",
          InsertBefore);
      Offset = BinaryOperator::Create(
          Instruction::Add, Prod, Offset, "input.offset", InsertBefore);
    }

    return Offset;
  }

  /// Indices to index into the given tensor are assumed to be from outermost
  /// dimensions to innermost dimensions.
  Value *computeIndex(
      TensorType &Tensor, SmallVector<Value *, 4> &InductionVars,
      SmallVector<Value *, 4> &WinInductionVars, 
      SmallVector<unsigned, 4> &WinStrides, Instruction *InsertBefore) {
    
    SmallVector<unsigned, 4> &Shape = Tensor.getShapeVector();
    unsigned NumDims = Shape.size();
    assert(WinInductionVars.size() == WinStrides.size() &&
        "The number strides provided must be same as number of window dimensions.");

    // Multiply the induction variables with the strides
    unsigned NumIndices = InductionVars.size();
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *HStride = ConstantInt::get(Int32Ty, WinStrides[WinStrides.size() - 1]);
    auto *HStrideProd = BinaryOperator::Create(
        Instruction::Mul, InductionVars[NumIndices - 1], HStride,
        "input.h.stride", InsertBefore);
    auto *VStride = ConstantInt::get(Int32Ty, WinStrides[WinStrides.size() - 2]);
    auto *VStrideProd = BinaryOperator::Create(
        Instruction::Mul, InductionVars[NumIndices - 2], VStride,
        "input.h.stride", InsertBefore);

    // First compute the index into the feature map
    unsigned Coefficient = Shape[NumDims - 1];
    auto *ConstCoefficient = ConstantInt::get(Int32Ty, Coefficient);
    auto *InProd = BinaryOperator::Create(
        Instruction::Mul, VStrideProd, ConstCoefficient,
        "input.v.stride", InsertBefore);
    auto *InOffset = BinaryOperator::Create(
        Instruction::Add, InProd, HStrideProd, "input.offset",
        InsertBefore);

    // Add the index into the window
    unsigned NumWinIndices = WinInductionVars.size();
    auto *WinProd = BinaryOperator::Create(
        Instruction::Mul, WinInductionVars[NumWinIndices - 2], ConstCoefficient,
        "win.stride", InsertBefore);
    auto *WinOffset = BinaryOperator::Create(
        Instruction::Add, WinProd, WinInductionVars[NumWinIndices - 1], 
        "win.offset", InsertBefore);
    auto *Offset = BinaryOperator::Create(
        Instruction::Add, InOffset, WinOffset, "full.offset", InsertBefore);

    // Iterate over rest of the feature maps
    for (int I = NumIndices - 3; I >= 0; I--) {
      Coefficient *= Shape[I + 1];
      ConstCoefficient = ConstantInt::get(Int32Ty, Coefficient);
      auto *Prod = BinaryOperator::Create(
          Instruction::Mul, InductionVars[I], ConstCoefficient, "input.high.stride",
          InsertBefore);
      Offset = BinaryOperator::Create(
          Instruction::Add, Prod, Offset, "input.high.offset", InsertBefore);
    }
    return Offset;
  }

  template <typename T>
  SmallVector<Value *, 16> loadTile(
      T &TensorOpInfo, Value *TensorPtr, TensorType &InTensor,
      TensorType &InBlock, Type *EltTy, SmallVector<Value *, 4> &Indices,
      SmallVector<Value *, 4> &WinInductionVars, SmallVector<unsigned, 4> &WinStrides, 
      MaybeAlign Align, bool IsVolatile, Instruction *InsertBefore) {

    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Offset = computeIndex(InTensor, Indices, 
                      WinInductionVars, WinStrides, InsertBefore);
    auto *TileStart = GetElementPtrInst::Create(
        EltTy, TensorPtr, ArrayRef<Value *>(Offset), "tile.start", InsertBefore);

    SmallVector<Value *, 16> Result;
    Type *LoadTy;
    if (TensorOpInfo.getStride(InBlock) == 1)
      LoadTy = EltTy;
    else
      LoadTy = FixedVectorType::get(EltTy, TensorOpInfo.getStride(InBlock));
    auto *Stride = ConstantInt::get(Int32Ty, TensorOpInfo.getStride(InTensor));
    for (unsigned I = 0; I < TensorOpInfo.getNumRows(InBlock); ++I) {
      auto *GEP = computeVectorAddr(
          TileStart, ConstantInt::get(Int32Ty, I), Stride,
          TensorOpInfo.getStride(InBlock), EltTy, InsertBefore); 
      auto *Vector = new LoadInst(
          LoadTy, GEP, "row.load", IsVolatile,
          getAlignForIndex(I, Stride, EltTy, Align), InsertBefore);
      Result.push_back(Vector);
    }
    return Result;
  }

  template <typename T>
  SmallVector<Value *, 16> loadTile(
      T &TensorOpInfo, Value *TensorPtr, TensorType &InTensor,
      TensorType &InBlock, Type *EltTy, SmallVector<Value *, 4> &Indices,
      MaybeAlign Align, bool IsVolatile, Instruction *InsertBefore) {

    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Offset = computeIndex(InTensor, Indices, false, InsertBefore);
    auto *TileStart = GetElementPtrInst::Create(
        EltTy, TensorPtr, ArrayRef<Value *>(Offset), "tile.start", InsertBefore);

    SmallVector<Value *, 16> Result;
    Type *LoadTy;
    if (TensorOpInfo.getStride(InBlock) == 1)
      LoadTy = EltTy;
    else
      LoadTy = FixedVectorType::get(EltTy, TensorOpInfo.getStride(InBlock));
    auto *Stride = ConstantInt::get(Int32Ty, TensorOpInfo.getStride(InTensor));
    for (unsigned I = 0; I < TensorOpInfo.getNumRows(InBlock); ++I) {
      auto *GEP = computeVectorAddr(
          TileStart, ConstantInt::get(Int32Ty, I), Stride,
          TensorOpInfo.getStride(InBlock), EltTy, InsertBefore); 
      auto *Vector = new LoadInst(
          LoadTy, GEP, "row.load", IsVolatile,
          getAlignForIndex(I, Stride, EltTy, Align), InsertBefore);
      Result.push_back(Vector);
    }
    return Result;
  }

  template <typename T>
  SmallVector<Value *, 16> load2DTile(
      T &TensorOpInfo, Value *TensorPtr, TensorType &InTensor,
      TensorType &InBlock, TensorType &RegTile, Type *EltTy, 
      SmallVector<Value *, 4> &Indices, 
      DenseMap<unsigned, std::vector<Value *>> &LoadMap,
      MaybeAlign Align, bool IsVolatile, const Twine Name,
      Instruction *InsertBefore) {
    
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Offset = computeIndex(InTensor, Indices, false, InsertBefore);
    auto *TileStart = GetElementPtrInst::Create(
        EltTy, TensorPtr, ArrayRef<Value *>(Offset), "tile.start", InsertBefore);
 
    // Determine the size of the  tile registers
    auto RegTileTensorTypeValVect = RegTile.getTensorPropertiesValueVector();
    auto RegTileTensorTypeTypeVect = RegTile.getTensorPropertiesTypeVector();
    auto *TensorStride = ConstantInt::get(Int32Ty, TensorOpInfo.getStride(InTensor));
    auto *StridesVector = ConstantVector::get(
                  ArrayRef<Constant *>({ConstantInt::get(Int32Ty, 0), TensorStride}));
    unsigned BlockNumRows = TensorOpInfo.getNumRows(InBlock);
    unsigned BlockNumCols = TensorOpInfo.getNumColumns(InBlock); 
    unsigned RegNumRows = TensorOpInfo.getNumRows(RegTile);
    unsigned RegNumCols = TensorOpInfo.getNumColumns(RegTile);
    auto *StrideVectTy = FixedVectorType::get(
        Type::getInt32Ty(InsertBefore->getParent()->getContext()), 2);
    SmallVector<Value *, 16> Result;
    for (unsigned J = 0; J < BlockNumRows; J += RegNumRows) {
      for (unsigned I = 0; I < BlockNumCols; I += RegNumCols) {
        auto *GEP = computeTileAddr(
            TileStart, ConstantInt::get(Int32Ty, I), ConstantInt::get(Int32Ty, J), 
            TensorStride, TensorOpInfo.getNumElems(InBlock), EltTy, InsertBefore);
        std::vector<Type *> ArgsTy = {GEP->getType()};
        ArgsTy.insert(ArgsTy.end(), RegTileTensorTypeTypeVect.begin(), RegTileTensorTypeTypeVect.end());
        ArgsTy.insert(ArgsTy.end(), {StrideVectTy});
        auto *TileLoadFunc = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                        Intrinsic::tensor_load, ArrayRef<Type *>(ArgsTy));
        std::vector<Value *> Args = {GEP};
        Args.insert(Args.end(), RegTileTensorTypeValVect.begin(), RegTileTensorTypeValVect.end());
        Args.insert(Args.end(), StridesVector);
        auto *TileLoad = CallInst::Create(TileLoadFunc, Args, 
                        Name +  "tile.load." + Twine(J / RegNumRows) 
                        + "." + Twine(I / RegNumCols) , InsertBefore);
        Result.push_back(TileLoad);

        // Add load instruction to the tensor type to value map
        LoadMap[J / RegNumRows].push_back(TileLoad);

        // Put the load in tensor info
        TI->addTensorInfoFor(TileLoad, RegTile);
      }
    }
    return Result;
  }

  Value *loadTensor(Value *Ptr,  Type *ElemTy, 
                  unsigned NumElements, Instruction *InsertBefore) {
     auto *VecTy = FixedVectorType::get(ElemTy, NumElements);
    if(dyn_cast<PointerType>(Ptr->getType())->getElementType() != VecTy) {
      unsigned AS = dyn_cast<PointerType>(Ptr->getType())->getAddressSpace();
      auto *VecPtrType = PointerType::get(
          FixedVectorType::get(ElemTy, NumElements), AS);
      Ptr = CastInst::CreatePointerCast(
          Ptr, VecPtrType, "vec.cast", InsertBefore);
    }
    return new LoadInst(VecTy, Ptr, 
                              "input.load", false, {}, InsertBefore);
  }


  void storeTensor(Value *Ptr,  Value *Tensor, Instruction *InsertBefore) {
    if(dyn_cast<PointerType>(Ptr->getType())->getElementType() != Tensor->getType()) {
      unsigned AS = dyn_cast<PointerType>(Ptr->getType())->getAddressSpace();
      auto *VecPtrType = PointerType::get(Tensor->getType(), AS);
      Ptr = CastInst::CreatePointerCast(
          Ptr, VecPtrType, "vec.cast", InsertBefore);
    }
    new StoreInst(Tensor, Ptr, false, {}, InsertBefore);
  }

  template <typename T>
  void storeTile(
      T &TensorOpInfo, Value *TensorPtr, Type *EltTy, MaybeAlign MAlign,
      bool IsVolatile, Instruction *InsertBefore) {

    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Offset = computeIndex(
        TensorOpInfo.getOutputTensor(), TensorOpInfo.getOutTensorIndices(), false,
        InsertBefore);

    auto *TileStart = GetElementPtrInst::Create(
        EltTy, TensorPtr, ArrayRef<Value *>(Offset), "tile.start",
        InsertBefore);

    auto *Stride = ConstantInt::get(
        Int32Ty, TensorOpInfo.getStride(TensorOpInfo.getOutputTensor()));
    for (unsigned I = 0; I < TensorOpInfo.getNumOutputTiles(); ++I) {
      auto *GEP = computeVectorAddr(
          TileStart, ConstantInt::get(Int32Ty, I), Stride,
          TensorOpInfo.getStride(TensorOpInfo.getOutputTile()), EltTy,
          InsertBefore);

      new StoreInst(
          TensorOpInfo.getOutputTileVector(I), GEP, IsVolatile,
          getAlignForIndex(I, Stride, EltTy, MAlign), InsertBefore);
    }
  }

  template <typename T>
  void store2DTile(
      T &TensorOpInfo, Value *TensorPtr, TensorType &RegTile, 
      Type *EltTy, MaybeAlign MAlign, bool IsVolatile, Instruction *InsertBefore) {
    
    auto &OutTensor = TensorOpInfo.getOutputTensor();
    auto &OutBlock = TensorOpInfo.getOutputTile();
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Offset = computeIndex(
        OutTensor, TensorOpInfo.getOutTensorIndices(), false,
        InsertBefore);
    auto *TileStart = GetElementPtrInst::Create(
        EltTy, TensorPtr, ArrayRef<Value *>(Offset), "tile.start", InsertBefore);
 
    // Determine the size of the  tile registers
    auto RegTileTensorTypeValVect = RegTile.getTensorPropertiesValueVector();
    auto RegTileTensorTypeTypeVect = RegTile.getTensorPropertiesTypeVector();
    auto *TensorStride = ConstantInt::get(Int32Ty, TensorOpInfo.getStride(OutTensor));
    auto *StridesVector = ConstantVector::get(
                  ArrayRef<Constant *>({ConstantInt::get(Int32Ty, 0), TensorStride}));
    unsigned BlockNumRows = TensorOpInfo.getNumRows(OutBlock);
    unsigned BlockNumCols = TensorOpInfo.getNumColumns(OutBlock); 
    unsigned RegNumRows = TensorOpInfo.getNumRows(RegTile);
    unsigned RegNumCols = TensorOpInfo.getNumColumns(RegTile);
    auto *StrideVectTy = FixedVectorType::get(
        Type::getInt32Ty(InsertBefore->getParent()->getContext()), 2);

    for (unsigned J = 0; J < BlockNumRows; J += RegNumRows) {
      for (unsigned I = 0; I < BlockNumCols; I += RegNumCols) {
        auto *GEP = computeTileAddr(
            TileStart, ConstantInt::get(Int32Ty, I), ConstantInt::get(Int32Ty, J), 
            TensorStride, TensorOpInfo.getNumElems(OutBlock), EltTy, InsertBefore);
        std::vector<Type *> ArgsTy = {GEP->getType()};
        ArgsTy.insert(ArgsTy.end(), {StrideVectTy});
        ArgsTy.insert(ArgsTy.end(), {Type::getTokenTy(InsertBefore->getParent()->getContext())});
        auto *TileStoreFunc = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                        Intrinsic::tensor_store, ArrayRef<Type *>(ArgsTy));
        errs() << "TILE STORE INTRINSIC: " << *TileStoreFunc << "\n";
        std::vector<Value *> Args = {GEP};
        Args.insert(Args.end(), StridesVector);
        Args.insert(Args.end(), 
              {TensorOpInfo.getOutput2DTile(J / RegNumRows , I / RegNumCols)});
        CallInst::Create(TileStoreFunc, Args, "", InsertBefore);
      }
    }
  }

  /// Set elements I..I+NumElts-1 to Block
  Value *insertVector(
      Value *Col, unsigned I, Value *Block, Instruction *InsertBefore) {
    // First, bring Block to the same size as Col
    unsigned BlockNumElts =
        dyn_cast<FixedVectorType>(Block->getType())->getNumElements();
    unsigned NumElts =
        dyn_cast<FixedVectorType>(Col->getType())->getNumElements();
    assert(NumElts >= BlockNumElts && "Too few elements for current block");
    Block = new ShuffleVectorInst(
        Block, PoisonValue::get(Block->getType()),
        createSequentialMask(0, BlockNumElts, NumElts - BlockNumElts), "",
        InsertBefore);

    // If Col is 7 long and I is 2 and BlockNumElts is 2 the mask is: 0, 1, 7,
    // 8, 4, 5, 6
    SmallVector<int, 16> Mask;
    unsigned i;
    for (i = 0; i < I; i++) {
      Mask.push_back(i);
    }

    unsigned VecNumElts =
        dyn_cast<FixedVectorType>(Col->getType())->getNumElements();
    for (; i < I + BlockNumElts; i++) {
      Mask.push_back(i - I + VecNumElts);
    }

    for (; i < VecNumElts; i++) {
      Mask.push_back(i);
    }

    return new ShuffleVectorInst(Col, Block, Mask, "tile.vect", InsertBefore);
    // return BinaryOperator::Create(Instruction::Add, Col, Final, "acc.vector",
    // InsertBefore);
  }

  Value *accumulateResult(Value *Acc, Value *V, Instruction *InsertBefore) {
    return BinaryOperator::Create(
        Instruction::Add, Acc, V, "acc.vector", InsertBefore);
  }

  void InsertCallToPrint(Value *V, Instruction *InsertBefore) {
    if(V->getType()->isVectorTy()) {
      auto *VecTy = FixedVectorType::get(
          Type::getInt32Ty(InsertBefore->getParent()->getContext()), 4);
      auto *Poison = PoisonValue::get(V->getType());
      auto *I = new ShuffleVectorInst(
          V, Poison, createSequentialMask(0, 4, 0), "to.print", InsertBefore);
      std::vector<Type *> ArgsTy = {VecTy};
      std::vector<Value *> Args = {I};
      auto *Func = InsertBefore->getModule()->getFunction("print");
      CallInst::Create(
          Func->getFunctionType(), Func, ArrayRef<Value *>(Args), "",
          InsertBefore);
    } else {
      std::vector<Type *> ArgsTy = {V->getType()};
      std::vector<Value *> Args = {V};
      auto *Func = InsertBefore->getModule()->getFunction("print2");
      CallInst::Create(
          Func->getFunctionType(), Func, ArrayRef<Value *>(Args), "",
          InsertBefore);
    }
  }

  void InsertCallToPrintIndex(Value *V, Instruction *InsertBefore) {
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    std::vector<Type *> ArgsTy = {Int32Ty};
    std::vector<Value *> Args = {V};
    auto *Func = InsertBefore->getModule()->getFunction("print_index");
    CallInst::Create(
        Func->getFunctionType(), Func, ArrayRef<Value *>(Args), "",
        InsertBefore);
  }

  template <typename T>
  Value *extractVector(
      T &TensorInfo, TensorType TensorTypeInfo,
      SmallVector<Value *, 16> &TensorVect, unsigned I, unsigned J,
      unsigned NumElts, Instruction *InsertBefore) const {
    Value *Vec =
        TensorInfo.isColumnMajor(TensorTypeInfo) ? TensorVect[J] : TensorVect[I];
    auto *Poison = PoisonValue::get(Vec->getType());
    return new ShuffleVectorInst(
        Vec, Poison,
        createSequentialMask(
            TensorInfo.isColumnMajor(TensorTypeInfo) ? I : J, NumElts, 0),
        "block", InsertBefore);
  }

  Value *broadcastValAcrossVector(
      unsigned NumElts, Value *V, Instruction *InsertBefore) {
    auto EC = ElementCount::getFixed(NumElts);
    assert(EC.isNonZero() && "Cannot splat to an empty vector!");

    // First insert it into a poison vector so we can shuffle it.
    auto *I32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Poison = PoisonValue::get(VectorType::get(V->getType(), EC));
    V = InsertElementInst::Create(
        Poison, V, ConstantInt::get(I32Ty, 0), "broadcast.insert",
        InsertBefore);

    // Shuffle the value across the desired number of elements.
    SmallVector<int, 16> Zeros;
    Zeros.resize(EC.getKnownMinValue());
    return new ShuffleVectorInst(V, Poison, Zeros, "broadcast", InsertBefore);
  }

  Value *createMulAdd(
      Value *Sum, Value *A, Value *B, bool UseFPOp, Instruction *InsertBefore) {
    if (!Sum) {
      if (UseFPOp) {
        return BinaryOperator::Create(
            Instruction::FMul, A, B, "", InsertBefore);
      }
      return BinaryOperator::Create(Instruction::Mul, A, B, "", InsertBefore);
    }

    if (UseFPOp) {
      auto *Mul =
          BinaryOperator::Create(Instruction::FMul, A, B, "", InsertBefore);
      return BinaryOperator::Create(
          Instruction::FAdd, Sum, Mul, "", InsertBefore);
    }

    auto *Mul =
        BinaryOperator::Create(Instruction::Mul, A, B, "", InsertBefore);
    return BinaryOperator::Create(Instruction::Add, Sum, Mul, "", InsertBefore);
  }

  Value *createReduceMacIntrinsic(
            Value *A, Value *B, unsigned BlockSize, Instruction *InsertBefore) {
    auto *MacIntrinsic = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                      Intrinsic::vector_reduce_mac, ArrayRef<Type*>({A->getType()}));
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    std::vector<Value *> Args = {A, B, ConstantInt::get(Int32Ty, BlockSize)};
    return CallInst::Create(MacIntrinsic, Args, "", InsertBefore);
  }

  Value *createReduceMacAccIntrinsic(
            Value* Acc, Value *A, Value *B, unsigned BlockSize, Instruction *InsertBefore) {
    auto *MacIntrinsic = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                      Intrinsic::vector_reduce_mac, ArrayRef<Type*>({A->getType()}));
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    std::vector<Value *> Args = {Acc, A, B, ConstantInt::get(Int32Ty, BlockSize)};
    return CallInst::Create(MacIntrinsic, Args, "", InsertBefore);
  }

  Value *
  reduceVector(Value *Vect, unsigned NumElems, Instruction *InsertBefore) {
    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    Value *Sum = ExtractElementInst::Create(
        Vect, ConstantInt::get(Int32Ty, 0), "", InsertBefore);
    for (unsigned I = 1; I < NumElems; I++) {
      auto *LH = ExtractElementInst::Create(
          Vect, ConstantInt::get(Int32Ty, I), "", InsertBefore);
      Sum = BinaryOperator::Create(
          Instruction::Add, Sum, LH, "reduce.add", InsertBefore);
    }
    return Sum;
  }

  Value *assembleVector(
      Type *ElemTy, SmallVector<Value *, 16> &ElemVect,
      Instruction *InsertBefore) {
    auto EC = ElementCount::getFixed(ElemVect.size());
    assert(EC.isNonZero() && "Cannot splat to an empty vector!");
    auto *I32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    Value *Vect = PoisonValue::get(VectorType::get(ElemTy, EC));
    for (unsigned I = 0; I < ElemVect.size(); I++) {
      Vect = InsertElementInst::Create(
          Vect, ElemVect[I], ConstantInt::get(I32Ty, I), "assmebled.vect",
          InsertBefore);
    }
    return Vect;
  }

  void splitVector(
      Value *Vect, SmallVector<Value *, 16> &Result, unsigned J,
      Instruction *InsertBefore) {
    auto *I32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    for (unsigned I = 0; I < Result.size(); I++) {
      Value *V = ExtractElementInst::Create(
          Vect, ConstantInt::get(I32Ty, I), "vect.split", InsertBefore);
      Result[I] = InsertElementInst::Create(
          Result[I], V, ConstantInt::get(I32Ty, J), "insert.elem",
          InsertBefore);
    }
  }

  void generateMatrixMultiply1DKernel(
      MatMulInfo &MMInfo, Type *EltType, Instruction *InsertBefore) {
    const unsigned VF = std::max<unsigned>(
        TTI.getRegisterBitWidth(true) /
            EltType->getPrimitiveSizeInBits().getFixedSize(), 1U);

    TensorType &LTileTensorType = MMInfo.LTile;
    TensorType &RTileTensorType = MMInfo.RTile;
    unsigned R = MMInfo.LBlockDim;
    unsigned C = MMInfo.RBlockDim;
    unsigned M = MMInfo.BlockCommonDim;

    SmallVector<Value *, 16> &A = MMInfo.LTileVector;
    SmallVector<Value *, 16> &B = MMInfo.RTileVector;
    SmallVector<Value *, 16> &TileResult = MMInfo.OutTiles;

    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    bool IsFP = EltType->isFloatingPointTy();

    if (MMInfo.isRowMajor(LTileTensorType) &&
        MMInfo.isColumnMajor(RTileTensorType)) {
      for (unsigned I = 0; I < R; I++) {
        bool isSumZero = isa<ConstantAggregateZero>(TileResult[I]);
        SmallVector<Value *, 16> ResultElemVect;
        for (unsigned J = 0; J < C; J++) {
          Value *Sum = nullptr;
          unsigned BlockSize = VF;
          for (unsigned K = 0; K < M; K += BlockSize) {
            // Gradually lower the vectorization factor to cover the remainder.
            while (K + BlockSize > M) {
              BlockSize /= 2;
            }

            auto *L = extractVector(
                MMInfo, LTileTensorType, A, I, K, BlockSize, InsertBefore);
            auto *R = extractVector(
                MMInfo, RTileTensorType, B, K, J, BlockSize, InsertBefore);
            auto *NewSum = createReduceMacIntrinsic(L, R, BlockSize, InsertBefore);
            if(Sum) {
              if(IsFP) {
                Sum = BinaryOperator::Create(
                        Instruction::FAdd, NewSum, Sum, "reduce.add", InsertBefore);
              } else {
                Sum = BinaryOperator::Create(
                        Instruction::Add, NewSum, Sum, "reduce.add", InsertBefore);
              }
            } else {
              Sum = NewSum;
            }
          }
          ResultElemVect.push_back(Sum);
        }

        auto *Vect = assembleVector(EltType, ResultElemVect, InsertBefore);
        TileResult[I] = accumulateResult(TileResult[I], Vect, InsertBefore);
      }
      return;
    }

    if (MMInfo.isColumnMajor(LTileTensorType) &&
        MMInfo.isRowMajor(RTileTensorType)) {
      unsigned BlockSize = VF;
      for (unsigned I = 0; I < R; I++) {
        bool isSumZero = isa<ConstantAggregateZero>(TileResult[I]);
        for (unsigned J = 0; J < C; J += BlockSize) {
          while (J + BlockSize > C) {
            BlockSize /= 2;
          }

          Value *Sum = nullptr;
          for (unsigned K = 0; K < M; ++K) {
            auto *R = extractVector(
                MMInfo, RTileTensorType, B, K, J, BlockSize, InsertBefore);
            auto *LH = ExtractElementInst::Create(
                A[K], ConstantInt::get(Int32Ty, I), "", InsertBefore);
            auto *Splat = createBroadCastIntrinsic(LH, BlockSize, InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, Splat, R, IsFP,
                InsertBefore);
          }
          auto *Vector = insertVector(TileResult[I], J, Sum, InsertBefore);
          TileResult[I] = accumulateResult(TileResult[I], Vector, InsertBefore);
        }
      }
      return;
    }

    if (MMInfo.isColumnMajor(LTileTensorType) &&
        MMInfo.isColumnMajor(RTileTensorType)) {
      unsigned BlockSize = VF;
      for (unsigned I = 0; I < R; I += BlockSize) {
        while (I + BlockSize > R) {
          BlockSize /= 2;
        }
        bool isSumZero = dyn_cast<ConstantAggregateZero>(TileResult[I]);

        // Fill a pseudo-tile with undefined
        SmallVector<Value *, 16> ResultVect;
        for (unsigned J = 0; J < R; J++) {
          ResultVect.push_back(UndefValue::get(TileResult[0]->getType()));
        }
        for (unsigned J = 0; J < C; J++) {
          Value *Sum = nullptr;
          for (unsigned K = 0; K < M; ++K) {
            auto *L = extractVector(
                MMInfo, LTileTensorType, A, I, K, BlockSize, InsertBefore);
            auto *RH = ExtractElementInst::Create(
                B[J], ConstantInt::get(Int32Ty, K), "", InsertBefore);
            auto *Splat = createBroadCastIntrinsic(RH, BlockSize, InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, L, Splat, IsFP,
                InsertBefore);
          }
          splitVector(Sum, ResultVect, J, InsertBefore);
        }

        for (unsigned J = 0; J < R; J++) {
          auto *Vector =
              insertVector(TileResult[J], I, ResultVect[J], InsertBefore);
          TileResult[J] = accumulateResult(TileResult[J], Vector, InsertBefore);
        }
      }
      return;
    }

    if (MMInfo.isRowMajor(LTileTensorType) &&
        MMInfo.isRowMajor(RTileTensorType)) {
      for (unsigned I = 0; I < R; ++I) {
        unsigned BlockSize = VF;
        bool isSumZero = isa<ConstantAggregateZero>(TileResult[I]);

        for (unsigned J = 0; J < C; J += BlockSize) {
          while (J + BlockSize > C) {
            BlockSize /= 2;
          }
          Value *Sum = nullptr;
          for (unsigned K = 0; K < M; ++K) {
            auto *R = extractVector(
                MMInfo, RTileTensorType, B, K, J, BlockSize, InsertBefore);
            auto *LH = ExtractElementInst::Create(
                A[I], ConstantInt::get(Int32Ty, K), "", InsertBefore);
            auto *Splat = createBroadCastIntrinsic(LH, BlockSize, InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, Splat, R, IsFP,
                InsertBefore);
          }
          auto *Vector = insertVector(TileResult[I], J, Sum, InsertBefore);
          TileResult[I] = accumulateResult(TileResult[I], Vector, InsertBefore);
        }
      }
      return;
    }
  }

  void generateMatrixMultiply2DKernel(
    MatMulInfo &MMInfo, Type *EltType, Instruction *InsertBefore) {
    
    unsigned NumLTilesRows = MMInfo.Num2DRegTileRows;
    unsigned NumRTilesCols = MMInfo.Num2DRegTileCols;
    unsigned NumTilesCommon = MMInfo.Num2DRegTileCommon;
    auto &Out2DTileReg = MMInfo.Out2DTileReg;
    auto &LTileMap = MMInfo.LTileMap;
    auto &RTileMap = MMInfo.RTileMap;
    auto &OutputTiles = MMInfo.Out2DTiles;
    auto OutRegPropertiesValVect = Out2DTileReg.getTensorPropertiesValueVector();
    auto OutRegPropertiesTypeVect = Out2DTileReg.getTensorPropertiesTypeVector();
    unsigned OutTileSize = MMInfo.getNumRows(Out2DTileReg) * MMInfo.getNumColumns(Out2DTileReg);
    auto *OutTileVecTy = FixedVectorType::get(EltType, OutTileSize);

    auto &Ctx = InsertBefore->getParent()->getContext();
    auto *TokenTy = Type::getTokenTy(Ctx);
    auto *MMAFunc = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                    Intrinsic::tensor_mma, ArrayRef<Type *>({OutTileVecTy}));
    errs() << "MMA INTRINSIC: " << *MMAFunc << "\n";
    std::vector<Type *> TyArgs = {OutTileVecTy};
    TyArgs.insert(TyArgs.end(), OutRegPropertiesTypeVect.begin(), OutRegPropertiesTypeVect.end());
    auto *TypeInfoFunc = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                    Intrinsic::tensor_typeinfo, ArrayRef<Type *>(TyArgs));
    errs() << "TYPEINFO INTRINSIC: " << *TypeInfoFunc << "\n";

    if (MMInfo.isRowMajor(MMInfo.L2DTileReg) &&
        MMInfo.isRowMajor(MMInfo.R2DTileReg)) {
      // Perform the MMA operation
      for (unsigned LJ = 0; LJ < NumLTilesRows; LJ++) {
        for (unsigned LI = 0; LI < NumTilesCommon; LI++) {
          for (unsigned RI = 0; RI < NumRTilesCols; RI++) {
            auto *LTileLoad = LTileMap[LJ][LI];
            auto *RTileLoad = RTileMap[LI][RI];
            auto *TileMMA = CallInst::Create(MMAFunc,
                            {OutputTiles[LJ][RI], LTileLoad, RTileLoad}, "tile.mma", InsertBefore);
            std::vector<Value *> Args = {TileMMA};
            Args.insert(Args.end(), OutRegPropertiesValVect.begin(), OutRegPropertiesValVect.end());
            auto *TypeInfoCall = CallInst::Create(TypeInfoFunc, Args, "tile.mma.typeinfo", InsertBefore);
            OutputTiles[LJ][RI] = TypeInfoCall;

            // Add tensor mma and typeinfo into the tensor info
            TI->addTensorInfoFor(TileMMA, Out2DTileReg);
            TI->addTensorInfoFor(TypeInfoCall, Out2DTileReg);
          }
        }
      }
    }
  }

  void generateMatrixMultiplyKernel(
      MatMulInfo &MMInfo, Type *EltType, Instruction *InsertBefore) {
    const unsigned VF = std::max<unsigned>(
        TTI.getRegisterBitWidth(true) /
            EltType->getPrimitiveSizeInBits().getFixedSize(), 1U);

    TensorType &LTileTensorType = MMInfo.LTile;
    TensorType &RTileTensorType = MMInfo.RTile;
    unsigned R = MMInfo.LBlockDim;
    unsigned C = MMInfo.RBlockDim;
    unsigned M = MMInfo.BlockCommonDim;

    SmallVector<Value *, 16> &A = MMInfo.LTileVector;
    SmallVector<Value *, 16> &B = MMInfo.RTileVector;
    SmallVector<Value *, 16> &TileResult = MMInfo.OutTiles;

    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    bool IsFP = EltType->isFloatingPointTy();

    if (MMInfo.isRowMajor(LTileTensorType) &&
        MMInfo.isColumnMajor(RTileTensorType)) {
      for (unsigned I = 0; I < R; I++) {
        // for (unsigned I = 0; I < R; I += BlockSize) {
        bool isSumZero = isa<ConstantAggregateZero>(TileResult[I]);
        SmallVector<Value *, 16> ResultElemVect;
        for (unsigned J = 0; J < C; J++) {
          Value *Sum = nullptr;
          unsigned BlockSize = VF;
          // for (unsigned K = 0; K < M; ++K) {
          for (unsigned K = 0; K < M; K += BlockSize) {
            // Gradually lower the vectorization factor to cover the remainder.
            while (K + BlockSize > M) {
              BlockSize /= 2;
            }

            auto *L = extractVector(
                MMInfo, LTileTensorType, A, I, K, BlockSize, InsertBefore);
            auto *R = extractVector(
                MMInfo, RTileTensorType, B, K, J, BlockSize, InsertBefore);
            // auto *LH = ExtractElementInst::Create(A[K],
            // ConstantInt::get(Int32Ty, I),
            //                                                   "",
            //                                                   InsertBefore);
            // auto *Splat = broadcastValAcrossVector(BlockSize, LH,
            // InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, L, R, IsFP, InsertBefore);
          }

          ResultElemVect.push_back(reduceVector(Sum, BlockSize, InsertBefore));
        }

        auto *Vect = assembleVector(EltType, ResultElemVect, InsertBefore);
        // auto *Vector = insertVector(TileResult[I], I, Vect, InsertBefore);
        TileResult[I] = accumulateResult(TileResult[I], Vect, InsertBefore);
      }
      return;
    }

    if (MMInfo.isColumnMajor(LTileTensorType) &&
        MMInfo.isRowMajor(RTileTensorType)) {
      unsigned BlockSize = VF;
      for (unsigned I = 0; I < R; I++) {

        // for (unsigned I = 0; I < R; I += BlockSize) {
        bool isSumZero = isa<ConstantAggregateZero>(TileResult[I]);

        for (unsigned J = 0; J < C; J += BlockSize) {
          while (J + BlockSize > C) {
            BlockSize /= 2;
          }

          Value *Sum = nullptr;
          for (unsigned K = 0; K < M; ++K) {
            auto *R = extractVector(
                MMInfo, RTileTensorType, B, K, J, BlockSize, InsertBefore);
            auto *LH = ExtractElementInst::Create(
                A[K], ConstantInt::get(Int32Ty, I), "", InsertBefore);
            auto *Splat = broadcastValAcrossVector(BlockSize, LH, InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, Splat, R, IsFP,
                InsertBefore);
          }
          // Result.setVector(I, insertVector(Result.getVector(I), J, Sum,
          // InsertBefore));
          auto *Vector = insertVector(TileResult[I], J, Sum, InsertBefore);
          TileResult[I] = accumulateResult(TileResult[I], Vector, InsertBefore);
        }
      }
      return;
    }

    if (MMInfo.isColumnMajor(LTileTensorType) &&
        MMInfo.isColumnMajor(RTileTensorType)) {
      unsigned BlockSize = VF;
      for (unsigned I = 0; I < R; I += BlockSize) {
        while (I + BlockSize > R) {
          BlockSize /= 2;
        }
        bool isSumZero = dyn_cast<ConstantAggregateZero>(TileResult[I]);

        // Fill a pseudo-tile with undefined
        SmallVector<Value *, 16> ResultVect;
        for (unsigned J = 0; J < R; J++) {
          ResultVect.push_back(UndefValue::get(TileResult[0]->getType()));
        }
        for (unsigned J = 0; J < C; J++) {
          Value *Sum = nullptr;
          for (unsigned K = 0; K < M; ++K) {
            // auto *L = extractVector(MMInfo, LTileTensorType, A, I, K,
            // BlockSize, InsertBefore);
            auto *L = extractVector(
                MMInfo, LTileTensorType, A, I, K, BlockSize, InsertBefore);
            auto *RH = ExtractElementInst::Create(
                B[J], ConstantInt::get(Int32Ty, K), "", InsertBefore);
            auto *Splat = broadcastValAcrossVector(BlockSize, RH, InsertBefore);
            // InsertCallToPrint(Splat, InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, L, Splat, IsFP,
                InsertBefore);
          }

          splitVector(Sum, ResultVect, J, InsertBefore);
          // Result.setVector(J, insertVector(Result.getVector(J), I, Sum,
          // InsertBefore)); TileResult[J] = insertVector(TileResult[J], I, Sum,
          // InsertBefore); TileResult[J] = insertVector(TileResult[J], I, Sum,
          // InsertBefore);
        }

        for (unsigned J = 0; J < R; J++) {
          auto *Vector =
              insertVector(TileResult[J], I, ResultVect[J], InsertBefore);
          TileResult[J] = accumulateResult(TileResult[J], Vector, InsertBefore);
        }
      }
      return;
    }

    if (MMInfo.isRowMajor(LTileTensorType) &&
        MMInfo.isRowMajor(RTileTensorType)) {
      for (unsigned I = 0; I < R; ++I) {
        unsigned BlockSize = VF;
        bool isSumZero = isa<ConstantAggregateZero>(TileResult[I]);

        for (unsigned J = 0; J < C; J += BlockSize) {
          while (J + BlockSize > C) {
            BlockSize /= 2;
          }
          Value *Sum = nullptr;
          for (unsigned K = 0; K < M; ++K) {
            auto *R = extractVector(
                MMInfo, RTileTensorType, B, K, J, BlockSize, InsertBefore);
            auto *LH = ExtractElementInst::Create(
                A[I], ConstantInt::get(Int32Ty, K), "", InsertBefore);
            auto *Splat = broadcastValAcrossVector(BlockSize, LH, InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, Splat, R, IsFP,
                InsertBefore);
          }

          // Result.setVector(I, insertVector(Result.getVector(I), J, Sum,
          // InsertBefore));
          auto *Vector = insertVector(TileResult[I], J, Sum, InsertBefore);
          TileResult[I] = accumulateResult(TileResult[I], Vector, InsertBefore);
        }
      }
      return;
    }
  }

  void forceUnrollOfLoop(Loop *L, unsigned InnerLoopUnrollFactor) {
    // Force unrolling of the given loop
    if (InnerLoopUnrollFactor) {
      addStringMetadataToLoop(
          L, "llvm.loop.unroll.count", InnerLoopUnrollFactor);
    }
  }

  Value *lowerMatMul(
      Value *LTensor, Value *RTensor, CallInst *MatMul, unsigned TileSize_M,
      unsigned TileSize_N, unsigned Tile_K, unsigned InnerLoopUnrollFactor) {
    LLVM_DEBUG(dbgs() << "LOWERING MATMUL\n");
    LLVM_DEBUG(dbgs() << TileSize_M << " " << TileSize_N << " " << Tile_K << " "
                      << InnerLoopUnrollFactor << "\n");
    // auto *LTensor = TI->getTensorOperand(MatMul, 0);
    // auto *RTensor = TI->getTensorOperand(MatMul, 1);
    TensorType &LTensorType = TI->getTensorTypeInfoFor(LTensor);
    TensorType &RTensorType = TI->getTensorTypeInfoFor(RTensor);
    auto *EltType = dyn_cast<VectorType>(MatMul->getType())->getElementType();
    auto &Ctx = MatMul->getParent()->getContext();

    // Register the matmul information
    auto MMInfo = MatMulInfo(
        Ctx, LTensorType, RTensorType, TI->getLayoutVectorFor(MatMul));

    // Create Loop nest and set up the tiles information
    MMInfo.createLoopNest(*this, TileSize_M, TileSize_N, TileSize_K, MatMul);

    LLVM_DEBUG(dbgs() << "CREATING LOOP NEST: \n");
    LLVM_DEBUG(dbgs() << *(MatMul->getParent()->getParent()) << "\n");

     // Inner loop body terminator
    auto *InnerBodyTerminator = (MMInfo.getInnerLoopBody())->getTerminator();

    if(LowerToTileIntrinsics) {
      // Insert PHIs that represent the tiles
      MMInfo.insert2DTilePHIs(EltType, *TI);

      errs() << "GENERATED PHIS: " << *(MatMul->getParent()->getParent()) << "\n";

      LLVM_DEBUG(dbgs() << "INSERTING PHIS: \n");
      LLVM_DEBUG(dbgs() << *(MatMul->getParent()->getParent()) << "\n");

      // Load tiles of the operands.
      MMInfo.LTileVector = load2DTile<MatMulInfo>(
          MMInfo, TI->getMemPtrFor(LTensor), LTensorType, MMInfo.LTile, MMInfo.L2DTileReg, 
          EltType, MMInfo.LTensorIndices, MMInfo.LTileMap, {}, false, "L",
          InnerBodyTerminator);
      MMInfo.RTileVector = load2DTile<MatMulInfo>(
          MMInfo, TI->getMemPtrFor(RTensor), RTensorType, MMInfo.RTile, MMInfo.R2DTileReg, 
          EltType, MMInfo.RTensorIndices, MMInfo.RTileMap, {}, false, "R",
          InnerBodyTerminator);

      LLVM_DEBUG(dbgs() << "INSERTING LOADS: \n");
      LLVM_DEBUG(dbgs() << *(MatMul->getParent()->getParent()) << "\n");
      errs() << "INSERTING LOADS: \n";
      errs() << *(MatMul->getParent()->getParent()) << "\n";

      generateMatrixMultiply2DKernel(MMInfo, EltType, InnerBodyTerminator);

      errs() << "GENERATED MATMUL KERNEL: \n" << *(InnerBodyTerminator->getParent()->getParent()) << "\n";

      // Store tiles of outputs.
      store2DTile<MatMulInfo>(
          MMInfo, TI->getMemPtrFor(MatMul), MMInfo.Out2DTileReg, EltType, {}, false,
          (MMInfo.getBlockToStoreTile())->getTerminator());

      errs() << "GENERATED MATMUL STORES: \n" << *(InnerBodyTerminator->getParent()->getParent()) << "\n";

      // Finish completeling the PHIs for tiles
      MMInfo.complete2DTilePHIs();

      errs() << "GENERATED PHIs: \n" << *(InnerBodyTerminator->getParent()->getParent()) << "\n";
    } else {
      // Insert PHIs that represent the tiles
      MMInfo.insertTilePHIs(EltType);

      // Load tiles of the operands.
      MMInfo.LTileVector = loadTile<MatMulInfo>(
          MMInfo, TI->getMemPtrFor(LTensor), LTensorType, MMInfo.LTile,
          EltType, MMInfo.LTensorIndices, {}, false,
          InnerBodyTerminator);
      MMInfo.RTileVector = loadTile<MatMulInfo>(
          MMInfo, TI->getMemPtrFor(RTensor), RTensorType, MMInfo.RTile,
          EltType, MMInfo.RTensorIndices, {}, false,
          InnerBodyTerminator);

      // Generate the matmul kernel
      if(LowerToVectorIntrinsics)
        generateMatrixMultiply1DKernel(MMInfo, EltType, InnerBodyTerminator);
      else
        generateMatrixMultiplyKernel(MMInfo, EltType, InnerBodyTerminator);

      LLVM_DEBUG(dbgs() << "GENERATING MATMUL: \n");
      LLVM_DEBUG(dbgs() << *(MatMul->getParent()->getParent()) << "\n");

      // Store tiles of outputs.
      storeTile<MatMulInfo>(
          MMInfo, TI->getMemPtrFor(MatMul), EltType, {}, false,
          (MMInfo.getBlockToStoreTile())->getTerminator());

      LLVM_DEBUG(dbgs() << "INSERTING STORES: \n");
      LLVM_DEBUG(dbgs() << *(MatMul->getParent()->getParent()) << "\n");

      // Finish completeling the PHIs for tiles
      MMInfo.completeTilePHIs();
    }

    // Force unrolling of innermost loop
    forceUnrollOfLoop(
        LI->getLoopFor(MMInfo.getInnerLoopBody()), InnerLoopUnrollFactor);
    
    // Load the tensor now
    auto *VecTy = FixedVectorType::get(EltType, TI->getTensorAllocSize(MatMul));
    auto *MallocPtr = TI->getMemPtrFor(MatMul);
    unsigned AS =
        dyn_cast<PointerType>(MallocPtr->getType())->getAddressSpace();
    auto *CastMallocPtr = CastInst::CreatePointerCast(
        MallocPtr, PointerType::get(VecTy, AS), "malloc.cast", MatMul);
    auto *Output =
        new LoadInst(VecTy, CastMallocPtr, "final.load", false, {}, MatMul);
    return Output;
  }

  Value *insertIntrinsicOperation(Intrinsic::ID ID, Value *Operand, Type *Ty, 
                                  const StringRef &Name, Instruction *InsertBefore) {
    auto *Declaration = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                                ID, ArrayRef<Type*>({Ty}));

    return CallInst::Create(Declaration->getFunctionType(), Declaration, 
                                ArrayRef<Value *>(Operand), Name, InsertBefore);
}

Value *generateElementWiseScalarKernel(Intrinsic::ID ID,
                                ElementWiseInfo &EwInfo, Value *Input, 
                                Type *ElemTy, const StringRef &OpName, 
                                Instruction *InsertBefore) {
    auto &Ctx = InsertBefore->getParent()->getContext();
    
    // Get the index into the tensor
    auto *Offset = computeIndex(EwInfo.Tensor, EwInfo.TensorIndices, 
                                EwInfo.getNumLoopsCollapsed(), InsertBefore);
    Value *UpdatedTensor = EwInfo.PHITensor;
    for(unsigned I = 0; I < EwInfo.TileSize; I++) {
        if(I != 0) {
            // Increment the offset into the tensor
            auto *Inc = ConstantInt::get(Type::getInt32Ty(Ctx), I);
            Offset = BinaryOperator::Create(Instruction::Add, Offset, Inc, "", InsertBefore);
        }

        // Extract the element
        auto *Elem = ExtractElementInst::Create(Input, Offset, "extract.elem", InsertBefore);
        auto *CastElem = ConvertToFloat(Elem, InsertBefore);

        // Compute the sine
        auto *Op = insertIntrinsicOperation(ID, CastElem, 
                                    CastElem->getType(), OpName, InsertBefore);

        // Insert the new element
        UpdatedTensor = InsertElementInst::Create(UpdatedTensor, Op, Offset, 
                                                    "insert.elem", InsertBefore);
    }
    return UpdatedTensor;
  }

  Value *lowerElementWiseTensorOp(CallInst *Op, Intrinsic::ID ID, unsigned TileSize, 
                                  const StringRef &OpName) {
    // Get the input tensor
    auto *Input = TI->getTensorOperand(Op, 0);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    auto *ElemTy = dyn_cast<VectorType>(Op->getType())->getElementType();

    // Load the tensor
    auto *Ptr = TI->getMemPtrFor(Op);
    Input = loadTensor(Ptr, ElemTy, 
                    TI->getTensorAllocSize(Input), Op);

    // Set up the information fro the elementwise operations.
    auto EwInfo = ElementWiseInfo(InputTensor);

    // Create the loop nest information
    EwInfo.createLoopNest(*this, TileSize, Op);

    // Insert the tensor PHI
    EwInfo.insertTensorPHI(Input, ElemTy);

    auto *InnerBodyTerminator = EwInfo.getInnerLoopBody()->getTerminator();
    auto *Output = generateElementWiseScalarKernel(ID, EwInfo, Input, 
                                            ElemTy, OpName, InnerBodyTerminator);

    // Complete the phi ndoe representing the tensor
    EwInfo.completeTensorPHI(Output);

    // Store the tensor back in memory
    storeTensor(TI->getMemPtrFor(Op), Output, Op);

    return Output;
  }

  Value *generateScalarReluKernel(
      ElementWiseInfo &EwInfo, Value *Input, Type *ElemTy,
      Instruction *InsertBefore) {
    auto &Ctx = InsertBefore->getParent()->getContext();
    auto *Zero = GetConstantValue(Ctx, ElemTy, 0);

    // Get the index into the tensor
    auto *Offset = computeIndex(
        EwInfo.Tensor, EwInfo.TensorIndices, EwInfo.getNumLoopsCollapsed(),
        InsertBefore);
    Value *UpdatedTensor = EwInfo.PHITensor;
    for (unsigned I = 0; I < EwInfo.TileSize; I++) {
      if (I != 0) {
        // Increment the offset into the tensor
        auto *Inc = ConstantInt::get(Type::getInt32Ty(Ctx), I);
        Offset = BinaryOperator::Create(
            Instruction::Add, Offset, Inc, "", InsertBefore);
      }

      // Extract the element
      auto *Elem = ExtractElementInst::Create(
          Input, Offset, "extract.elem", InsertBefore);

      // See if the extracted element is negative
      Value *Cond;
      switch (ElemTy->getTypeID()) {
      case Type::IntegerTyID:
        Cond = CmpInst::Create(
            Instruction::ICmp, ICmpInst::ICMP_SGE, Elem, Zero, "relu.cond",
            InsertBefore);
        break;
      case Type::FloatTyID:
      case Type::DoubleTyID:
        Cond = CmpInst::Create(
            Instruction::FCmp, ICmpInst::FCMP_UGE, Elem, Zero, "relu.cond",
            InsertBefore);
        break;
      case Type::HalfTyID:
      case Type::BFloatTyID:
      default:
        assert(false && "Invalid element type.");
      }

      // Use select to use choose a value to insert
      auto *NewElem =
          SelectInst::Create(Cond, Elem, Zero, "new.elem", InsertBefore);

      // Insert the new element
      UpdatedTensor = InsertElementInst::Create(
          UpdatedTensor, NewElem, Offset, "insert.elem", InsertBefore);
    }
    return UpdatedTensor;
  }

  Value *lowerRelu(CallInst *Relu, unsigned TileSize) {
    // Get the input tensor
    auto *Input = TI->getTensorOperand(Relu, 0);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    auto *ElemTy = dyn_cast<VectorType>(Relu->getType())->getElementType();

    // Load the tensor
    auto *Ptr = TI->getMemPtrFor(Relu);
    Input = loadTensor(Ptr, ElemTy, 
                    TI->getTensorAllocSize(Input), Relu);

    auto EwInfo = ElementWiseInfo(InputTensor);

    // Create the loop nest information
    EwInfo.createLoopNest(*this, TileSize, Relu);

    // Insert the tensor PHI
    EwInfo.insertTensorPHI(Input, ElemTy);

    auto *InnerBodyTerminator = EwInfo.getInnerLoopBody()->getTerminator();
    auto *Output =
        generateScalarReluKernel(EwInfo, Input, ElemTy, InnerBodyTerminator);

    // Complete the phi node representing the tensor
    EwInfo.completeTensorPHI(Output);

    // Store the tensor back in memory
    storeTensor(TI->getMemPtrFor(Relu), Output, Relu);

    return Output;
  }

  Value *generateScalarTanhKernel(
      ElementWiseInfo &EwInfo, Value *Input, Type *ElemTy,
      Instruction *InsertBefore) {
    auto &Ctx = InsertBefore->getParent()->getContext();

    // Get the index into the tensor
    auto *Offset = computeIndex(
        EwInfo.Tensor, EwInfo.TensorIndices, EwInfo.getNumLoopsCollapsed(),
        InsertBefore);
    Value *UpdatedTensor = EwInfo.PHITensor;
    for (unsigned I = 0; I < EwInfo.TileSize; I++) {
      if (I != 0) {
        // Increment the offset into the tensor
        auto *Inc = ConstantInt::get(Type::getInt32Ty(Ctx), I);
        Offset = BinaryOperator::Create(
            Instruction::Add, Offset, Inc, "", InsertBefore);
      }

      // Extract the element
      auto *Elem = ExtractElementInst::Create(
          Input, Offset, "extract.elem", InsertBefore);
      auto *CastElem = ConvertToFloat(Elem, InsertBefore);

      // Compute the exponent
      auto *Two = GetConstantValue(Ctx, CastElem->getType(), 2);
      auto *Exponent = BinaryOperator::Create(
          Instruction::FMul, Two, Elem, "exponent", InsertBefore);
      auto *Exp = insertIntrinsicOperation(Intrinsic::exp, Exponent, 
                        Exponent->getType(), "exp", InsertBefore);

      // Compute Tanh
      auto *One = GetConstantValue(Ctx, Exp->getType(), 1);
      auto *Diff =
          BinaryOperator::Create(Instruction::FSub, Exp, One, "", InsertBefore);
      auto *Sum =
          BinaryOperator::Create(Instruction::FAdd, Exp, One, "", InsertBefore);
      auto *Tanh = BinaryOperator::Create(
          Instruction::FDiv, Diff, Sum, "tanh", InsertBefore);

      // Insert the new element
      UpdatedTensor = InsertElementInst::Create(
          UpdatedTensor, Tanh, Offset, "insert.elem", InsertBefore);
    }
    return UpdatedTensor;
  }

  Value *lowerTanh(CallInst *Tanh, unsigned TileSize) {
    // Get the input tensor
    auto *Input = TI->getTensorOperand(Tanh, 0);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    auto *ElemTy = dyn_cast<VectorType>(Tanh->getType())->getElementType();

    // Load the tensor
    auto *Ptr = TI->getMemPtrFor(Tanh);
    Input = loadTensor(Ptr, ElemTy, 
                    TI->getTensorAllocSize(Input), Tanh);

    // Set up the information fro the elementwise operations.
    auto EwInfo = ElementWiseInfo(InputTensor);

    // Create the loop nest information
    EwInfo.createLoopNest(*this, TileSize, Tanh);

    // Insert the tensor PHI
    EwInfo.insertTensorPHI(Input, ElemTy);

    auto *InnerBodyTerminator = EwInfo.getInnerLoopBody()->getTerminator();

    auto *Output =
        generateScalarTanhKernel(EwInfo, Input, ElemTy, InnerBodyTerminator);

    // Complete the phi ndoe representing the tensor
    EwInfo.completeTensorPHI(Output);

    // Store the tensor back in memory
    storeTensor(TI->getMemPtrFor(Tanh), Output, Tanh);

    return Output;
  }

  Value *generateScalarSigmoidKernel(
      ElementWiseInfo &EwInfo, Value *Input, Type *ElemTy,
      Instruction *InsertBefore) {
    auto &Ctx = InsertBefore->getParent()->getContext();

    // Get the index into the tensor
    auto *Offset = computeIndex(
        EwInfo.Tensor, EwInfo.TensorIndices, EwInfo.getNumLoopsCollapsed(),
        InsertBefore);
    Value *UpdatedTensor = EwInfo.PHITensor;
    for (unsigned I = 0; I < EwInfo.TileSize; I++) {
      if (I != 0) {
        // Increment the offset into the tensor
        auto *Inc = ConstantInt::get(Type::getInt32Ty(Ctx), I);
        Offset = BinaryOperator::Create(
            Instruction::Add, Offset, Inc, "", InsertBefore);
      }

      // Extract the element
      auto *Elem = ExtractElementInst::Create(
          Input, Offset, "extract.elem", InsertBefore);
      auto *Exponent = ConvertToFloat(Elem, InsertBefore);

      // Compute the exponent
      auto *Exp = insertIntrinsicOperation(Intrinsic::exp, Exponent, 
                            Exponent->getType(), "exp", InsertBefore);

      // Compute Tanh
      auto *One = GetConstantValue(Ctx, Exp->getType(), 1);
      auto *Sum =
          BinaryOperator::Create(Instruction::FAdd, Exp, One, "", InsertBefore);
      auto *Tanh = BinaryOperator::Create(
          Instruction::FDiv, Exp, Sum, "sigmoid", InsertBefore);

      // Insert the new element
      UpdatedTensor = InsertElementInst::Create(
          UpdatedTensor, Tanh, Offset, "insert.elem", InsertBefore);
    }
    return UpdatedTensor;
  }

  Value *lowerSigmoid(CallInst *Sigmoid, unsigned TileSize) {
    // Get the input tensor
    auto *Input = TI->getTensorOperand(Sigmoid, 0);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    auto *ElemTy = dyn_cast<VectorType>(Sigmoid->getType())->getElementType();

    // Load the tensor
    auto *Ptr = TI->getMemPtrFor(Sigmoid);
    Input = loadTensor(Ptr, ElemTy, 
                    TI->getTensorAllocSize(Input), Sigmoid);

    // Set up the information fro the elementwise operations.
    auto EwInfo = ElementWiseInfo(InputTensor);

    // Create the loop nest information
    EwInfo.createLoopNest(*this, TileSize, Sigmoid);

    // Insert the tensor PHI
    EwInfo.insertTensorPHI(Input, ElemTy);

    auto *InnerBodyTerminator = EwInfo.getInnerLoopBody()->getTerminator();

    auto *Output =
        generateScalarSigmoidKernel(EwInfo, Input, ElemTy, InnerBodyTerminator);

    // Complete the phi ndoe representing the tensor
    EwInfo.completeTensorPHI(Output);

    // Store the tensor back in memory
    storeTensor(TI->getMemPtrFor(Sigmoid), Output, Sigmoid);

    return Output;
  }

  Value *generateBroadcastKernel(
      Value *BroadcastVal, unsigned NumElems, Instruction *InsertBefore) {
    // If the given value to be broadcast is a constant value, just
    // generate a constant vector.
    if (auto *C = dyn_cast<Constant>(BroadcastVal)) {
      std::vector<Constant *> ConstTensorVec;
      for (unsigned I = 0; I < NumElems; I++) {
        ConstTensorVec.push_back(C);
      }
      return ConstantVector::get(ArrayRef<Constant *>(ConstTensorVec));
    }

    // Just generate vector instructions
    return broadcastValAcrossVector(NumElems, BroadcastVal, InsertBefore);
  }

  Value *createBroadCastIntrinsic(Value *Input,
      Value *BroadcastVal, unsigned NumElems, Instruction *InsertBefore) {
    // Generate the vector splat intrinsic
    auto *RetTy = FixedVectorType::get(BroadcastVal->getType(), NumElems);
    auto *SplatIntrinsic = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                                  Intrinsic::vector_splat, ArrayRef<Type*>({RetTy}));
    std::vector<Value *> Args {Input, BroadcastVal};
    return CallInst::Create(SplatIntrinsic, Args, "", InsertBefore);
  }

  Value *createBroadCastIntrinsic(
          Value *BroadcastVal, unsigned NumElems, Instruction *InsertBefore) {
    // Since no input is given, create a poison vector
    auto *Input = PoisonValue::get(FixedVectorType::get(BroadcastVal->getType(), NumElems));
    return createBroadCastIntrinsic(Input, BroadcastVal, NumElems, InsertBefore);
  }

  Value *generateBroadcastKernel(Value *Input,
      Value *BroadcastVal, unsigned NumElems, Instruction *InsertBefore) {
    // If the given value to be broadcast is a constant value, just
    // generate a constant vector.
    if (auto *C = dyn_cast<Constant>(BroadcastVal)) {
      std::vector<Constant *> ConstTensorVec;
      for (unsigned I = 0; I < NumElems; I++) {
        ConstTensorVec.push_back(C);
      }
      return ConstantVector::get(ArrayRef<Constant *>(ConstTensorVec));
    }

    // Just generate splat intrinsic
    return createBroadCastIntrinsic(Input, BroadcastVal, NumElems, InsertBefore);
  }

  Value *lowerBroadcast(CallInst *Broadcast) {
    auto *Input = TI->getTensorOperand(Broadcast, 0);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    unsigned NumElems = InputTensor.getTensorSize();
    auto *BroadcastVal = Broadcast->getArgOperand(1);
    if(LowerToVectorIntrinsics) {
      // Load the tensor
      auto *ElemTy = dyn_cast<VectorType>(Broadcast->getType())->getElementType();
      auto *Ptr = TI->getMemPtrFor(Broadcast);
      Input = loadTensor(Ptr, ElemTy, 
                      TI->getTensorAllocSize(Input), Broadcast);
      auto *Output = generateBroadcastKernel(Input, BroadcastVal, NumElems, Broadcast);

      // Store the tensor back in memory
      storeTensor(TI->getMemPtrFor(Broadcast), Output, Broadcast);

      return Output;
    }
    auto *Output = generateBroadcastKernel(BroadcastVal, NumElems, Broadcast);

     // Store the tensor back in memory
    storeTensor(TI->getMemPtrFor(Broadcast), Output, Broadcast);

    return Output;
  }

  /// TODO: make transpose more general
  void generateTransposeKernel(
      TensorTransformInfo &TTInfo, Type *EltType, Instruction *InsertBefore) {
    auto *I32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    unsigned R = TTInfo.InputRowTileDim;
    unsigned C = TTInfo.InputColTileDim;

    for (unsigned I = 0; I < R; ++I) {
      for (unsigned J = 0; J < C; ++J) {
        auto *V = ExtractElementInst::Create(
            TTInfo.InTileVector[I], ConstantInt::get(I32Ty, J),
            "transpose.extract", InsertBefore);
        TTInfo.OutTiles[J] = InsertElementInst::Create(
            TTInfo.OutTiles[J], V, ConstantInt::get(I32Ty, I),
            "transpose.insert", InsertBefore);
      }
    }
  }

  Value *lowerSin(CallInst *Sin, unsigned TileSize) {
    return lowerElementWiseTensorOp(Sin, Intrinsic::sin, TileSize, "sin");
  }

  Value *lowerCos(CallInst *Cos, unsigned TileSize) {
    return lowerElementWiseTensorOp(Cos, Intrinsic::cos, TileSize, "cos");
  }

  Value *lowerFloor(CallInst *Floor, unsigned TileSize) {
    return lowerElementWiseTensorOp(Floor, Intrinsic::floor, TileSize, "floor");
  }

  Value *lowerCeil(CallInst *Ceil, unsigned TileSize) {
    return lowerElementWiseTensorOp(Ceil, Intrinsic::ceil, TileSize, "ceil");
  }

  Value *lowerSqrt(CallInst *Sqrt, unsigned TileSize) {
    return lowerElementWiseTensorOp(Sqrt, Intrinsic::sqrt, TileSize, "sqrt");
  }

  Value *lowerExp(CallInst *Exp, unsigned TileSize) {
    return lowerElementWiseTensorOp(Exp, Intrinsic::exp, TileSize, "exp");
  }

  Value *lowerExp2(CallInst *Exp2, unsigned TileSize) {
    return lowerElementWiseTensorOp(Exp2, Intrinsic::exp2, TileSize, "exp2");
  }

  Value *lowerLog(CallInst *Log, unsigned TileSize) {
    return lowerElementWiseTensorOp(Log, Intrinsic::log, TileSize, "log");
  }

  Value *lowerLog2(CallInst *Log2, unsigned TileSize) {
    return lowerElementWiseTensorOp(Log2, Intrinsic::log2, TileSize, "log2");
  }

  Value *lowerLog10(CallInst *Log10, unsigned TileSize) {
    return lowerElementWiseTensorOp(Log10, Intrinsic::log10, TileSize, "log10");
  }

  Value *lowerFabs(CallInst *Fabs, unsigned TileSize) {
    return lowerElementWiseTensorOp(Fabs, Intrinsic::fabs, TileSize, "fabs");
  }

  Value *lowerTranspose(
      CallInst *Transpose, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
    // Get the input and output tensors
    auto *Input = TI->getTensorOperand(Transpose, 0);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    TensorType &OutputTensor = TI->getTensorTypeInfoFor(Transpose);
    auto *EltType =
        dyn_cast<VectorType>(Transpose->getType())->getElementType();

    // Set up the tensor transform interface
    auto TTInfo = TensorTransformInfo(InputTensor, OutputTensor);

    // Create the loop nest information
    TTInfo.createLoopNest(*this, TileSize_M, TileSize_N, Transpose);

    // Insert PHIs that represent the tiles
    TTInfo.initOutputTiles(EltType);

    // Inner loop body terminator
    auto *InnerBodyTerminator = TTInfo.getInnerLoopBody()->getTerminator();

    // Load tiles of the operands.
    TTInfo.InTileVector = loadTile<TensorTransformInfo>(
        TTInfo, TI->getMemPtrFor(Input), InputTensor, TTInfo.InTile, EltType,
        TTInfo.InTensorIndices, {}, false, InnerBodyTerminator);

    // Generate kernel for transpose
    generateTransposeKernel(TTInfo, EltType, InnerBodyTerminator);

    // Store tiles of outputs.
    storeTile<TensorTransformInfo>(
        TTInfo, TI->getMemPtrFor(Transpose), EltType, {}, false,
        InnerBodyTerminator);

    // Force unrolling of innermost loop
    forceUnrollOfLoop(
        LI->getLoopFor(TTInfo.getInnerLoopBody()), InnerLoopUnrollFactor);

    // Load the tensor now
    auto *VecTy =
        FixedVectorType::get(EltType, TI->getTensorAllocSize(Transpose));
    auto *MallocPtr = TI->getMemPtrFor(Transpose);
    unsigned AS =
        dyn_cast<PointerType>(MallocPtr->getType())->getAddressSpace();
    auto *CastMallocPtr = CastInst::CreatePointerCast(
        MallocPtr, PointerType::get(VecTy, AS), "malloc.cast", Transpose);
    auto *Output =
        new LoadInst(VecTy, CastMallocPtr, "final.load", false, {}, Transpose);

    return Output;
  }

  Value *createReductionAccumulate(Intrinsic::ID VectorID, Intrinsic::ID ScalarID, 
      Value *Acc, Value *Input, Instruction *InsertBefore) {
    Value *ReducedOut = Input;
    if(Input->getType()->isVectorTy()) {
      auto *Declaration = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                                  VectorID, ArrayRef<Type*>({Input->getType()}));
      ReducedOut = CallInst::Create(Declaration->getFunctionType(), Declaration, 
                    ArrayRef<Value *>(Input), "reduce.vector", InsertBefore);
    }
    if (!Acc)
      return ReducedOut;

    auto *Declaration = Intrinsic::getDeclaration(InsertBefore->getModule(), ScalarID, 
              ArrayRef<Type*>({ReducedOut->getType()}));
    return CallInst::Create(Declaration->getFunctionType(), Declaration, 
                    ArrayRef<Value *>({Acc, ReducedOut}), "reduce.scalar", InsertBefore);
  }

  Value *createReductionAccumulate(Intrinsic::ID VectorID, Instruction::BinaryOps ScalarOpcode,
      Value *Acc, Value *Input, Instruction *InsertBefore) {
    Value *ReducedOut = Input;
    if(Input->getType()->isVectorTy()) {
      auto *Declaration = Intrinsic::getDeclaration(InsertBefore->getModule(), 
                                  VectorID, ArrayRef<Type*>({Input->getType()}));
      ReducedOut = CallInst::Create(Declaration->getFunctionType(), Declaration, 
                    ArrayRef<Value *>(Input), "reduce.vector", InsertBefore);
    }
    if (!Acc)
      return ReducedOut;

    return BinaryOperator::Create(ScalarOpcode, Acc, ReducedOut, 
                                  "reduce.scalar", InsertBefore);
  }

  void generateReductionKernel(
      ReductionInfo &ReduceInfo, Intrinsic::ID VectorID, Intrinsic::ID ScalarID, 
      Type *EltType, Instruction *InsertBefore) {
    const unsigned VF = std::max<unsigned>(
        TTI.getRegisterBitWidth(true) /
            EltType->getPrimitiveSizeInBits().getFixedSize(), 1U);

    TensorType &InTileTensorType = ReduceInfo.InTile;
    unsigned NumBlockRows = ReduceInfo.NumBlockRows;
    unsigned NumBlockCols = ReduceInfo.NumBlockCols;
    SmallVector<Value *, 16> &InTileVector = ReduceInfo.InTileVector;

    Value *Acc = nullptr;
    for (unsigned I = 0; I < NumBlockRows; I++) {
      unsigned BlockSize = VF;
      for (unsigned J = 0; J < NumBlockCols; J += BlockSize) {
        // Gradually lower the vectorization factor to cover the remainder.
        while (J + BlockSize > NumBlockCols) {
          BlockSize /= 2;
        }
        
        Value *Input = InTileVector[I];
        if(InTileVector[I]->getType()->isVectorTy()) {
          auto *Poison = PoisonValue::get(InTileVector[I]->getType());
          Input = new ShuffleVectorInst(InTileVector[I], Poison, 
                  createSequentialMask(J, BlockSize, 0), "block", InsertBefore);
        }
        
        Acc = createReductionAccumulate(VectorID, ScalarID,
            I == 0 && J == 0 ? nullptr : Acc, Input, InsertBefore);
      }
    }
    InsertCallToPrint(Acc, InsertBefore);
    InsertCallToPrint(ReduceInfo.OutTiles, InsertBefore);
    auto *Declaration = Intrinsic::getDeclaration(InsertBefore->getModule(), ScalarID, 
              ArrayRef<Type*>({Acc->getType()}));
    ReduceInfo.OutTiles = CallInst::Create(Declaration->getFunctionType(), Declaration, 
                    ArrayRef<Value *>({Acc, ReduceInfo.OutTiles}), 
                    "reduce.acc", InsertBefore);
    InsertCallToPrint(ReduceInfo.OutTiles, InsertBefore);
  }

  void generateReductionKernel(
      ReductionInfo &ReduceInfo, Intrinsic::ID VectorID, Instruction::BinaryOps ScalarOpcode, 
      Type *EltType, Instruction *InsertBefore) {
    const unsigned VF = std::max<unsigned>(
        TTI.getRegisterBitWidth(true) /
            EltType->getPrimitiveSizeInBits().getFixedSize(), 1U);

    TensorType &InTileTensorType = ReduceInfo.InTile;
    unsigned NumBlockRows = ReduceInfo.NumBlockRows;
    unsigned NumBlockCols = ReduceInfo.NumBlockCols;
    SmallVector<Value *, 16> &InTileVector = ReduceInfo.InTileVector;

    Value *Acc = nullptr;
    for (unsigned I = 0; I < NumBlockRows; I++) {
      unsigned BlockSize = VF;
      for (unsigned J = 0; J < NumBlockCols; J += BlockSize) {
        // Gradually lower the vectorization factor to cover the remainder.
        while (J + BlockSize > NumBlockCols) {
          BlockSize /= 2;
        }
        
        Value *Input = InTileVector[I];
        if(InTileVector[I]->getType()->isVectorTy()) {
          auto *Poison = PoisonValue::get(InTileVector[I]->getType());
          Input = new ShuffleVectorInst(InTileVector[I], Poison, 
                  createSequentialMask(J, BlockSize, 0), "block", InsertBefore);
        }
        
        Acc = createReductionAccumulate(VectorID, ScalarOpcode,
            I == 0 && J == 0 ? nullptr : Acc, Input, InsertBefore);
      }
    }
    InsertCallToPrint(Acc, InsertBefore);
    InsertCallToPrint(ReduceInfo.OutTiles, InsertBefore);
    ReduceInfo.OutTiles = BinaryOperator::Create(ScalarOpcode, Acc, 
                            ReduceInfo.OutTiles, "reduce.acc", InsertBefore);
    InsertCallToPrint(ReduceInfo.OutTiles, InsertBefore);
  }

  Value *lowerReduction(
      CallInst *Reduce, int64_t InitVal, 
      Intrinsic::ID VectorID, Intrinsic::ID ScalarID, 
      unsigned TileSize_M, unsigned TileSize_N, unsigned InnerLoopUnrollFactor) {
    // Get the input and output tensors
    auto *Input = TI->getTensorOperand(Reduce, 0);
    auto *Window = Reduce->getOperand(1);
    auto *Strides = Reduce->getOperand(2);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    TensorType &OutputTensor = TI->getTensorTypeInfoFor(Reduce);
    auto *EltType =
        dyn_cast<VectorType>(Reduce->getType())->getElementType();

    // Set up the tensor reduction interface
    auto &Ctx = Reduce->getParent()->getContext();
    auto ReduceInfo = ReductionInfo(Ctx, InputTensor, Window, Strides,
                      TI->getLayoutVectorFor(Reduce));

    // Create the loop nest information
    ReduceInfo.createLoopNest(*this, TileSize_M, TileSize_N, Reduce);

    // Insert PHIs that represent the tiles
    ReduceInfo.insertTilePHIs(EltType, InitVal);

    // Inner loop body terminator
    auto *InnerBodyTerminator = ReduceInfo.getInnerLoopBody()->getTerminator();

    // Load tile for the input tensor.
    ReduceInfo.InTileVector = loadTile<ReductionInfo>(
        ReduceInfo, TI->getMemPtrFor(Input), InputTensor, ReduceInfo.InTile, EltType,
        ReduceInfo.InTensorIndices, ReduceInfo.WinTensorIndices, 
        ReduceInfo.WindowStrides, {}, false, InnerBodyTerminator);

    // Generate kernel for reduction
    generateReductionKernel(ReduceInfo, VectorID, ScalarID, 
                            EltType, InnerBodyTerminator);
    
    // Store tiles of outputs.
    storeTile<ReductionInfo>(
        ReduceInfo, TI->getMemPtrFor(Reduce), EltType, {}, false,
        (ReduceInfo.getBlockToStoreTile())->getTerminator());
    
    errs() << "--GENERATED REDUCTION KERNEL: " << *(Reduce->getParent()->getParent()) << "\n";

    // Finish completeling the PHIs for tiles
    ReduceInfo.completeTilePHIs();

    // Force unrolling of innermost loop
    forceUnrollOfLoop(
        LI->getLoopFor(ReduceInfo.getInnerLoopBody()), InnerLoopUnrollFactor);

    // Load the tensor now
    auto *VecTy =
        FixedVectorType::get(EltType, TI->getTensorAllocSize(Reduce));
    auto *MallocPtr = TI->getMemPtrFor(Reduce);
    unsigned AS =
        dyn_cast<PointerType>(MallocPtr->getType())->getAddressSpace();
    auto *CastMallocPtr = CastInst::CreatePointerCast(
        MallocPtr, PointerType::get(VecTy, AS), "malloc.cast", Reduce);
    auto *Output =
        new LoadInst(VecTy, CastMallocPtr, "final.load", false, {}, Reduce);

    return Output;
  }

  Value *lowerReduction(
      CallInst *Reduce, int64_t InitVal, 
      Intrinsic::ID VectorID, Instruction::BinaryOps ScalarOpcode, 
      unsigned TileSize_M, unsigned TileSize_N, unsigned InnerLoopUnrollFactor) {
    // Get the input and output tensors
    auto *Input = TI->getTensorOperand(Reduce, 0);
    auto *Window = Reduce->getOperand(1);
    auto *Strides = Reduce->getOperand(2);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    TensorType &OutputTensor = TI->getTensorTypeInfoFor(Reduce);
    auto *EltType =
        dyn_cast<VectorType>(Reduce->getType())->getElementType();

    // Set up the tensor reduction interface
    auto &Ctx = Reduce->getParent()->getContext();
    auto ReduceInfo = ReductionInfo(Ctx, InputTensor, Window, Strides,
                      TI->getLayoutVectorFor(Reduce));

    // Create the loop nest information
    ReduceInfo.createLoopNest(*this, TileSize_M, TileSize_N, Reduce);

    // Insert PHIs that represent the tiles
    ReduceInfo.insertTilePHIs(EltType, InitVal);

    // Inner loop body terminator
    auto *InnerBodyTerminator = ReduceInfo.getInnerLoopBody()->getTerminator();

    // Load tile for the input tensor.
    ReduceInfo.InTileVector = loadTile<ReductionInfo>(
        ReduceInfo, TI->getMemPtrFor(Input), InputTensor, ReduceInfo.InTile, EltType,
        ReduceInfo.InTensorIndices, ReduceInfo.WinTensorIndices, 
        ReduceInfo.WindowStrides, {}, false, InnerBodyTerminator);

    // Generate kernel for reduction
    generateReductionKernel(ReduceInfo, VectorID, ScalarOpcode, 
                            EltType, InnerBodyTerminator);
    
    // Store tiles of outputs.
    storeTile<ReductionInfo>(
        ReduceInfo, TI->getMemPtrFor(Reduce), EltType, {}, false,
        (ReduceInfo.getBlockToStoreTile())->getTerminator());
    
    errs() << "--GENERATED REDUCTION KERNEL: " << *(Reduce->getParent()->getParent()) << "\n";

    // Finish completeling the PHIs for tiles
    ReduceInfo.completeTilePHIs();

    // Force unrolling of innermost loop
    forceUnrollOfLoop(
        LI->getLoopFor(ReduceInfo.getInnerLoopBody()), InnerLoopUnrollFactor);

    // Load the tensor now
    auto *VecTy =
        FixedVectorType::get(EltType, TI->getTensorAllocSize(Reduce));
    auto *MallocPtr = TI->getMemPtrFor(Reduce);
    unsigned AS =
        dyn_cast<PointerType>(MallocPtr->getType())->getAddressSpace();
    auto *CastMallocPtr = CastInst::CreatePointerCast(
        MallocPtr, PointerType::get(VecTy, AS), "malloc.cast", Reduce);
    auto *Output =
        new LoadInst(VecTy, CastMallocPtr, "final.load", false, {}, Reduce);

    return Output;
  }

  Value *lowerReduceMax(
    CallInst *Reduce, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
        auto *EltType = dyn_cast<VectorType>(Reduce->getType())->getElementType();
        bool IsFP = EltType->isFloatingPointTy();
        if(IsFP) {
          return lowerReduction(Reduce, GetMinFor(EltType),
                Intrinsic::vector_reduce_fmax, Intrinsic::maximum,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
        }
        return lowerReduction(Reduce, GetMinFor(EltType),
                Intrinsic::vector_reduce_smax, Intrinsic::smax,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
  }

  Value *lowerReduceMin(
    CallInst *Reduce, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
        auto *EltType = dyn_cast<VectorType>(Reduce->getType())->getElementType();
        bool IsFP = EltType->isFloatingPointTy();
        if(IsFP) {
          return lowerReduction(Reduce, GetMaxFor(EltType),
                Intrinsic::vector_reduce_fmin, Intrinsic::minimum,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
        }
        errs() << "MAX: " << std::numeric_limits<int32_t>::max() << "\n";
        return lowerReduction(Reduce, GetMaxFor(EltType),
                Intrinsic::vector_reduce_smin, Intrinsic::smin,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
  }

  Value *lowerReduceAnd(
    CallInst *Reduce, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
        return lowerReduction(Reduce, (~((int64_t)0)),
                Intrinsic::vector_reduce_and, Instruction::And, 
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
  }

  Value *lowerReduceOr(
    CallInst *Reduce, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
        return lowerReduction(Reduce, 0,
                Intrinsic::vector_reduce_or, Instruction::Or, 
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
  }

  Value *lowerReduceXor(
    CallInst *Reduce, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
        return lowerReduction(Reduce, 0,
                Intrinsic::vector_reduce_xor, Instruction::Xor, 
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
  }

  Value *lowerReduceAdd(
    CallInst *Reduce, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
        auto *EltType = dyn_cast<VectorType>(Reduce->getType())->getElementType();
        bool IsFP = EltType->isFloatingPointTy();
        if(IsFP) {
          return lowerReduction(Reduce, 0, 
                Intrinsic::vector_reduce_fadd, Instruction::FAdd,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
        }
        return lowerReduction(Reduce, 0, 
                Intrinsic::vector_reduce_add, Instruction::Add,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
  }

  Value *lowerReduceMul(
    CallInst *Reduce, unsigned TileSize_M, unsigned TileSize_N,
      unsigned InnerLoopUnrollFactor) {
        auto *EltType = dyn_cast<VectorType>(Reduce->getType())->getElementType();
        bool IsFP = EltType->isFloatingPointTy();
        if(IsFP) {
          return lowerReduction(Reduce, 1, 
                Intrinsic::vector_reduce_fmul, Instruction::FMul,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
        }
        return lowerReduction(Reduce, 1, 
                Intrinsic::vector_reduce_mul, Instruction::Mul,
                TileSize_M, TileSize_N, InnerLoopUnrollFactor);
  }

};


static std::vector<size_t> divisorsSmallerThan(size_t n, size_t kmax) {
  size_t nsqrt = size_t(std::sqrt(n));
  std::vector<size_t> ret;
  for (size_t i = 1; i <= std::min(nsqrt, kmax); ++i) {
    if (n % i != 0)
      continue;
    ret.push_back(i);
    if (n / i <= kmax)
      ret.push_back(n / i);
  }
  return ret;
}

void printKnobsTo(
    const std::string &OutputFile, const std::string &FunName,
    ArrayRef<IntrinsicInst *> TensorInsts, TensorInfo *TI) {
  json::Object instKnobs;
  size_t counter = 0;
  for (auto *II : TensorInsts) {
    auto instName =
        std::to_string(II->getIntrinsicID()) + "_" + std::to_string(counter);
    II->setName(instName);
    ++counter;

    auto getOperandDivisors =
        [TI, II](size_t op_idx, int64_t shape_dim, size_t kmax) {
          auto *input = TI->getTensorOperand(II, op_idx);
          TensorType &inputTensorTy = TI->getTensorTypeInfoFor(input);
          ArrayRef<uint32_t> shape = inputTensorTy.getShapeVector();
          if (shape_dim < 0)
            shape_dim += shape.size();
          auto divisors = divisorsSmallerThan(shape[shape_dim], kmax);
          return json::Value(
              json::Object{{"data_type", "int"}, {"values", divisors}});
        };
    json::Value unroll(
        json::Object{{"data_type", "int"}, {"data_range", {0, 16}}});
    json::Object knobs;
    switch (II->getIntrinsicID()) {
    case Intrinsic::tensor_relu:
    case Intrinsic::tensor_tanh:
    case Intrinsic::tensor_sigmoid:
      knobs["TileSize"] = getOperandDivisors(0, -1, 128);
      break;
    case Intrinsic::tensor_matmul:
      knobs["TileSize_M"] = getOperandDivisors(0, -2, 128);
      knobs["TileSize_K"] = getOperandDivisors(0, -1, 128);
      knobs["TileSize_N"] = getOperandDivisors(1, -1, 128);
      knobs["InnerLoopUnrollFactor"] = unroll;
      break;
    case Intrinsic::tensor_transpose:
      knobs["TileSize_M"] = getOperandDivisors(0, -2, 128);
      knobs["TileSize_N"] = getOperandDivisors(0, -1, 128);
      knobs["InnerLoopUnrollFactor"] = unroll;
      break;
    default:
      continue;
    }
    instKnobs[instName] = json::Value(std::move(knobs));
  }

  json::Object fileObject;
  std::ifstream fin(OutputFile);
  if (fin.good()) {
    std::string fileStr(
        (std::istreambuf_iterator<char>(fin)),
        std::istreambuf_iterator<char>());
    fileObject = *llvm::json::parse(fileStr).get().getAsObject();
  }
  fileObject[FunName] = json::Value(std::move(instKnobs));

  std::error_code EC;
  llvm::raw_fd_ostream fout(OutputFile, EC);
  fout << llvm::formatv("{0:2}\n", json::Value(std::move(fileObject)));
}

llvm::StringMap<llvm::StringMap<int64_t>>
readKnobsFrom(const std::string &InputFile, const std::string &FunName) {
  llvm::StringMap<llvm::StringMap<int64_t>> funcKnobsRet;

  std::ifstream fin(InputFile);
  std::string fileStr(
      (std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
  auto fileObject = *llvm::json::parse(fileStr).get().getAsObject();
  auto *funcKnobsVP = fileObject.get(FunName);
  if (funcKnobsVP == nullptr)
    return funcKnobsRet;
  auto funcKnobs = *funcKnobsVP->getAsObject();

  for (const auto &instKnobs : funcKnobs) {
    llvm::StringMap<int64_t> instKnobsRet;
    for (const auto &knob : *instKnobs.second.getAsObject())
      instKnobsRet[knob.first.str()] = knob.second.getAsInteger().getValue();
    funcKnobsRet[instKnobs.first.str()] = instKnobsRet;
  }
  return funcKnobsRet;
}

bool LowerTensorIntrinsicsLegacyPass::runOnFunction(Function &F) {
  auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto &TI = getAnalysis<TensorInfoWrapperPass>().getTensorInfo(&F);
  LLVM_DEBUG(dbgs() << "ALLOCATING BUFFERS\n");
  TI.bufferAlloc(InitTensorsWithMemCpy);

  LLVM_DEBUG(dbgs() << "PRINTING FUNCTION: " << F << "\n");

  LLVM_DEBUG(dbgs() << "LOWERING TENSOR INTRINSICS\n");
  LowerTensorIntrinsics LMT(F, TTI, &DT, &LI, &TI);

  SmallVector<IntrinsicInst *, 16> TensorInsts;
  SmallVector<Instruction *, 4> ToBeRemoved;
  DenseMap<Instruction *, SmallVector<Value *, 2>> MatMulInstMap;

  // Track fusable instructions
  DenseMap<Instruction *, bool> IsFusableInst;

  ReversePostOrderTraversal<Function *> RPOT(&F);
  for (auto *BB : RPOT) {
    for (Instruction &I : *BB) {
      if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
        switch (II->getIntrinsicID()) {
        case Intrinsic::tensor_typeinfo: {
          ToBeRemoved.push_back(II);
          break;
        }
        case Intrinsic::tensor_matmul: {
          auto *Op1 = dyn_cast<IntrinsicInst>(II->getArgOperand(0));
          auto *Op2 = dyn_cast<IntrinsicInst>(II->getArgOperand(1));
          auto *InTensor1 = dyn_cast<IntrinsicInst>(Op1->getArgOperand(0));
          auto *InTensor2 = dyn_cast<IntrinsicInst>(Op1->getArgOperand(1));
          Value *Arg1;
          Value *Arg2;
          if (InTensor1 &&
              InTensor1->getIntrinsicID() == Intrinsic::tensor_transpose &&
              IsFusableInst[InTensor1]) {
            ToBeRemoved.push_back(InTensor1);
            Arg1 = dyn_cast<IntrinsicInst>(InTensor1)->getArgOperand(0);
          } else {
            Arg1 = Op1;
          }
          if (InTensor2 &&
              InTensor2->getIntrinsicID() == Intrinsic::tensor_transpose &&
              IsFusableInst[InTensor2]) {
            ToBeRemoved.push_back(InTensor2);
            Arg2 = dyn_cast<IntrinsicInst>(InTensor2)->getArgOperand(0);
          } else {
            Arg2 = Op2;
          }
          SmallVector<Value *, 2> Args;
          Args.push_back(Arg1);
          Args.push_back(Arg2);
          MatMulInstMap[II] = Args;
          TensorInsts.push_back(II);
          break;
        }
        case Intrinsic::tensor_reduce_max:
        case Intrinsic::tensor_reduce_min:
        case Intrinsic::tensor_reduce_and:
        case Intrinsic::tensor_reduce_or:
        case Intrinsic::tensor_reduce_xor:
        case Intrinsic::tensor_reduce_add:
        case Intrinsic::tensor_reduce_mul:
        case Intrinsic::tensor_transpose:
        case Intrinsic::tensor_relu:
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
        case Intrinsic::tensor_tanh:
        case Intrinsic::tensor_sigmoid:
        case Intrinsic::tensor_broadcast:
          TensorInsts.push_back(II);
          break;
        default:
          continue;
        }
      }
    }
  }

  // Remove instructions in TensorInsts that are in ToBeRemoved.
  auto eraseAfter = std::remove_if(
      TensorInsts.begin(), TensorInsts.end(), [&](Instruction *II) {
        return find(ToBeRemoved, II) != ToBeRemoved.end();
      });
  TensorInsts.erase(eraseAfter, TensorInsts.end());

  if (PrintKnobsTo != "") {
    printKnobsTo(PrintKnobsTo, F.getName().str(), TensorInsts, &TI);
    return false;
  }

  auto knobs = ReadKnobsFrom == ""
                   ? llvm::StringMap<llvm::StringMap<int64_t>>()
                   : readKnobsFrom(ReadKnobsFrom, F.getName().str());
  for (auto *II : TensorInsts) {
    auto instKnobs = knobs[II->getName()];
    auto getKnob = [&instKnobs](const char *key, int defaultVal) {
      auto it = instKnobs.find(key);
      if (it == instKnobs.end())
        return defaultVal;
      return int(it->second);
    };

    Value *Output = nullptr;
    switch (II->getIntrinsicID()) {
    case Intrinsic::tensor_relu:
      Output = LMT.lowerRelu(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_tanh:
      Output = LMT.lowerTanh(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_sigmoid:
      Output = LMT.lowerSigmoid(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_sin:
      Output = LMT.lowerSin(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_cos:
      Output = LMT.lowerCos(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_exp:
      Output = LMT.lowerExp(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_exp2:
      Output = LMT.lowerExp2(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_log:
      Output = LMT.lowerLog(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_log2:
      Output = LMT.lowerLog2(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_log10:
      Output = LMT.lowerLog10(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_sqrt:
      Output = LMT.lowerSqrt(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_fabs:
      Output = LMT.lowerFabs(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_floor:
      Output = LMT.lowerFloor(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_ceil:
      Output = LMT.lowerCeil(II, getKnob("TileSize", TileSize));
      break;
    case Intrinsic::tensor_broadcast:
      Output = LMT.lowerBroadcast(II);
      break;
    case Intrinsic::tensor_matmul:
      Output = LMT.lowerMatMul(
          MatMulInstMap[II][0], MatMulInstMap[II][1], II,
          getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("TileSize_K", TileSize_K),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_reduce_max:
       Output = LMT.lowerReduceMax(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_reduce_min:
       Output = LMT.lowerReduceMin(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_reduce_and:
       Output = LMT.lowerReduceAnd(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_reduce_or:
       Output = LMT.lowerReduceOr(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_reduce_xor:
       Output = LMT.lowerReduceXor(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_reduce_add:
       Output = LMT.lowerReduceAdd(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_reduce_mul:
       Output = LMT.lowerReduceMul(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    case Intrinsic::tensor_transpose:
      Output = LMT.lowerTranspose(
          II, getKnob("TileSize_M", TileSize_M), getKnob("TileSize_N", TileSize_N),
          getKnob("InnerLoopUnrollFactor", InnerLoopUnrollFactor));
      break;
    default:
      continue;
    }
    // Remove the store associated witht the operation
    for (auto *User : II->users()) {
      LLVM_DEBUG(dbgs() << "USER: " << *User << "\n");
      if (auto *SI = dyn_cast<StoreInst>(User)) {
        LLVM_DEBUG(dbgs() << "STORE FOUND\n");
        // Put the value being stored in the map
        // SI->eraseFromParent();
        ToBeRemoved.push_back(SI);
      }
    }
    LLVM_DEBUG(dbgs() << "REPLACING USES\n");
    II->replaceAllUsesWith(Output);
    // TODO: update the tensor information
    // II->eraseFromParent();
    // LLVM_DEBUG(dbgs() << *(II->getParent()->getParent()) << "\n");
    ToBeRemoved.push_back(II);
  }

  // Remove the typeinfo intrinsics
  for (auto *I : ToBeRemoved) {
    // Remove the tensor info
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      if(II->getIntrinsicID() == Intrinsic::tensor_typeinfo) {
        TI.removeTenorInfoFor(II->getArgOperand(0));
      }
    }
    TI.removeTenorInfoFor(I);
    I->replaceAllUsesWith(UndefValue::get(I->getType()));
    I->eraseFromParent();
  }

  bool BrokenDebugInfo = true;
  assert(verifyModule(BrokenDebugInfo, *(F.getParent()), errs()));

  return true;
}

char LowerTensorIntrinsicsLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(
    LowerTensorIntrinsicsLegacyPass, "lower-tensor",
    "Pass to lower tensor intrinsics", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TensorInfoWrapperPass)
INITIALIZE_PASS_END(
    LowerTensorIntrinsicsLegacyPass, "lower-tensor",
    "Pass to lower tensor intrinsics", false, false)

FunctionPass *llvm::createLowerTensorIntrinsicsPass() {
  return new LowerTensorIntrinsicsLegacyPass();
}

