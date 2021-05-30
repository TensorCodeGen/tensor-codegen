
#include "tensor.h"

void foo(int condition, _tensor_t tensor1) {
  _shape_t shape = {2, 2, 2, 2};
  _padding_t padding =  {0, 0, 0, 1};
  _layout_t layout =  {0, 1, 2, 3};

   _token_t tensor1_token = tensor_typeinfo(tensor1, shape, layout, padding);

  // Must be removed by DCE!  
  _tensor_t tensor3 = tensor_relu(tensor1_token);
  _token_t t3_token = tensor_typeinfo(tensor3, shape, layout, padding);

}
