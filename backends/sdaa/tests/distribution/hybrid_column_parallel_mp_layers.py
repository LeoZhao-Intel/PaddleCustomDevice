# Copyright (c) 2023 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import random
import unittest

import numpy as np

import paddle
import paddle.distributed as dist
from paddle.distributed import fleet


def set_random_seed(seed):
    """Set random seed for reproducability."""
    random.seed(seed)
    np.random.seed(seed)
    paddle.seed(seed)
    fleet.meta_parallel.model_parallel_random_seed(seed)


class ColumnLinearNet(paddle.nn.Layer):
    def __init__(self, input_size, output_size, global_dtype):
        super().__init__()
        self.parallel_linear = fleet.meta_parallel.ColumnParallelLinear(
            in_features=input_size,
            out_features=output_size,
            weight_attr=None,
            has_bias=True,
            gather_output=True,
            name="test_column_linear",
        )

    def forward(self, x):
        output = self.parallel_linear(x)
        return output


class SimpleMatmul(paddle.nn.Layer):
    def __init__(self, weight, output_size, global_dtype):
        super().__init__()
        self.weight = paddle.create_parameter(
            shape=weight.shape,
            dtype=global_dtype,
            attr=paddle.ParamAttr(initializer=paddle.nn.initializer.Assign(weight)),
        )
        self.bias = self.create_parameter(
            shape=[output_size],
            dtype=global_dtype,
            attr=paddle.ParamAttr(initializer=paddle.nn.initializer.Constant(0.0)),
        )

    def forward(self, x):
        output = paddle.matmul(x, self.weight) + self.bias
        return output


class TestDistTraning(unittest.TestCase):
    def setUp(self):
        strategy = fleet.DistributedStrategy()
        self.model_parallel_size = 2
        strategy.hybrid_configs = {
            "dp_degree": 1,
            "mp_degree": self.model_parallel_size,
            "pp_degree": 1,
        }
        fleet.init(is_collective=True, strategy=strategy)

    def test_column_parallel_layer(self):
        set_random_seed(1024)
        global_dtype = "float32"

        input_size_per_card = 17
        input_size = input_size_per_card * self.model_parallel_size
        output_size_per_card = 13
        output_size = output_size_per_card * self.model_parallel_size
        batch_size = 4

        model_a = ColumnLinearNet(input_size, output_size, global_dtype)

        # get w
        check_group = dist.new_group(list(range(self.model_parallel_size)))
        integral_w = []
        partial_w = model_a.parallel_linear.weight.clone().detach()
        paddle.distributed.all_gather(integral_w, partial_w, group=check_group)
        integral_w = paddle.concat(integral_w, axis=1)

        model_b = SimpleMatmul(integral_w, output_size, global_dtype)

        optimizer_a = paddle.optimizer.SGD(
            learning_rate=0.001, parameters=model_a.parameters()
        )
        optimizer_b = paddle.optimizer.SGD(
            learning_rate=0.001, parameters=model_b.parameters()
        )
        for idx in range(5):
            input = paddle.randn([batch_size, input_size], global_dtype)
            input.stop_gradient = True

            output_a = model_a(input)
            loss_a = output_a.mean()
            loss_a.backward()

            output_b = model_b(input)
            loss_b = output_b.mean()
            loss_b.backward()

            optimizer_a.step()
            optimizer_b.step()

            np.testing.assert_allclose(loss_a.numpy(), loss_b.numpy())


if __name__ == "__main__":
    unittest.main()
