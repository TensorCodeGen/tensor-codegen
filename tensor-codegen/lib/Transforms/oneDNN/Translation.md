# LLVM -> oneDNN Translation

## Tensor Operations
Proposed Instrinsic | oneDNN Instrinsic(s) | Conversion Process
--- | --- | ---
llvm.tensor.matmul | blah | blah
llvm.tensor.mma | blah | blah
llvm.vector.reduction.mac | blah | blah
llvm.vector.reduction.mac.sat | blah | blah
llvm.vector.mac | blah | blah
llvm.tensor.tanh | <ol><li>tanh_desc = eltwise_forward::desc(prop_kind::forward, algorithm::eltwise_tanh, \<engine_descriptor>, 0.0f)</li><li>tanh_impl = eltwise::forward::primitive_desc(tanh_desc, \<engine>)</li><li>tanh = eltwise_forward(tanh_impl)</li></ol> | Same as below.
llvm.tensor.relu | <ol><li>relu_desc = eltwise_forward::desc(prop_kind::forward, algorithm::eltwise_relu, m_gpu.get_desc(), 0.0f)</li><li>relu_impl = eltwise::forward::primitive_desc(relu_desc, \<engine>)</li><li>relu = eltwise_forward(relu_impl);</li></ol> | First, create ReLU operation descriptor. Second, create ReLU primitive descripton to describe implementation. Lastly, create operation to be executed.
llvm.tensor.broadcast | blah | blah
llvm.vector.splat | blah | blah
blah | blah | blah
blah | blah | blah
blah | blah | blah
blah | blah | blah
blah | blah | blah
blah | blah | blah


## Memory Operations

Proposed Instrinsic | oneDNN Instrinsic(s) | Conversion Process
--- | --- | ---
llvm.tensor.load | blah | blah
llvm.tensor.store | blah | blah
blah | blah | blah

## Reduction Operations


