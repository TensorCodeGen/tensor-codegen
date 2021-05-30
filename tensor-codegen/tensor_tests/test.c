
#include "tensor.h"
#include <stdio.h>
/*
void print(_tensor_t10000 tensor) {
  printf("start\n");
   for(unsigned i = 0; i < 2; i++) {
    printf("printing tensor: %d\n", tensor[i]);
  }
  printf("end\n");
}
*/
_tensor_t10000  foo(_tensor_t10000 tensor1, _tensor_t10000 tensor2) {
  _shape_t shape1 = {1, 1, 100, 100};
  _shape_t shape2 = {1, 1, 100, 100};
 _shape_t shape3 = {1, 1, 20, 20};
  //_shape_t shape3 = {1, 1, 2, 2};
  _shape_t shape4 = {1, 1, 100, 100};
  _layout_t layout =  {0, 1, 2, 3};
  _layout_t layout1 =  {0, 1, 3, 2};
  _padding_t padding =  {0, 0, 0, 0};

  _token_t tensor1_token =  tensor_typeinfo10000(tensor1, shape1, layout, padding);
  _token_t tensor2_token =  tensor_typeinfo10000(tensor2, shape2, layout, padding);

  //_tensor_t10000 tensor3 = tensor_transpose1000(tensor1_token); 
  //_token_t tensor3_token = tensor_typeinfo10000(tensor3, shape3, layout1, padding);

  _tensor_t10000 tensor4 = tensor_matmul10000(tensor1_token, tensor2_token);  
  _token_t tensor4_token = tensor_typeinfo10000(tensor4, shape4, layout, padding);

  return tensor4;
}


int main(void) {
  _tensor_t10000 tensor1 = {0};// = {5, 7, 8, 9};
  _tensor_t10000 tensor2 = {0};// = {1, 2, 3, 4,};
  _tensor_t10000 tensor3 = foo(tensor1, tensor2);  

  //unsigned output_size = 4;
  //for(unsigned i = 0; i < output_size; i++) {
  //  printf("output: %d\n", tensor3[i]);
 // }
  
  return 0;
}
