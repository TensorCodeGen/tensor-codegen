
#ifndef __TENSOR__H__
#define __TENSOR__H__

// Define vector types
typedef int _tensor_t  __attribute__((__vector_size__(16)));//, __aligned__(4)));

typedef int _tensor_t10000  __attribute__((__vector_size__(40000)));//, __aligned__(4)));
//typedef int _tensor_t  __attribute__((__vector_size__(32), __aligned__(4)));
//typedef int _tensor_t  __attribute__((__vector_size__(64), __aligned__(4)));
//typedef int _tensor_t  __attribute__((__vector_size__(128), __aligned__(4)));

typedef int _shape_t   __attribute__((__vector_size__(16), __aligned__(4)));
typedef int _layout_t  __attribute__((__vector_size__(16), __aligned__(4)));
typedef int _padding_t __attribute__((__vector_size__(16), __aligned__(4)));
//typedef int _token_t __attribute__((__vector_size__(16), __aligned__(4)));
typedef int _token_t;

// Define the tensor functions.
_token_t tensor_typeinfo(_tensor_t, _shape_t, _layout_t, _padding_t);
_token_t tensor_typeinfo10000(_tensor_t10000, _shape_t, _layout_t, _padding_t);

_tensor_t tensor_relu(_token_t);
_tensor_t tensor_tanh(_token_t);
_tensor_t tensor_transpose(_token_t);
_tensor_t tensor_broadcast(_token_t, int);
_tensor_t tensor_matmul(_token_t, _token_t);
_tensor_t10000 tensor_matmul10000(_token_t, _token_t);

#endif
