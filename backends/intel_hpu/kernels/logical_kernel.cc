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
#include "kernels/funcs.h"
#include "kernels/hpu_operator.h"
#include "utils/utils.h"

namespace custom_kernel {
struct LogicalParams {
  char op[MAX_OPNAME_LEN];
};

class Logical : public HpuOperator {
 public:
  Logical() : HpuOperator("logical") {}
  void AddNode(ConvertTensors& ct, LogicalParams& params) {
    auto inputs = ct.GetTensors();
    auto outputs = ct.GetTensors(false);

    synSectionHandle section = nullptr;
    if (inputs[0].device_addr == outputs[0].device_addr) {
      section = createSection();
    }

    std::vector<synTensor> syn_inputs;
    for (size_t i = 0; i < inputs.size(); i++) {
      bool use_section = (i == 0 && section != nullptr);
      syn_inputs.push_back(createTensor(inputs[i].dims.size(),
                                        inputs[i].type,
                                        inputs[i].dims,
                                        true,
                                        inputs[i].name,
                                        use_section ? section : nullptr));
    }

    std::vector<synTensor> syn_outputs;
    for (size_t i = 0; i < outputs.size(); i++) {
      syn_outputs.push_back(createTensor(outputs[i].dims.size(),
                                         outputs[i].type,
                                         outputs[i].dims,
                                         true,
                                         outputs[i].name,
                                         section));
    }

    std::string guid =
        std::string(params.op) + "_" + SynDataTypeToStr(inputs[0].type);

    synStatus status = synNodeCreate(graphHandle_,
                                     syn_inputs.data(),
                                     syn_outputs.data(),
                                     syn_inputs.size(),
                                     syn_outputs.size(),
                                     nullptr,
                                     0,
                                     guid.c_str(),
                                     params.op,
                                     nullptr,
                                     nullptr);
    PD_CHECK(
        status == synSuccess, "[RUNTIME] synNodeCreate () failed = %d", status);
  }
};

template <typename T, typename Context>
void LogicalNotKernel(const Context& dev_ctx,
                      const phi::DenseTensor& x,
                      phi::DenseTensor* out) {
  dev_ctx.template Alloc<bool>(out);

  ConvertTensors ct;
  ct.Add(x);
  ct.Add(out, false);

  LogicalParams params = {};
  snprintf(params.op, MAX_OPNAME_LEN, "%s", "not");
  std::vector<DIMS> inputs_dims = ct.GetDims();
  OpCacheOperator op_info;
  op_info.prepareOpInfo<T, nullptr_t>("LogicalNotKernel", inputs_dims, nullptr);
  auto recipe = op_info.GetRecipe();

  if (recipe == nullptr) {
    Logical op;

    op.AddNode(ct, params);
    op.Compile();
    op_info.setOp(op);

    recipe = op_info.GetRecipe();
  }

  std::map<std::string, uint64_t> tensors = ct.GetDeviceAddr();
  RecipeRunner runner(recipe);
  runner.Run(reinterpret_cast<C_Stream>(dev_ctx.stream()), tensors);
}

template <typename T, typename Context>
void LogicalOrKernel(const Context& dev_ctx,
                     const phi::DenseTensor& x,
                     const phi::DenseTensor& y,
                     phi::DenseTensor* out) {
  dev_ctx.template Alloc<bool>(out);
  OpCacheOperator op_info;
  ConvertTensors ct;
  ct.Add(x);
  ct.Add(y);

  ct.Add(out, false);

  LogicalParams params = {};
  snprintf(params.op, MAX_OPNAME_LEN, "%s", "or");
  std::vector<DIMS> inputs_dims = ct.GetDims();
  std::string op_name =
      (x.data() == out->data()) ? "_LogicalOrKernel" : "LogicalOrKernel";
  op_info.prepareOpInfo<T, nullptr_t>(op_name, inputs_dims, nullptr);
  auto recipe = op_info.GetRecipe();

  if (recipe == nullptr) {
    Logical op;

    op.AddNode(ct, params);
    op.Compile();
    op_info.setOp(op);

    recipe = op_info.GetRecipe();
  }

  std::map<std::string, uint64_t> tensors = ct.GetDeviceAddr();
  RecipeRunner runner(recipe);
  runner.Run(reinterpret_cast<C_Stream>(dev_ctx.stream()), tensors);
}

template <typename T, typename Context>
void LogicalAndKernel(const Context& dev_ctx,
                      const phi::DenseTensor& x,
                      const phi::DenseTensor& y,
                      phi::DenseTensor* out) {
  dev_ctx.template Alloc<bool>(out);
  OpCacheOperator op_info;
  ConvertTensors ct;
  ct.Add(x);
  ct.Add(y);
  ct.Add(out, false);

  LogicalParams params = {};
  snprintf(params.op, MAX_OPNAME_LEN, "%s", "and");
  std::vector<DIMS> inputs_dims = ct.GetDims();
  std::string op_name =
      (x.data() == out->data()) ? "_LogicalAndKernel" : "LogicalAndKernel";
  op_info.prepareOpInfo<T, nullptr_t>(op_name, inputs_dims, nullptr);
  auto recipe = op_info.GetRecipe();

  if (recipe == nullptr) {
    Logical op;

    op.AddNode(ct, params);
    op.Compile();
    op_info.setOp(op);

    recipe = op_info.GetRecipe();
  }

  std::map<std::string, uint64_t> tensors = ct.GetDeviceAddr();
  RecipeRunner runner(recipe);
  runner.Run(reinterpret_cast<C_Stream>(dev_ctx.stream()), tensors);
}

template <typename T, typename Context>
void LogicalXorKernel(const Context& dev_ctx,
                      const phi::DenseTensor& x,
                      const phi::DenseTensor& y,
                      phi::DenseTensor* out) {
  dev_ctx.template Alloc<bool>(out);
  OpCacheOperator op_info;
  ConvertTensors ct;
  ct.Add(x);
  ct.Add(y);
  ct.Add(out, false);

  LogicalParams params = {};
  snprintf(params.op, MAX_OPNAME_LEN, "%s", "xor");
  std::vector<DIMS> inputs_dims = ct.GetDims();
  std::string op_name =
      (x.data() == out->data()) ? "_LogicalXorKernel" : "LogicalXorKernel";
  op_info.prepareOpInfo<T, nullptr_t>(op_name, inputs_dims, nullptr);
  auto recipe = op_info.GetRecipe();

  if (recipe == nullptr) {
    Logical op;

    op.AddNode(ct, params);
    op.Compile();
    op_info.setOp(op);

    recipe = op_info.GetRecipe();
  }

  std::map<std::string, uint64_t> tensors = ct.GetDeviceAddr();
  RecipeRunner runner(recipe);
  runner.Run(reinterpret_cast<C_Stream>(dev_ctx.stream()), tensors);
}

}  // namespace custom_kernel

