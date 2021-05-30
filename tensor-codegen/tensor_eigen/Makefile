LLVM_ROOT=..
BUILD=$(LLVM_ROOT)/build
BIN=$(BUILD)/bin
OPTS=`pkg-config --cflags eigen3` -O3 -march=native -fopenmp

BENCH_BIN = $(BUILD)/tensor_eigen/matmul_bench

$(BENCH_BIN): matmul_bench.cpp
	mkdir $(BUILD)/tensor_eigen -p
	clang++ $(OPTS) matmul_bench.cpp -o $(BUILD)/tensor_eigen/matmul_bench

run: $(BENCH_BIN)
	$(BENCH_BIN) $(THREADS)

clean:
	rm $(BENCH_BIN)
