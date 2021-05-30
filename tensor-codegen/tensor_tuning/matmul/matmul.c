#include <stddef.h>
#include <string.h>
#include <stdio.h>

#ifndef M
#define M 200
#endif
#ifndef K
#define K 200
#endif
#ifndef N
#define N 200
#endif
#ifndef NITER
#define NITER 200
#endif

// Define vector types
#define INTVEC(N) int __attribute__((__vector_size__(N * sizeof(int)), __aligned__(16)))

typedef INTVEC(4) _shape_t;
typedef INTVEC(4) _layout_t;
typedef INTVEC(4) _padding_t;
typedef int _token_t;

typedef INTVEC(M*K) _tensor_t1;
typedef INTVEC(K*N) _tensor_t2;
typedef INTVEC(M*N) _tensor_t3;

_token_t tensor_typeinfo1(_tensor_t1, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo2(_tensor_t2, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo3(_tensor_t3, _shape_t, _layout_t, _padding_t);
_tensor_t3 tensor_matmul(_token_t, _token_t);

int _g;

int main(void) {
  _tensor_t1 tensor1;
  _tensor_t2 tensor2;
  _tensor_t3 tensor3;
  memset(&tensor1, 1, sizeof(tensor1));
  memset(&tensor2, 1, sizeof(tensor2));

  for (size_t i = 0; i < NITER; i++) {
    _shape_t shape1 = {1, 1, M, K};
    _shape_t shape2 = {1, 1, K, N};
    _shape_t shape3 = {1, 1, M, N};
    _layout_t layout = {0, 1, 2, 3};
    _layout_t layout1 = {0, 1, 2, 3};
    _padding_t padding = {0, 0, 0, 0};

    _token_t tensor1_token = tensor_typeinfo1(tensor1, shape1, layout, padding);
    _token_t tensor2_token = tensor_typeinfo2(tensor2, shape2, layout, padding);
    tensor3 = tensor_matmul(tensor1_token, tensor2_token);
    _token_t tensor3_token =
        tensor_typeinfo3(tensor3, shape3, layout1, padding);
  }
}