PD_REGISTER_PLUGIN_KERNEL(logical_not,
                          intel_hpu,
                          ALL_LAYOUT,
                          custom_kernel::LogicalNotKernel,
                          bool,
                          int8_t,
                          uint8_t,
                          phi::dtype::float16) {
  kernel->OutputAt(0).SetDataType(phi::DataType::BOOL);
}

PD_REGISTER_PLUGIN_KERNEL(logical_or,
                          intel_hpu,
                          ALL_LAYOUT,
                          custom_kernel::LogicalOrKernel,
                          bool,
                          int8_t,
                          uint8_t,
                          float,
                          phi::dtype::float16,
                          phi::dtype::bfloat16) {
  kernel->OutputAt(0).SetDataType(phi::DataType::BOOL);
}

PD_REGISTER_PLUGIN_KERNEL(logical_and,
                          intel_hpu,
                          ALL_LAYOUT,
                          custom_kernel::LogicalAndKernel,
                          bool,
                          int8_t,
                          uint8_t,
                          float,
                          phi::dtype::float16,
                          phi::dtype::bfloat16) {
  kernel->OutputAt(0).SetDataType(phi::DataType::BOOL);
}

PD_REGISTER_PLUGIN_KERNEL(logical_xor,
                          intel_hpu,
                          ALL_LAYOUT,
                          custom_kernel::LogicalXorKernel,
                          bool,
                          int8_t,
                          uint8_t,
                          float,
                          phi::dtype::float16,
                          phi::dtype::bfloat16) {
  kernel->OutputAt(0).SetDataType(phi::DataType::BOOL);
}
