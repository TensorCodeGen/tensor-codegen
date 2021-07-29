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

#include "llvm/Analysis/TensorProperties.h"
#include "llvm/IR/TensorType.h"
#include "llvm/Transforms/Scalar/LowerTensorIntrinsics.h"

#include <fstream>


using namespace llvm;

static cl::opt<std::string> PrintKnobsTo(
    "print-knobs-to",
    cl::desc("If set, only print knobs to the given file (doesn't actually "
             "lower the instructions)"));
static cl::opt<std::string> ReadKnobsFrom(
    "read-knobs-from", cl::desc("If set, read knob values from the given file "
                                "and lower instructions with these values"));

unsigned TileSize_M = 2;
unsigned TileSize_N = 2;
unsigned TileSize_K = 2;

unsigned TileSize = 2;

bool FuseTransposeAndMatmul = false;

bool InitTensorsWithMemCpy = true;

unsigned InnerLoopUnrollFactor = 0;

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

  if (MustHaveBody) {
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

  // Map the instructions with what values need to be used
  // to remove them.
  DenseMap<Instruction *, Instruction *> ReplacementMap;

public:
  LowerTensorIntrinsics(
      Function &F, TargetTransformInfo &TTI, DominatorTree *DT, LoopInfo *LI,
      TensorInfo *TI)
      : Func(F), DL(F.getParent()->getDataLayout()), TTI(TTI), DT(DT), LI(LI),
        TI(TI) {}

  class TensorMapinfo {
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

    unsigned getStride(TensorType &Tensor) { return getNumColumns(Tensor); }

    virtual unsigned getNumOutputTileVectors() const = 0;

    virtual TensorType &getOutputTensor() = 0;

    virtual TensorType &getOutputTile() = 0;

    virtual Value *getOutputTileVector(unsigned Index) = 0;

    virtual SmallVector<Value *, 4> &getOutTensorIndices() = 0;
  };

  class MatMulInfo : public TensorMapinfo {
  public:
    // Input tensor dimensions
    unsigned LTensorDim;
    unsigned RTensorDim;
    unsigned CommonDim;

    // Input and output tensor type infomation
    TensorType LTensor;
    TensorType RTensor;
    TensorType OutputTensor;

    // Tile dimensions
    unsigned LTileDim;
    unsigned RTileDim;
    unsigned TileCommonDim;

    // Indices for the input and output tensors
    SmallVector<Value *, 4> LTensorIndices;
    SmallVector<Value *, 4> RTensorIndices;
    SmallVector<Value *, 4> OutTensorIndices;

    // Tile vectors
    SmallVector<Value *, 16> LTileVector;
    SmallVector<Value *, 16> RTileVector;
    SmallVector<Value *, 16> OutTileVector;
    SmallVector<PHINode *, 16> TilePHIs;

    // Tensor type info for the tiles
    TensorType LTile;
    TensorType RTile;
    TensorType OutTile;

    // Loop nest info
    TiledLoopNestInfo LoopNestInfo;

    BasicBlock *getInnerLoopBody() { return LoopNestInfo.InnerLoopBody; }

    BasicBlock *getBlockToStoreTile() {
      return LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 2];
    }

    unsigned getNumOutputTileVectors() const { return OutTileVector.size(); }

    TensorType &getOutputTensor() { return OutputTensor; }

    TensorType &getOutputTile() { return OutTile; }

    Value *getOutputTileVector(unsigned Index) { return OutTileVector[Index]; }

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
    }

    void insertTilePHIs(Type *ElemType) {
      unsigned TileRows;
      unsigned TileCols;
      if (isColumnMajor(OutputTensor)) {
        TileRows = RTileDim;
        TileCols = LTileDim;
      } else {
        TileRows = LTileDim;
        TileCols = RTileDim;
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
        OutTileVector.push_back(Phi);
        TilePHIs.push_back(Phi);
      }
    }

    void completeTilePHIs() {
      BasicBlock *InnerLoopLatch =
          LoopNestInfo.LoopLatches[LoopNestInfo.LoopLatches.size() - 1];
      for (unsigned I = 0; I < OutTileVector.size(); I++) {
        TilePHIs[I]->addIncoming(OutTileVector[I], InnerLoopLatch);
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
      LoopBounds.push_back(LTensorDim);
      LoopBounds.push_back(RTensorDim);
      LoopBounds.push_back(CommonDim);

      LoopSteps.push_back(TileSize_M);
      LoopSteps.push_back(TileSize_N);
      LoopSteps.push_back(TileSize_K);

      LoopStartIndices.push_back(0);
      LoopStartIndices.push_back(0);
      LoopStartIndices.push_back(0);

      LoopNestInfo = TiledLoopNestInfo(LoopBounds, LoopSteps, LoopStartIndices);

      LTileDim = TileSize_M;
      RTileDim = TileSize_N;
      TileCommonDim = TileSize_K;
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
        ShapeVector.push_back(TileCommonDim);
        ShapeVector.push_back(LTileDim);
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        ShapeVector.push_back(LTileDim);
        ShapeVector.push_back(TileCommonDim);
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
        ShapeVector.push_back(RTileDim);
        ShapeVector.push_back(TileCommonDim);
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        ShapeVector.push_back(TileCommonDim);
        ShapeVector.push_back(RTileDim);
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
        ShapeVector.push_back(RTileDim);
        ShapeVector.push_back(LTileDim);
        LayoutVector.push_back(1);
        LayoutVector.push_back(0);
      } else {
        // Row major case
        ShapeVector.push_back(LTileDim);
        ShapeVector.push_back(RTileDim);
        LayoutVector.push_back(0);
        LayoutVector.push_back(1);
      }

      // Set the tensor type information for the output tile
      OutTile = TensorType(Ctx, ShapeVector, LayoutVector, PaddingVector);
    }
  };

  class ElementWiseInfo : public TensorMapinfo {
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

    unsigned getNumOutputTileVectors() const { return 0; }

    TensorType &getOutputTensor() { return *(new TensorType()); }

    TensorType &getOutputTile() { return *(new TensorType()); }

    Value *getOutputTileVector(unsigned Index) { return nullptr; }

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

  class TensorTransformInfo : public TensorMapinfo {
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
    SmallVector<Value *, 16> OutTileVector;

    // Tiled loop nest info
    TiledLoopNestInfo LoopNestInfo;

    // Indices for indexing into the tensors
    SmallVector<Value *, 4> InTensorIndices;
    SmallVector<Value *, 4> OutTensorIndices;

    unsigned getNumOutputTileVectors() const { return OutTileVector.size(); }

    TensorType &getOutputTensor() { return OutputTensor; }

    TensorType &getOutputTile() { return OutTile; }

    Value *getOutputTileVector(unsigned Index) { return OutTileVector[Index]; }

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
      assert(
          isValidTranspose(InputTensor, OutputTensor) &&
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
        OutTileVector.push_back(UndefValue::get(TileVecTy));
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
      Value *BasePtr, Value *VecIdx, Value *Stride, unsigned NumElements,
      Type *EltType, Instruction *InsertBefore) {

    assert(
        (!dyn_cast<ConstantInt>(Stride) ||
         dyn_cast<ConstantInt>(Stride)->getZExtValue() >= NumElements) &&
        "Stride must be >= the number of elements in the result vector.");
    unsigned AS = dyn_cast<PointerType>(BasePtr->getType())->getAddressSpace();

    // Get pointer to the start of the selected vector. Skip GEP creation,
    // if we select vector 0.
    Instruction *VecStart;
    if (dyn_cast<ConstantInt>(VecIdx) &&
        dyn_cast<ConstantInt>(VecIdx)->isZero()) {
      VecStart = dyn_cast<Instruction>(BasePtr);
    } else {
      VecStart = BinaryOperator::Create(
          Instruction::Mul, VecIdx, Stride, "vec.start", InsertBefore);
      VecStart = GetElementPtrInst::Create(
          EltType, BasePtr, VecStart, "vec.gep", InsertBefore);
    }

    // Cast elementwise vector start pointer to a pointer to a vector
    // (EltType x NumElements)*.
    auto *VecType = FixedVectorType::get(EltType, NumElements);
    Type *VecPtrType = PointerType::get(VecType, AS);
    return CastInst::CreatePointerCast(
        VecStart, VecPtrType, "vec.cast", InsertBefore);
  }

  /// Indices to index into the given tensor are assumed to be frmom outermost
  /// dimensions to
  /// innermost dimensions.
  Value *computeIndex(
      TensorType &Tensor, SmallVector<Value *, 4> &InductionVars,
      unsigned NumCollapsedLoops, Instruction *InsertBefore) {
    SmallVector<unsigned, 4> &Shape = Tensor.getShapeVector();
    unsigned NumDims = Shape.size();
    assert(
        InductionVars.size() == (NumDims - NumCollapsedLoops) &&
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

  template <typename T>
  SmallVector<Value *, 16> loadTile(
      T &TensorOpInfo, Value *TensorPtr, TensorType &InTensor,
      TensorType &InTile, Type *EltTy, SmallVector<Value *, 4> &Indices,
      MaybeAlign Align, bool IsVolatile, Instruction *InsertBefore) {

    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Offset = computeIndex(InTensor, Indices, 0, InsertBefore);
    auto *TileStart = GetElementPtrInst::Create(
        EltTy, TensorPtr, ArrayRef<Value *>(Offset), "tile.start",
        InsertBefore);

    SmallVector<Value *, 16> Result;
    auto *VecTy = FixedVectorType::get(EltTy, TensorOpInfo.getStride(InTile));
    auto *Stride = ConstantInt::get(Int32Ty, TensorOpInfo.getStride(InTensor));
    for (unsigned I = 0; I < TensorOpInfo.getNumRows(InTile); ++I) {
      auto *GEP = computeVectorAddr(
          TileStart, ConstantInt::get(Int32Ty, I), Stride,
          TensorOpInfo.getStride(InTile), EltTy, InsertBefore);
      auto *Vector = new LoadInst(
          VecTy, GEP, "row.load", IsVolatile,
          getAlignForIndex(I, Stride, EltTy, Align), InsertBefore);
      Result.push_back(Vector);
    }
    return Result;
  }

  template <typename T>
  void storeTile(
      T &TensorOpInfo, Value *TensorPtr, Type *EltTy, MaybeAlign MAlign,
      bool IsVolatile, Instruction *InsertBefore) {

    auto *Int32Ty = Type::getInt32Ty(InsertBefore->getParent()->getContext());
    auto *Offset = computeIndex(
        TensorOpInfo.getOutputTensor(), TensorOpInfo.getOutTensorIndices(), 0,
        InsertBefore);

    LLVM_DEBUG(dbgs() << "INSERTING COMPUTE INDEX: \n");
    LLVM_DEBUG(dbgs() << *(InsertBefore->getParent()->getParent()) << "\n");
    LLVM_DEBUG(dbgs() << "TENSOR PTR: " << TensorPtr << "\n");

    auto *TileStart = GetElementPtrInst::Create(
        EltTy, TensorPtr, ArrayRef<Value *>(Offset), "tile.start",
        InsertBefore);

    LLVM_DEBUG(dbgs() << "INSERTING GET ELEM PTR: \n");
    LLVM_DEBUG(dbgs() << *(InsertBefore->getParent()->getParent()) << "\n");

    auto *Stride = ConstantInt::get(
        Int32Ty, TensorOpInfo.getStride(TensorOpInfo.getOutputTensor()));
    for (unsigned I = 0; I < TensorOpInfo.getNumOutputTileVectors(); ++I) {
      auto *GEP = computeVectorAddr(
          TileStart, ConstantInt::get(Int32Ty, I), Stride,
          TensorOpInfo.getStride(TensorOpInfo.getOutputTile()), EltTy,
          InsertBefore);

      new StoreInst(
          TensorOpInfo.getOutputTileVector(I), GEP, IsVolatile,
          getAlignForIndex(I, Stride, EltTy, MAlign), InsertBefore);

      LLVM_DEBUG(dbgs() << "INSERTING STORE INST: \n");
      LLVM_DEBUG(dbgs() << *(InsertBefore->getParent()->getParent()) << "\n");
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

  Value *extractVector(
      MatMulInfo MMInfo, TensorType TensorTypeInfo,
      SmallVector<Value *, 16> &TensorVect, unsigned I, unsigned J,
      unsigned NumElts, Instruction *InsertBefore) const {
    Value *Vec =
        MMInfo.isColumnMajor(TensorTypeInfo) ? TensorVect[J] : TensorVect[I];
    auto *Poison = PoisonValue::get(Vec->getType());
    return new ShuffleVectorInst(
        Vec, Poison,
        createSequentialMask(
            MMInfo.isColumnMajor(TensorTypeInfo) ? I : J, NumElts, 0),
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

  void generateMatrixMultiplyKernel(
      MatMulInfo &MMInfo, Type *EltType, Instruction *InsertBefore) {
    const unsigned VF = std::max<unsigned>(
        TTI.getRegisterBitWidth(true) /
            EltType->getPrimitiveSizeInBits().getFixedSize(),
        1U);

    TensorType &LTileTensorType = MMInfo.LTile;
    TensorType &RTileTensorType = MMInfo.RTile;
    unsigned R = MMInfo.LTileDim;
    unsigned C = MMInfo.RTileDim;
    unsigned M = MMInfo.TileCommonDim;

    SmallVector<Value *, 16> &A = MMInfo.LTileVector;
    SmallVector<Value *, 16> &B = MMInfo.RTileVector;
    SmallVector<Value *, 16> &TileResult = MMInfo.OutTileVector;

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
            // InsertCallToPrint(Splat, InsertBefore);
            Sum = createMulAdd(
                isSumZero && K == 0 ? nullptr : Sum, Splat, R, IsFP,
                InsertBefore);
            // InsertCallToPrint(Sum, InsertBefore);
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

    // Insert PHIs that represent the tiles
    MMInfo.insertTilePHIs(EltType);

    LLVM_DEBUG(dbgs() << "INSERTING PHIS: \n");
    LLVM_DEBUG(dbgs() << *(MatMul->getParent()->getParent()) << "\n");

    // Inner loop body terminator
    auto *InnerBodyTerminator = (MMInfo.getInnerLoopBody())->getTerminator();

    // Load tiles of the operands.
    MMInfo.LTileVector = loadTile<MatMulInfo>(
        MMInfo, TI->getMemPtrFor(LTensor), LTensorType, MMInfo.LTile, EltType,
        MMInfo.LTensorIndices, {}, false, InnerBodyTerminator);
    MMInfo.RTileVector = loadTile<MatMulInfo>(
        MMInfo, TI->getMemPtrFor(RTensor), RTensorType, MMInfo.RTile, EltType,
        MMInfo.RTensorIndices, {}, false, InnerBodyTerminator);

    LLVM_DEBUG(dbgs() << "INSERTING LOADS: \n");
    LLVM_DEBUG(dbgs() << *(MatMul->getParent()->getParent()) << "\n");

    // Generate the matmul kernel
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

  Constant *getConstantValue(LLVMContext &Ctx, Type *Ty, int64_t Val) {
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

  Value *convertToFloat(Value *V, Instruction *InsertBefore) {
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
        auto *CastElem = convertToFloat(Elem, InsertBefore);

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

    return Output;
  }

  Value *generateScalarReluKernel(
      ElementWiseInfo &EwInfo, Value *Input, Type *ElemTy,
      Instruction *InsertBefore) {
    auto &Ctx = InsertBefore->getParent()->getContext();
    auto *Zero = getConstantValue(Ctx, ElemTy, 0);

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
      auto *CastElem = convertToFloat(Elem, InsertBefore);

      // Compute the exponent
      auto *Two = getConstantValue(Ctx, CastElem->getType(), 2);
      auto *Exponent = BinaryOperator::Create(
          Instruction::FMul, Two, Elem, "exponent", InsertBefore);
      auto *Exp = insertIntrinsicOperation(Intrinsic::exp, Exponent, 
                        Exponent->getType(), "exp", InsertBefore);

      // Compute Tanh
      auto *One = getConstantValue(Ctx, Exp->getType(), 1);
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
      auto *Exponent = convertToFloat(Elem, InsertBefore);

      // Compute the exponent
      auto *Exp = insertIntrinsicOperation(Intrinsic::exp, Exponent, 
                            Exponent->getType(), "exp", InsertBefore);

      // Compute Tanh
      auto *One = getConstantValue(Ctx, Exp->getType(), 1);
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

  Value *lowerBroadcast(CallInst *Broadcast) {
    auto *Input = TI->getTensorOperand(Broadcast, 0);
    TensorType &InputTensor = TI->getTensorTypeInfoFor(Input);
    unsigned NumElems = InputTensor.getTensorSize();
    auto *BroadcastVal = Broadcast->getArgOperand(1);

    return generateBroadcastKernel(BroadcastVal, NumElems, Broadcast);
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
        TTInfo.OutTileVector[J] = InsertElementInst::Create(
            TTInfo.OutTileVector[J], V, ConstantInt::get(I32Ty, I),
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
  for (auto *II : ToBeRemoved) {
    II->eraseFromParent();
  }

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

