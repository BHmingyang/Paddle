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

#pragma once
#include "paddle/phi/core/dense_tensor.h"
#include "paddle/phi/core/kernel_registry.h"
#include "paddle/phi/kernels/funcs/eigen/common.h"
#include "paddle/phi/kernels/funcs/for_range.h"
#include "paddle/phi/kernels/funcs/selected_rows_functor.h"

namespace phi {

template <typename T>
class SparseFTRLFunctor {
 private:
  const T* g_;
  const T* p_;
  const T* s_acc_;
  const T* l_acc_;
  const T* lr_;
  const T l1_;
  const T l2_;
  const T lr_power_;
  const int64_t* rows_;
  const int64_t row_numel_;
  T* p_out_;
  T* s_acc_out_;
  T* l_acc_out_;

 public:
  SparseFTRLFunctor(const T* g,
                    const T* p,
                    const T* s_acc,
                    const T* lr,
                    const T l1,
                    const T l2,
                    const T lr_power,
                    const int64_t* rows,
                    int64_t row_numel,
                    T* p_out,
                    T* s_acc_out,
                    T* l_acc_out)
      : g_(g),
        p_(p),
        s_acc_(s_acc),
        lr_(lr),
        l1_(l1),
        l2_(l2),
        lr_power_(lr_power),
        rows_(rows),
        row_numel_(row_numel),
        p_out_(p_out),
        s_acc_out_(s_acc_out),
        l_acc_out_(l_acc_out) {}

  inline HOSTDEVICE void operator()(size_t i) {
    auto j = rows_[i / row_numel_] * row_numel_ + i % row_numel_;
    const T g = g_[i];
    const T p = p_[j];
    const T s_acc = s_acc_[j];
    const T lr = lr_[0];

    auto new_acc = s_acc + g * g;

    if (lr_power_ == static_cast<T>(-0.5)) {
      l_acc_out_[j] += g - (std::sqrt(new_acc) - std::sqrt(s_acc)) / lr * p;
    } else {
      l_acc_out_[j] +=
          g - (std::pow(new_acc, -lr_power_) - std::pow(s_acc, -lr_power_)) /
                  lr * p;
    }

    auto l_acc = l_acc_out_[j];

    if (std::fabs(l_acc) > l1_) {
      auto x = -l_acc;
      if (l_acc >= static_cast<T>(0)) {
        x += l1_;
      } else {
        x -= l1_;
      }

      auto y = static_cast<T>(2) * l2_;
      if (lr_power_ == static_cast<T>(-0.5)) {
        y += std::sqrt(new_acc) / lr;
      } else {
        y += std::pow(new_acc, -lr_power_) / lr;
      }

      auto pre_shrink = x / y;
      p_out_[j] = pre_shrink;
    } else {
      p_out_[j] = static_cast<T>(0);
    }

    s_acc_out_[j] += g * g;
  }
};

template <typename T, typename Context>
void FTRLOpKernel(const Context& dev_ctx,
                  const DenseTensor& param,
                  const DenseTensor& squared_accumulator,
                  const DenseTensor& linear_accumulator,
                  const DenseTensor& grad_in,
                  const DenseTensor& learning_rate,
                  float l1_in,
                  float l2_in,
                  float lr_power_in,
                  DenseTensor* param_out,
                  DenseTensor* squared_accum_out,
                  DenseTensor* linear_accum_out) {
  auto* lr_in = &learning_rate;

  auto* param_in = &param;
  auto* sq_accum_in = &squared_accumulator;
  auto* lin_accum_in = &linear_accumulator;

  auto* sq_accum_out = squared_accum_out;
  auto* lin_accum_out = linear_accum_out;

  dev_ctx.template Alloc<T>(param_out);
  dev_ctx.template Alloc<T>(sq_accum_out);
  dev_ctx.template Alloc<T>(lin_accum_out);

  auto l1 = static_cast<T>(l1_in) + static_cast<T>(1e-10);
  auto l2 = static_cast<T>(l2_in) + static_cast<T>(1e-10);
  auto lr_power = static_cast<T>(lr_power_in);

  auto grad = &grad_in;
  auto g = EigenVector<T>::Flatten(*grad);

  auto p = EigenVector<T>::Flatten(*param_in);
  auto sq_accum = EigenVector<T>::Flatten(*sq_accum_in);
  auto lin_accum = EigenVector<T>::Flatten(*lin_accum_in);
  auto lr = EigenVector<T>::Flatten(*lr_in);

  auto p_out = EigenVector<T>::Flatten(*param_out);
  auto s_acc_out = EigenVector<T>::Flatten(*sq_accum_out);
  auto l_acc_out = EigenVector<T>::Flatten(*lin_accum_out);
  auto& place = *dev_ctx.eigen_device();

  Eigen::DSizes<int, 1> grad_dsize(grad->numel());

  auto new_accum = sq_accum + g * g;
  // Special case for lr_power = -0.5
  if (lr_power == static_cast<T>(-0.5)) {
    l_acc_out.device(place) =
        lin_accum + g -
        ((new_accum.sqrt() - sq_accum.sqrt()) / lr.broadcast(grad_dsize)) * p;
  } else {
    l_acc_out.device(place) =
        lin_accum + g -
        ((new_accum.pow(-lr_power) - sq_accum.pow(-lr_power)) /
         lr.broadcast(grad_dsize)) *
            p;
  }

  auto x = (l_acc_out.constant(l1) * l_acc_out.sign() - l_acc_out);
  if (lr_power == static_cast<T>(-0.5)) {
    auto y = (new_accum.sqrt() / lr.broadcast(grad_dsize)) +
             l_acc_out.constant(static_cast<T>(2) * l2);
    auto pre_shrink = x / y;
    p_out.device(place) =
        (l_acc_out.abs() > l_acc_out.constant(l1))
            .select(pre_shrink, p.constant(static_cast<T>(0)));
  } else {
    auto y = (new_accum.pow(-lr_power) / lr.broadcast(grad_dsize)) +
             l_acc_out.constant(static_cast<T>(2) * l2);
    auto pre_shrink = x / y;
    p_out.device(place) =
        (l_acc_out.abs() > l_acc_out.constant(l1))
            .select(pre_shrink, p.constant(static_cast<T>(0)));
  }

  s_acc_out.device(place) = sq_accum + g * g;
}
}  // namespace phi
