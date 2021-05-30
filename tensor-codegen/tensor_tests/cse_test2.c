
#include "tensor.h"


//_tensor_t
void foo(int condition, _tensor_t tensor1, _tensor_t tensor2) {
  _shape_t shape = {2, 2, 4, 4};
  _layout_t layout =  {0, 1, 2, 3};
  _padding_t padding =  {0, 0, 0, 0};

  _token_t tensor1_token =  tensor_typeinfo(tensor1, shape, layout, padding);
  _token_t tensor2_token =  tensor_typeinfo(tensor2, shape, layout, padding);


  _tensor_t tensor3 = tensor_relu(tensor1_token);  
  _token_t tensor3_token =  tensor_typeinfo(tensor3, shape, layout, padding);
  if(condition) {
     // Must be removed by CSE!
     tensor3 = tensor_relu(tensor1_token); 
     tensor3_token = tensor_typeinfo(tensor3, shape, layout, padding);
  }
   _tensor_t tensor4 = tensor_matmul(tensor3_token, tensor1_token);
   //_tensor_t tensor4 = tensor3 + tensor3;
  _token_t tensor4_token =  tensor_typeinfo(tensor4, shape, layout, padding);

  return;
}
