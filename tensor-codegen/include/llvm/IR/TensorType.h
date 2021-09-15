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

#ifndef LLVM_IR_TENSORTYPE_H
#define LLVM_IR_TENSORTYPE_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LLVMContext.h"

#define DEBUG_TYPE "TensorType"

namespace llvm {

// Pseudo Tensor Type in LLVM
class TensorType {

    // Tensor properties
    Value *Shape;
    Value *Layout;
    Value *Padding;

    // This stores the indices in a vector form for easier access
    SmallVector<unsigned, 4> ShapeVector;
    SmallVector<unsigned, 4> LayoutVector;
    SmallVector<unsigned, 4> PaddingVector;

public:
    TensorType(Value *Shape, Value *Layout, Value *Padding) 
        : Shape{Shape}, Layout{Layout}, Padding{Padding} {
        // Some sanity checks
        auto *ShapeFVTy = dyn_cast<FixedVectorType>(Shape->getType());
        auto *LayoutFVTy = dyn_cast<FixedVectorType>(Layout->getType());
        auto *PaddingFVTy = dyn_cast<FixedVectorType>(Padding->getType());

        assert(ShapeFVTy && "Tenosr shape is of fixed vector type.");
        assert(LayoutFVTy && "Tenosr layout is of fixed vector type.");
        assert(PaddingFVTy && "Tenosr padding is of fixed vector type.");

        assert(ShapeFVTy->getNumElements() == LayoutFVTy->getNumElements()
         && "Layout vector and shape vector are of the same length.");
        assert(ShapeFVTy->getNumElements() == PaddingFVTy->getNumElements()
         && "Layout vector and shape vector are of the same length.");

        // Now lets fill up the vectors
        LLVM_DEBUG(dbgs() << "SHAPE========================" << *Shape << "\n");
        LLVM_DEBUG(dbgs() << "LAYOUT========================" << *Layout << "\n");
        LLVM_DEBUG(dbgs() << "PADDING========================" << *Padding << "\n");
        if(auto *CV = dyn_cast<ConstantDataVector>(Shape)) {
            LLVM_DEBUG(dbgs() << "CONSTANT VECTOR====================\n");
            for(unsigned I = 0; I < ShapeFVTy->getNumElements(); I++) {
                auto *C = CV->getAggregateElement(I);
                ShapeVector.push_back(dyn_cast<ConstantInt>(C)->getZExtValue());
            }
        }
        if(auto *CV = dyn_cast<ConstantDataVector>(Layout)) {
            LLVM_DEBUG(dbgs() << "CONSTANT VECTOR====================\n");
            for(unsigned I = 0; I < LayoutFVTy->getNumElements(); I++) {
                auto *C = CV->getAggregateElement(I);
                LayoutVector.push_back(dyn_cast<ConstantInt>(C)->getZExtValue());
            }
        }
        if(auto *CV = dyn_cast<ConstantDataVector>(Padding)) {
            LLVM_DEBUG(dbgs() << "CONSTANT VECTOR====================\n");
            for(unsigned I = 0; I < PaddingFVTy->getNumElements(); I++) {
                auto *C = CV->getAggregateElement(I);
                PaddingVector.push_back(dyn_cast<ConstantInt>(C)->getZExtValue());
            }
        }
    }

    TensorType() : Shape{nullptr}, Layout{nullptr}, Padding{nullptr} {};

    TensorType(LLVMContext &Ctx, SmallVector<unsigned, 4> &ShapeVect, 
                SmallVector<unsigned, 4> &LayoutVect, SmallVector<unsigned, 4> &PaddingVect) {
        // Just a sanity check first.
        unsigned NumDims = ShapeVect.size();
        assert(NumDims ==  LayoutVect.size() && NumDims == PaddingVect.size() 
            && "Size of shape, layout and padding vectors must the same.");

        // Initialize the vectors
        ShapeVector = ShapeVect;
        LayoutVector = LayoutVect;
        PaddingVector = PaddingVect;

        // Now, create the LLVM values
        auto *Int32Ty = Type::getInt32Ty(Ctx);
        std::vector<Constant *> ConstShapeVec;
        std::vector<Constant *> ConstLayoutVec;
        std::vector<Constant *> ConstPaddingVec;
        for(unsigned I = 0; I < NumDims; I++) {
            ConstShapeVec.push_back(ConstantInt::get(Int32Ty, ShapeVect[I]));
            ConstLayoutVec.push_back(ConstantInt::get(Int32Ty, LayoutVect[I]));
            ConstPaddingVec.push_back(ConstantInt::get(Int32Ty, PaddingVect[I]));
        }

        // Initilaize the LLVM values
        Shape = ConstantVector::get(ArrayRef<Constant *>(ConstShapeVec));
        Layout = ConstantVector::get(ArrayRef<Constant *>(ConstLayoutVec));
        Padding = ConstantVector::get(ArrayRef<Constant *>(ConstPaddingVec));
    }

