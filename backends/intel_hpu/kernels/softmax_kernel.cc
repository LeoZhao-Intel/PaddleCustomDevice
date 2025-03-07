// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "habanalabs/perf_lib_layer_params.h"
#include "habanalabs/synapse_api.h"
#include "habanalabs/synapse_common_types.h"
#include "kernels/funcs.h"
#include "kernels/hpu_operator.h"
#include "utils/utils.h"

namespace custom_kernel {

class SoftmaxOperator : public HpuOperator {
 public:
  SoftmaxOperator(std::string guid_prefix, std::string node_name)
      : HpuOperator(guid_prefix), pName_(node_name) {}
  void AddNode(ConvertTensors& ct,
               synDataType datatype,
               ns_Softmax::Params params) {
    auto ins = ct.GetTensors();
    auto outs = ct.GetTensors(false);
    assert(ins.size() == 1 && "input size should be 1");
    assert(outs.size() == 1 && "output size should be 1");

    synSectionHandle section = nullptr;
    if (ins[0].device_addr == outs[0].device_addr) {
      section = createSection();
    }

    synTensor inputs[ins.size()] = {createTensor(
        ins[0].dims.size(), datatype, ins[0].dims, true, "input", section)};
    synTensor outputs[outs.size()] = {createTensor(
        outs[0].dims.size(), datatype, outs[0].dims, true, "output", section)};

    synStatus status = synNodeCreate(graphHandle_,
                                     inputs,
                                     outputs,
                                     ins.size(),
                                     outs.size(),
                                     &params,
                                     sizeof(params),
                                     guid_.c_str(),
                                     pName_.c_str(),
                                     nullptr,
                                     nullptr);
    PD_CHECK(
        status == synSuccess, "[RUNTIME] synNodeCreate () failed = %d", status);
  }
  std::string pName_;
};

template <typename T, typename Context>
void SoftmaxKernel(const Context& dev_ctx,
                   const phi::DenseTensor& x,
                   int axis,
                   phi::DenseTensor* out) {
  const int rank = x.dims().size();
  const int calc_axis = custom_kernel::CanonicalAxis(axis, rank);

  // allocate memory on device.
  dev_ctx.template Alloc<T>(out);
  if (out->numel() == 0) {
    return;
  }

  ConvertTensors ct;
  ct.Add(x);
  ct.Add(out, false);

  std::vector<int64_t> inputs_dim = phi::vectorize<int64_t>(x.dims());
  std::vector<int64_t> outputs_dim = phi::vectorize<int64_t>(out->dims());
  ns_Softmax::Params params{static_cast<int>(inputs_dim.size()) - 1 -
                            calc_axis};

  OpCacheOperator op_info;
  op_info.prepareOpInfo<T, ns_Softmax::Params>(
      "softmax_fwd", {inputs_dim}, &params);

  auto recipe = op_info.GetRecipe();
  if (recipe == nullptr) {
    // compile
    std::string op_node_name =
        (x.data() == out->data()) ? "_softmax_op" : "softmax_op";
    SoftmaxOperator op(op_info.guid_, op_node_name);
    op.AddNode(ct, op_info.datatype_, params);

    op.Compile();
    op_info.setOp(op);
    recipe = op_info.GetRecipe();
  }

  // runtime
  std::map<std::string, uint64_t> tensors;
  tensors["input"] = reinterpret_cast<uint64_t>(x.data<T>());
  tensors["output"] = reinterpret_cast<uint64_t>(out->data<T>());

  RecipeRunner runner(recipe);
  runner.Run(reinterpret_cast<C_Stream>(dev_ctx.stream()), tensors);
}

}  // namespace custom_kernel

PD_REGISTER_PLUGIN_KERNEL(softmax,
                          intel_hpu,
                          ALL_LAYOUT,
                          custom_kernel::SoftmaxKernel,
                          float,
                          phi::dtype::float16,
                          phi::dtype::bfloat16) {}
