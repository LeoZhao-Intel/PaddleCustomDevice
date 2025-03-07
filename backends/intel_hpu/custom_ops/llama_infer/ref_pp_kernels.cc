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
#include <dlfcn.h>
#include <omp.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "paddle/extension.h"

inline bool is_in_end(const int64_t id, const int64_t* end_ids, int length) {
  bool flag = false;
  for (int i = 0; i < length; i++) {
    if (id == end_ids[i]) {
      return true;
    }
  }
  return flag;
}

void set_value_by_flags(const bool* stop_flags,
                        const int64_t* end_ids,
                        int64_t* topk_ids,
                        bool* stop_flags_out,
                        const int bs,
                        int end_length) {
#pragma omp parallel for num_threads(OMP_THREAD_NUM)
  for (int bi = 0; bi < bs; bi++) {
    topk_ids[bi] = stop_flags[bi] ? end_ids[0] : topk_ids[bi];
    if (is_in_end(topk_ids[bi], end_ids, end_length)) {
      stop_flags_out[bi] = true;
    }
  }
}

std::vector<paddle::Tensor> GetStopFlagsMulti(const paddle::Tensor& topk_ids,
                                              const paddle::Tensor& stop_flags,
                                              const paddle::Tensor& end_ids) {
  PD_CHECK(topk_ids.dtype() == paddle::DataType::INT64);
  PD_CHECK(stop_flags.dtype() == paddle::DataType::BOOL);

  std::vector<int64_t> shape = topk_ids.shape();
  int64_t bs_now = shape[0];
  int64_t end_length = end_ids.shape()[0];

  auto topk_ids_cpu = topk_ids.copy_to(paddle::CPUPlace(), true);
  auto stop_flags_cpu = stop_flags.copy_to(paddle::CPUPlace(), true);
  auto end_ids_cpu = end_ids.copy_to(paddle::CPUPlace(), true);
  auto stop_flags_out = stop_flags.copy_to(stop_flags_cpu.place(), true);

  set_value_by_flags(stop_flags_cpu.data<bool>(),
                     end_ids_cpu.data<int64_t>(),
                     topk_ids_cpu.data<int64_t>(),
                     stop_flags_out.data<bool>(),
                     bs_now,
                     end_length);

  return {topk_ids_cpu.copy_to(stop_flags.place(), true),
          stop_flags_out.copy_to(stop_flags.place(), true)};
}

std::vector<std::vector<int64_t>> GetStopFlagsMultiInferShape(
    const std::vector<int64_t>& topk_ids_shape,
    const std::vector<int64_t>& stop_flags_shape,
    const std::vector<int64_t>& end_ids_shape) {
  return {topk_ids_shape, stop_flags_shape};
}

std::vector<paddle::DataType> GetStopFlagsMultiInferDtype(
    const paddle::DataType& topk_ids_dtype,
    const paddle::DataType& stop_flags_dtype,
    const paddle::DataType& end_ids_dtype) {
  return {topk_ids_dtype, stop_flags_dtype};
}

PD_BUILD_OP(set_stop_value_multi_ends)
    .Inputs({"topk_ids", "stop_flags", "end_ids"})
    .Outputs({"topk_ids_out", "stop_flags_out"})
    .SetKernelFn(PD_KERNEL(GetStopFlagsMulti))
    .SetInferShapeFn(PD_INFER_SHAPE(GetStopFlagsMultiInferShape))
    .SetInferDtypeFn(PD_INFER_DTYPE(GetStopFlagsMultiInferDtype));

void set_value_by_flag_and_id(const bool* stop_flags,
                              int64_t* pre_ids_all,
                              const int64_t* pre_ids,
                              const int64_t* step_idx,
                              int bs,
                              int length) {
#pragma omp parallel for num_threads(OMP_THREAD_NUM)
  for (int bi = 0; bi < bs; bi++) {
    if (!stop_flags[bi]) {
      int64_t* pre_ids_all_now = pre_ids_all + bi * length;
      if (step_idx[bi] >= 0) {
        pre_ids_all_now[step_idx[bi]] = pre_ids[bi];
      }
    }
  }
}

std::vector<paddle::Tensor> SetValueByFlagsAndIdx(
    const paddle::Tensor& pre_ids_all,
    const paddle::Tensor& pre_ids_now,
    const paddle::Tensor& step_idx,
    const paddle::Tensor& stop_flags) {
  std::vector<int64_t> pre_ids_all_shape = pre_ids_all.shape();

  int bs = stop_flags.shape()[0];
  int length = pre_ids_all_shape[1];

  auto pre_ids_all_cpu = pre_ids_all.copy_to(paddle::CPUPlace(), true);
  auto pre_ids_now_cpu = pre_ids_now.copy_to(paddle::CPUPlace(), true);
  auto step_idx_cpu = step_idx.copy_to(paddle::CPUPlace(), true);
  auto stop_flags_cpu = stop_flags.copy_to(paddle::CPUPlace(), true);

  set_value_by_flag_and_id(
      stop_flags_cpu.data<bool>(),
      const_cast<int64_t*>(pre_ids_all_cpu.data<int64_t>()),
      pre_ids_now_cpu.data<int64_t>(),
      step_idx_cpu.data<int64_t>(),
      bs,
      length);
  pre_ids_all_cpu.copy_to(pre_ids_all.place(), true);
  return {stop_flags};
}

