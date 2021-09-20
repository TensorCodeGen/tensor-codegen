
CURRENT_DIR=`pwd`
INSTALL_DIR=`pwd`/install
BUILD_DIR=$CURRENT_DIR/build



LLVM_SRC=$CURRENT_DIR/../llvm
CLANG_SRC=$CURRENT_DIR/../clang
TENSOR_DIR=$LLVM_SRC/lib/Transforms/Tensor
PATCHES_DIR=llvm_patches


# Install options
NUM_THREADS=2
NUM_THREADS_INPUT=2


TARGET=all
TARGET_INPUT=all


echo "#############################################################################"
read -p "Number of threads: " NUM_THREADS_INPUT

if [[ $NUM_THREADS_INPUT == "" ]]; then
  echo "No input given. Using default: $NUM_THREADS"
elif ! [[ $NUM_THREADS_INPUT =~ ^[0-9]+$ ]]; then
  echo "Given input is not an integer. Using default: $NUM_THREADS"
elif [ ! $NUM_THREADS_INPUT -gt 0 ]; then
  echo "Given input is not greater than 0. Using default: $NUM_THREADS"
else
  NUM_THREADS=$NUM_THREADS_INPUT
fi

echo
echo
echo "Supports the following options: AArch64, AMDGPU, ARM, BPF, Hexagon, Mips, MSP430, NVPTX, PowerPC, Sparc, SystemZ, X86, XCore."
echo "If building for multiple targets, seperate options with semicolon:"
echo "e.g. X86;Hexagon;PowerPC"
read -p "Build target: " TARGET_INPUT
if [[ $TARGET_INPUT == "" ]]; then
  echo "No input given. Using default: $TARGET"
else
  TARGET=$TARGET_INPUT
fi
echo

if [ ! -d $LLVM_SRC/tools/clang ]; then
    echo "#############################################################################"
    echo "Copying clang over to llvm dir"
    cp -r $CLANG_SRC $LLVM_SRC/tools
    echo "Clang copy complete"
    echo "#############################################################################"
fi

if [ ! -d $TENSOR_DIR ]; then
    echo "#############################################################################"
    echo "Creating Tensor directory"
    # Copy Tensor Transformation files
    #mkdir -p $TENSOR_DIR
    ln -s $CURRENT_DIR/lib/Transforms/Tensor/ $LLVM_SRC/lib/Transforms
    ln -s $CURRENT_DIR/lib/Analysis/TensorProperties.cpp $LLVM_SRC/lib/Analysis/TensorProperties.cpp
    ln -s $CURRENT_DIR/lib/Transforms/Scalar/LowerTensorIntrinsics.cpp $LLVM_SRC/lib/Transforms/Scalar/LowerTensorIntrinsics.cpp
    ln -s $CURRENT_DIR/include/llvm/Analysis/TensorProperties.h $LLVM_SRC/include/llvm/Analysis/TensorProperties.h
    ln -s $CURRENT_DIR/include/llvm/IR/TensorType.h $LLVM_SRC/include/llvm/IR/TensorType.h
    ln -s $CURRENT_DIR/include/llvm/Transforms/Scalar/LowerTensorIntrinsics.h $LLVM_SRC/include/llvm/Transforms/Scalar/LowerTensorIntrinsics.h
    echo "#############################################################################"
else
    echo "#############################################################################"
    echo "Tensor directory already exists"
    echo "#############################################################################"
fi



echo "###################################################"
echo "Running with the following options:"
echo Threads: $NUM_THREADS
echo Targets: $TARGET
echo "###################################################"
#read -p "Press enter to continue"




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
echo cmake $LLVM_SRC -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF -DLLVM_TARGETS_TO_BUILD=\"$TARGET\"  -DCMAKE_BUILD_TYPE=\"Release\"
cmake $LLVM_SRC -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF -DLLVM_TARGETS_TO_BUILD="$TARGET" -DCMAKE_BUILD_TYPE="Release"
echo "Generated build files"

echo make -j$NUM_THREADS
make -j$NUM_THREADS

echo "Installation complete"
