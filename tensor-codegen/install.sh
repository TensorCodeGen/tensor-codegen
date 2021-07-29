
CURRENT_DIR=`pwd`
INSTALL_DIR=`pwd`/install
BUILD_DIR=$CURRENT_DIR/build



LLVM_SRC=$CURRENT_DIR/../llvm
CLANG_SRC=$CURRENT_DIR/../clang
TENSOR_DIR=$LLVM_SRC/lib/Transforms/Tensor
PATCHES_DIR=llvm_patches

if [ ! -d $LLVM_SRC/tools/clang ]; then
    echo "Copying clang over to llvm dir"
    cp -r $CLANG_SRC $LLVM_SRC/tools
    echo "Clang copy complete"
fi

if [ ! -d $TENSOR_DIR ]; then
    echo "Creating Tensor directory"
    # Copy Tensor Transformation files
    mkdir -p $TENSOR_DIR

else
    echo "Tensor directory already exists"
fi


cp $CURRENT_DIR/lib/Transforms/Tensor/* $TENSOR_DIR
#ln -s $CURRENT_DIR/lib/Transforms/Tensor/Tensor.cpp $TENSOR_DIR/Tensor.cpp
#ln -s $CURRENT_DIR/lib/Transforms/Tensor/CMakeLists.txt $TENSOR_DIR/CMakeLists.txt
#ln -s $CURRENT_DIR/lib/Transforms/Tensor/Tensor.exports $TENSOR_DIR/Tensor.exports

cp $CURRENT_DIR/lib/Analysis/* $LLVM_SRC/lib/Analysis/

# Copy Scalar Transformation files
cp $CURRENT_DIR/lib/Transforms/Scalar/LowerTensorIntrinsics.cpp $LLVM_SRC/lib/Transforms/Scalar/LowerTensorIntrinsics.cpp
#ln -s $CURRENT_DIR/lib/Transforms/Scalar/LowerTensorIntrinsics.cpp $LLVM_SRC/lib/Transforms/Scalar/LowerTensorIntrinsics.cpp

## Begin copying include headers
cp $CURRENT_DIR/include/llvm/Analysis/TensorProperties.h $LLVM_SRC/include/llvm/Analysis/TensorProperties.h
#ln -s $CURRENT_DIR/include/llvm/Analysis/TensorProperties.h $LLVM_SRC/include/llvm/Analysis/TensorProperties.h


cp $CURRENT_DIR/include/llvm/IR/TensorType.h $LLVM_SRC/include/llvm/IR/TensorType.h
#ln -s $CURRENT_DIR/include/llvm/IR/TensorType.h $LLVM_SRC/include/llvm/IR/TensorType.h

cp $CURRENT_DIR/include/llvm/Transforms/Scalar/LowerTensorIntrinsics.h $LLVM_SRC/include/llvm/Transforms/Scalar/LowerTensorIntrinsics.h
#ln -s $CURRENT_DIR/include/llvm/Transforms/Scalar/LowerTensorIntrinsics.h $LLVM_SRC/include/llvm/Transforms/Scalar/LowerTensorIntrinsics.h

cp $CURRENT_DIR/include/llvm/Transforms/Utils/TensorUtils.h $LLVM_SRC/include/llvm/Transforms/Utils/TensorUtils.h
#ln -s $CURRENT_DIR/include/llvm/Transforms/Utils/TensorUtils.h $LLVM_SRC/include/llvm/Transforms/Utils/TensorUtils.h








export LLVM_SRC_ROOT=$LLVM_SRC


cd $PATCHES_DIR
echo "Applying Patches"
/bin/bash llvm_patch.sh
echo "Patches have been applied"

cd $CURRENT_DIR
if [ ! -d $BUILD_DIR ]; then
  mkdir -p $BUILD_DIR
fi

if [ ! -d $INSTALL_DIR ]; then
  mkdir -p $INSTALL_DIR
fi

cd $BUILD_DIR
echo cmake $LLVM_SRC -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF -DLLVM_TARGETS_TO_BUILD="X86"  -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
cmake $LLVM_SRC -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF -DLLVM_TARGETS_TO_BUILD="X86"  -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
echo "Generated build files"

echo make -j12
make -j12

echo "Installation complete"