std::vector<std::vector<int64_t>> SetValueByFlagsAndIdxInferShape(
    const std::vector<int64_t>& pre_ids_all_shape,
    const std::vector<int64_t>& pre_ids_now_shape,
    const std::vector<int64_t>& step_idx_shape,
    const std::vector<int64_t>& stop_flags_shape) {
  return {stop_flags_shape};
}

std::vector<paddle::DataType> SetValueByFlagsAndIdxInferDtype(
    const paddle::DataType& pre_ids_all_dtype,
    const paddle::DataType& pre_ids_now_dtype,
    const paddle::DataType& step_idx_dtype,
    const paddle::DataType& stop_flags_dtype) {
  return {stop_flags_dtype};
}

PD_BUILD_OP(set_value_by_flags_and_idx)
    .Inputs({"pre_ids_all", "pre_ids_now", "step_idx", "stop_flags"})
    .Outputs({"stop_flags_out"})
    .SetKernelFn(PD_KERNEL(SetValueByFlagsAndIdx))
    .SetInferShapeFn(PD_INFER_SHAPE(SetValueByFlagsAndIdxInferShape))
    .SetInferDtypeFn(PD_INFER_DTYPE(SetValueByFlagsAndIdxInferDtype));

template <typename T>
void min_length_logits_process(T* logits,
                               const int64_t* cur_len,
                               const int64_t* min_len,
                               const int64_t* eos_token_id,
                               const int64_t bs,
                               const int64_t length,
                               const int64_t end_length) {
#pragma omp parallel for num_threads(OMP_THREAD_NUM)
  for (int bi = 0; bi < bs; ++bi) {
    if (cur_len[bi] < 0) {
      continue;
    }
    if (cur_len[bi] < min_len[bi]) {
      for (int i = 0; i < end_length; ++i) {
        logits[bi * length + eos_token_id[i]] = -1e10;
      }
    }
  }
}

void update_repeat_times(const int64_t* pre_ids,
                         const int64_t* cur_len,
                         int* repeat_times,
                         const int64_t bs,
                         const int64_t length,
                         const int64_t length_id) {
#pragma omp parallel for num_threads(OMP_THREAD_NUM)
  for (int bi = 0; bi < bs; ++bi) {
    if (cur_len[bi] < 0) {
      continue;
    }
    const int64_t* pre_ids_now = pre_ids + bi * length_id;
    int* repeat_times_now = repeat_times + bi * length;
    for (int i = 0; i < length_id; i++) {
      int64_t id = pre_ids_now[i];
      if (id < 0) {
        break;
      }
      repeat_times_now[id] += 1;
    }
  }
}

template <typename T>
void update_value_by_repeat_times(const int* repeat_times,
                                  const T* penalty_scores,
                                  const T* frequency_score,
                                  const T* presence_score,
                                  T* logits,
                                  const int64_t bs,
                                  const int64_t length) {
#pragma omp parallel for num_threads(OMP_THREAD_NUM)
  for (int bi = 0; bi < bs; ++bi) {
    T* logits_now = logits + bi * length;
    const int* repeat_times_now = repeat_times + bi * length;
    float alpha = static_cast<float>(penalty_scores[bi]);
    float beta = static_cast<float>(frequency_score[bi]);
    float gamma = static_cast<float>(presence_score[bi]);
    for (int i = 0; i < length; ++i) {
      int times = repeat_times_now[i];
      if (times == 0) continue;
      float logit_now = static_cast<float>(logits_now[i]);
      logit_now = logit_now < 0 ? logit_now * alpha : logit_now / alpha;
      logits_now[i] = static_cast<T>(logit_now - times * beta - gamma);
    }
  }
}
template <paddle::DataType D>
std::vector<paddle::Tensor> token_penalty_multi_scores_kernel(
    const paddle::Tensor& pre_ids,
    const paddle::Tensor& logits,
    const paddle::Tensor& penalty_scores,
    const paddle::Tensor& frequency_score,
    const paddle::Tensor& presence_score,
    const paddle::Tensor& cur_len,
    const paddle::Tensor& min_len,
    const paddle::Tensor& eos_token_id) {
  std::vector<int64_t> shape = logits.shape();
  auto repeat_times =
      paddle::full(shape, 0, paddle::DataType::INT32, pre_ids.place());
  int64_t bs = shape[0];
  int64_t length = shape[1];
  int64_t length_id = pre_ids.shape()[1];
  auto logits_out = logits.copy_to(logits.place(), false);
  int64_t end_length = eos_token_id.shape()[0];

  min_length_logits_process(const_cast<float*>(logits_out.data<float>()),
                            cur_len.data<int64_t>(),
                            min_len.data<int64_t>(),
                            eos_token_id.data<int64_t>(),
                            bs,
                            length,
                            end_length);
  update_repeat_times(pre_ids.data<int64_t>(),
                      cur_len.data<int64_t>(),
                      repeat_times.data<int>(),
                      bs,
                      length,
                      length_id);
  update_value_by_repeat_times(
      repeat_times.data<int>(),
      const_cast<float*>(penalty_scores.data<float>()),
      const_cast<float*>(frequency_score.data<float>()),
      const_cast<float*>(presence_score.data<float>()),
      const_cast<float*>(logits_out.data<float>()),
      bs,
      length);
  return {logits_out};
}

