
#include "tensor.h"

_tensor_t foo(int condition, _tensor_t tensor1) {
  _shape_t shape = {2, 2, 2, 2};
  _padding_t padding =  {0, 0, 0, 1};
  _layout_t layout =  {0, 1, 2, 3};

   _token_t tensor1_token = tensor_typeinfo(tensor1, shape, layout, padding);
  // Must be removed by DCE!  
  _tensor_t tensor2 = tensor_relu(tensor1_token);
  _token_t tensor2_token = tensor_typeinfo(tensor2, shape, layout, padding);
  if(condition) {
      tensor2_token = tensor_typeinfo(tensor2, shape, layout, padding);
  }
  //_tensor_t tensor3 = tensor_relu(tensor2_token);
  //_token_t tensor3_token = tensor_typeinfo(tensor3, shape, layout, padding);
   return tensor2;
}