    bool isValidTensorType() {
        if(!Shape || !Layout || !Padding) {
            return false;
        }

        auto *ShapeFVTy = dyn_cast<FixedVectorType>(Shape->getType());
        auto *LayoutFVTy = dyn_cast<FixedVectorType>(Layout->getType());
        auto *PaddingFVTy = dyn_cast<FixedVectorType>(Padding->getType());
        if(!ShapeFVTy || !LayoutFVTy || !PaddingFVTy) {
            return false;
        }

        if(ShapeFVTy->getNumElements() != LayoutFVTy->getNumElements()
        || ShapeFVTy->getNumElements() != PaddingFVTy->getNumElements()) {
            return false;
        }

        return true;
    }
 
    // Interface to get tensor type info
    Value *getShape() const { return Shape; }
    Value *getLayout() const { return Layout; }
    Value *getPadding() const { return Padding; }

    SmallVector<unsigned, 4> &getShapeVector() { return ShapeVector; }
    SmallVector<unsigned, 4> &getLayoutVector() { return LayoutVector; }
    SmallVector<unsigned, 4> &getPaddingVector() { return PaddingVector; }

    std::vector<Value *> getTensorPropertiesValueVector() {
      return std::vector {Shape, Layout, Padding};
    }

    std::vector<Type *> getTensorPropertiesTypeVector() {
      return std::vector {Shape->getType(), Layout->getType(), Padding->getType()};
    }
    
    unsigned getNumDimensions() {
        auto *ShapeFVTy = dyn_cast<FixedVectorType>(Shape->getType());
        return ShapeFVTy->getNumElements();
    }

    unsigned getTensorSize() {
        unsigned Size = 1;
        for(unsigned I = 0; I < ShapeVector.size(); I++) {
            LLVM_DEBUG(dbgs() << "ShapeVector[I]: " << ShapeVector[I] << "\n");
            Size *= ShapeVector[I];
        }
        return Size;
    }

    unsigned getShapeDimensionVal(unsigned Index) const {
        assert(Index < ShapeVector.size() 
        && "Given index is more than number of dimensions.");
        return ShapeVector[Index];
    }

    unsigned getLayoutVal(unsigned Index) const {
        assert(Index < LayoutVector.size() 
        && "Given index is more than number of dimensions.");
        return LayoutVector[Index];
    }

    unsigned getPaddingVal(unsigned Index) const {
        assert(Index < PaddingVector.size() 
        && "Given index is more than number of dimensions.");
        return PaddingVector[Index];
    }
    
    void setTensorProperties(Value *TShape, Value *TLayout, Value *TPadding) {
        Shape = TShape;
        Layout = TLayout;
        Padding = TPadding;
    }

    bool operator==(const TensorType &Other) {
        return ((Shape == Other.Shape) && (Layout == Other.Layout)
              && (Padding == Other.Padding));
    }

    TensorType operator=(const TensorType &Other) {
        Shape = Other.Shape; 
        Layout = Other.Layout;
        Padding = Other.Padding;
        ShapeVector = Other.ShapeVector; 
        LayoutVector = Other.LayoutVector;
        PaddingVector = Other.PaddingVector;
        return *this;
    }

    void print(raw_ostream &os) {
        if(!isValidTensorType()) {
            os << "SHAPE: invalid\n";
            os << "LAYOUT: invalid\n";
            os << "PADDING: invalid\n";
        } else {
            os << "SHAPE: " << *Shape << "\n";
            os << "LAYOUT: " << *Layout << "\n";
            os << "PADDING: " << *Padding << "\n";
        }
    }
};

}   // end of namespace llvm

#endif //LLVM_IR_TENSORTYPE_H
