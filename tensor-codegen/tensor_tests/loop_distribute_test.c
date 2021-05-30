
#include "tensor.h"

_tensor_t foo(_tensor_t tensor1, _tensor_t tensor2, int *c, int *b) { 
  _shape_t shape = {2, 2, 2, 2};
  _padding_t padding =  {0, 0, 0, 1};
  _layout_t layout =  {0, 1, 2, 3};

  _token_t tensor1_token = tensor_typeinfo(tensor1, shape, layout, padding);
  _token_t tensor2_token = tensor_typeinfo(tensor2, shape, layout, padding);

  int a[100]; 
  _tensor_t tensor4, tensor5;   
  _token_t tensor4_token, tensor5_token;
  for (int i = 2; i < 100; ++i) {
     _tensor_t tensor6 = {0, 0, 0, i};
     _token_t tensor6_token = tensor_typeinfo(tensor6, shape, layout, padding);
     tensor4 = tensor_relu(tensor6_token);
     tensor4_token= tensor_typeinfo(tensor4, shape, layout, padding);
     tensor5 = tensor_relu(tensor4_token);
     tensor5_token = tensor_typeinfo(tensor5, shape, layout, padding);
     c[i] = b[i] * c[i - 2]; 
  }

   return tensor5;
}
