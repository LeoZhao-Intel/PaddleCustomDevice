// Copyright (c) 2024 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "kernels/funcs/reduce_op.h"

namespace custom_kernel {

template <typename T, typename Context>
void AllKernel(const Context& dev_ctx,
               const phi::DenseTensor& x,
               const std::vector<int64_t>& dims,
               bool keep_dim,
               phi::DenseTensor* out) {
  dev_ctx.template Alloc<T>(out);
  bool reduce_all =
      dims.size() == 0 || static_cast<int>(dims.size()) == x.dims().size();
  MLUReduceOp<T>(dev_ctx, x, dims, keep_dim, reduce_all, "reduce_all", out);
}

}  // namespace custom_kernel

PD_REGISTER_PLUGIN_KERNEL(
    all, mlu, ALL_LAYOUT, custom_kernel::AllKernel, float, int, int64_t, bool) {
}
