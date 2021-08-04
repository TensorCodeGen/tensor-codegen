LLVM_ROOT=..
BIN=$(LLVM_ROOT)/build/bin
BUILD=$(LLVM_ROOT)/build


TESTS=$(SOURCES:%.c)

all: licm dse cse dce adce

licm:
	$(BIN)/clang -S -emit-llvm licm_test.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg  licm_test.ll -S -o licm.mem2reg.ll
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -licm  licm_test.ll -S -o licm_test.licm.ll
	$(BIN)/clang -S -emit-llvm licm_test2.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -licm  licm_test2.ll -S -o licm_test2.licm.ll
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -load $(BUILD)/lib/LLVMTensorProperties.so -mem2reg -tensor -tensor-analysis licm_test2.ll -S -o licm_test2.tensor.ll

### DSE needs to be fixed! (Does DSE make sense after mem2reg?)
dse:
	$(BIN)/clang -S -emit-llvm dse_test.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor  -dse  dse_test.ll -S -o dse_test.dse.ll
	$(BIN)/clang -S -emit-llvm dse_test2.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor  -dse  dse_test2.ll -S -o dse_test2.dse.ll
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -load $(BUILD)/lib/LLVMTensorProperties.so -mem2reg -tensor -tensor-analysis dse_test2.ll -S -o dse_test2.tensor.ll

### CSE is working as expected!
cse:
	$(BIN)/clang -S -emit-llvm cse_test.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -early-cse  cse_test.ll -S -o cse_test.cse.ll
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -load $(BUILD)/lib/LLVMTensorProperties.so -mem2reg -tensor -tensor-analysis cse_test.ll -S -o cse_test.tensor.ll
	$(BIN)/clang -S -emit-llvm cse_test2.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -early-cse  cse_test2.ll -S -o cse_test2.cse.ll
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -load $(BUILD)/lib/LLVMTensorProperties.so -mem2reg -tensor -tensor-analysis cse_test2.ll -S -o cse_test2.tensor.ll

### DCE is working as expected!
dce:
	$(BIN)/clang -S -emit-llvm dce_test.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg  dce_test.ll -S -o dce.mem2reg.ll
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -dce  dce_test.ll -S -o dce_test.dce.ll

### ADCE is working as expected!
adce:
	$(BIN)/clang -S -emit-llvm adce_test.c -O0 -Xclang -disable-O0-optnone
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -adce  adce_test.ll -S -o adce_test.adce.ll


lower:
	$(BIN)/clang -S -emit-llvm test.c -O0 -Xclang -disable-O0-optnone
	#$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -tensor-analysis matmul.ll -S -o matmul.buffer.ll
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -tensor-analysis -lower-tensor -dse -mem2reg -licm -adce test.ll -S -o test.lowered.ll

lower_raw:
	$(BIN)/opt -load $(BUILD)/lib/LLVMTensor.so -mem2reg -tensor -tensor-analysis test.ll -S -o test.lowered.ll

clean:
	rm -rf *.ll