std::vector<paddle::Tensor> TokenPenaltyMultiScores(
    const paddle::Tensor& pre_ids,
    const paddle::Tensor& logits,
    const paddle::Tensor& penalty_scores,
    const paddle::Tensor& frequency_scores,
    const paddle::Tensor& presence_scores,
    const paddle::Tensor& cur_len,
    const paddle::Tensor& min_len,
    const paddle::Tensor& eos_token_id) {
  auto pre_ids_cpu = pre_ids.copy_to(paddle::CPUPlace(), true);
  auto logits_cpu = logits.copy_to(paddle::CPUPlace(), true);
  auto penalty_scores_cpu = penalty_scores.copy_to(paddle::CPUPlace(), true);
  auto frequency_scores_cpu =
      frequency_scores.copy_to(paddle::CPUPlace(), true);
  auto presence_scores_cpu = presence_scores.copy_to(paddle::CPUPlace(), true);
  auto cur_len_cpu = cur_len.copy_to(paddle::CPUPlace(), true);
  auto min_len_cpu = min_len.copy_to(paddle::CPUPlace(), true);
  auto eos_token_id_cpu = eos_token_id.copy_to(paddle::CPUPlace(), true);
  switch (logits_cpu.type()) {
    case paddle::DataType::FLOAT32: {
      return {token_penalty_multi_scores_kernel<paddle::DataType::FLOAT32>(
                  pre_ids_cpu,
                  logits_cpu,
                  penalty_scores_cpu,
                  frequency_scores_cpu,
                  presence_scores_cpu,
                  cur_len_cpu,
                  min_len_cpu,
                  eos_token_id_cpu)[0]
                  .copy_to(logits.place(), true)};
    }
    default: {
      PD_THROW(
          "NOT supported data type. "
          "Only float32 are supported. ");
      break;
    }
  }
}

std::vector<std::vector<int64_t>> TokenPenaltyMultiScoresInferShape(
    const std::vector<int64_t>& pre_ids_shape,
    const std::vector<int64_t>& logits_shape,
    const std::vector<int64_t>& penalty_scores_shape,
    const std::vector<int64_t>& frequency_scores_shape,
    const std::vector<int64_t>& presence_scores_shape,
    const std::vector<int64_t>& cur_len_shape,
    const std::vector<int64_t>& min_len_shape,
    const std::vector<int64_t>& eos_token_id_shape) {
  return {logits_shape};
}

std::vector<paddle::DataType> TokenPenaltyMultiScoresInferDtype(
    const paddle::DataType& pre_ids_dtype,
    const paddle::DataType& logits_dtype,
    const paddle::DataType& penalty_scores_dtype,
    const paddle::DataType& frequency_scores_dtype,
    const paddle::DataType& presence_scores_dtype,
    const paddle::DataType& cur_len_dtype,
    const paddle::DataType& min_len_dtype,
    const paddle::DataType& eos_token_id_dtype) {
  return {logits_dtype};
}
PD_BUILD_OP(get_token_penalty_multi_scores)
    .Inputs({"pre_ids",
             "logits",
             "penalty_scores",
             "frequency_scores",
             "presence_scores",
             "cur_len",
             "min_len",
             "eos_token_id"})
    .Outputs({"logits_out"})
    .SetKernelFn(PD_KERNEL(TokenPenaltyMultiScores))
    .SetInferShapeFn(PD_INFER_SHAPE(TokenPenaltyMultiScoresInferShape))
    .SetInferDtypeFn(PD_INFER_DTYPE(TokenPenaltyMultiScoresInferDtype));

constexpr char kSEP = '/';

std::string DirName(const std::string& filepath) {
  auto pos = filepath.rfind(kSEP);
  if (pos == std::string::npos) {
    return "";
  }
  return filepath.substr(0, pos);
}

bool FileExists(const std::string& filepath) {
  struct stat buffer;
  return (stat(filepath.c_str(), &buffer) == 0);
}

void MkDir(const char* path) {
  std::string path_error(path);
  path_error += " mkdir failed!";
  if (mkdir(path, 0755)) {
    if (errno != EEXIST) {
      throw std::runtime_error(path_error);
    }
  }
}

void MkDirRecursively(const char* fullpath) {
  if (*fullpath == '\0') return;  // empty string
  if (FileExists(fullpath)) return;
  MkDirRecursively(DirName(fullpath).c_str());
  MkDir(fullpath);
}

template <typename data_t>
void saveToFile(std::ostream& os,
                const void* x_data,
                std::vector<int64_t> shape,
                int64_t x_numel,
                const char type_id) {
  // 1.type
  os.write(reinterpret_cast<const char*>(&type_id), sizeof(type_id));
  // 2.data
  uint64_t size = x_numel * sizeof(data_t);
  os.write(static_cast<const char*>(x_data),
           static_cast<std::streamsize>(size));
}

template <typename data_t>
void save_with_output_kernel(const paddle::Tensor& x,
                             const paddle::Tensor& batch_idx,
                             const paddle::Tensor& step_idx,
                             std::string file_path,
                             int64_t rank_id,
                             char type_id) {
  std::vector<int64_t> x_shape = x.shape();

  if (rank_id >= 0) {
    file_path += "_rank_" + std::to_string(rank_id);
  }

  int batch_idx_data = -1, step_idx_data = -1;

  if (batch_idx.is_custom_device()) {
    paddle::Tensor batch_idx_cpu =
        batch_idx.copy_to<int32_t>(paddle::CPUPlace());
    batch_idx_data = batch_idx_cpu.data<int32_t>()[0];
  } else {
    batch_idx_data = batch_idx.data<int32_t>()[0];
  }
  if (step_idx.is_custom_device()) {
    paddle::Tensor step_idx_cpu = step_idx.copy_to<int64_t>(paddle::CPUPlace());
    step_idx_data = step_idx_cpu.data<int64_t>()[0];
  } else {
    step_idx_data = step_idx.data<int64_t>()[0];
  }
  auto x_data = x.data<data_t>();

  if (batch_idx_data >= 0) {
    file_path += "_batch_" + std::to_string(batch_idx_data);
  }
  if (step_idx_data >= 0) {
    file_path += "_step_" + std::to_string(step_idx_data);
  }
  MkDirRecursively(DirName(file_path).c_str());
  std::ofstream fout(file_path, std::ios::binary);
  fout.write("0", 1);
  saveToFile<data_t>(fout, x_data, x_shape, x.numel(), type_id);
  fout.seekp(std::ios::beg);
  fout.write("1", 1);
  fout.close();
}

std::vector<paddle::Tensor> SaveWithOutputForward(
    const paddle::Tensor& x,
    const paddle::Tensor& batch_idx,
    const paddle::Tensor& step_idx,
    std::string file_path,
    int64_t rank_id) {
  auto out = x.copy_to(paddle::CPUPlace(), false);
  switch (x.type()) {
    case paddle::DataType::FLOAT32:
      save_with_output_kernel<float>(
          out, batch_idx, step_idx, file_path, rank_id, '0');
      break;
    case paddle::DataType::INT64:
      save_with_output_kernel<int64_t>(
          out, batch_idx, step_idx, file_path, rank_id, '1');
      break;
    case paddle::DataType::INT32:
      save_with_output_kernel<int32_t>(
          out, batch_idx, step_idx, file_path, rank_id, '2');
      break;
    default:
      PD_THROW(
          "function SaveWithOutputForward is not implemented for data type");
  }
  return {out};
}

std::vector<std::vector<int64_t>> SaveWithOutputInferShape(
    const std::vector<int64_t>& x_shape,
    const std::vector<int64_t>& batch_idx_shape,
    const std::vector<int64_t>& step_idx_shape) {
  return {x_shape};
}

std::vector<paddle::DataType> SaveWithOutputInferDtype(
    const paddle::DataType& x_dtype,
    const paddle::DataType& batch_idx_dtype,
    const paddle::DataType& step_idx_dtype) {
  return {x_dtype};
}

PD_BUILD_OP(save_with_output)
    .Inputs({"x", "batch_idx", "step_idx"})
    .Attrs({"file_path: std::string", "rank_id: int64_t"})
    .Outputs({"out"})
    .SetKernelFn(PD_KERNEL(SaveWithOutputForward))
    .SetInferShapeFn(PD_INFER_SHAPE(SaveWithOutputInferShape))
    .SetInferDtypeFn(PD_INFER_DTYPE(SaveWithOutputInferDtype));
