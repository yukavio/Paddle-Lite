// Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.
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

#include <cmath>
#include <string>
#include <vector>
#include "lite/backends/arm/math/conv_impl.h"
#include "lite/backends/arm/math/funcs.h"
#include "lite/core/context.h"
#include "lite/core/kernel.h"
#include "lite/core/target_wrapper.h"

namespace paddle {
namespace lite {
namespace kernels {
namespace arm {

template <PrecisionType Ptype, PrecisionType Otype>
class IntragroupConv : public KernelLite<TARGET(kARM), Ptype> {
 public:
  IntragroupConv() = default;
  ~IntragroupConv() {}

  void init_index(int8_t *index, int size)
  {
    const int8_t temp[8] = {0, 1, 2, 3, 0, 1, 2, 3};
    int8x8_t t = vld1_s8(temp);
    for(int i=0;i<size/8;i++)
    {
      vst1_s8(index, t);
      index+=8;
    }
  }

  virtual void ReInitWhenNeeded() {
    auto& param = this->template Param<param_t>();
    CHECK(this->ctx_);
    auto& ctx = this->ctx_->template As<ARMContext>();
    auto x_dims = param.x->dims();
    auto w_dims = param.filter->dims();
    auto o_dims = param.output->dims();
    if (last_shape_ == x_dims) {
      return;
    }

    int iw = x_dims[3];  // nchw
    int ih = x_dims[2];
    int ic = x_dims[1];
    int ow = o_dims[3];
    int oh = o_dims[2];
    int oc = o_dims[1];
    int kw = w_dims[3];
    int kh = w_dims[2];

    auto paddings = *param.paddings;
    auto dilations = *param.dilations;

    int sw = param.strides[1];
    int sh = param.strides[0];
    int pw = paddings[2];
    int ph = paddings[0];
    int dw = dilations[1];
    int dh = dilations[0];

    bool pads_equal =
        ((paddings[0] == paddings[1]) && (paddings[2] == paddings[3]));

    int m = oc / param.groups;
    int k = ic * kh * kw / param.groups;
    int n = oh * ow;

    bool kps_equal = (pw == ph) && (sw == sh) && (kw == kh);
    bool ks_equal = (sw == sh) && (kw == kh);
    flag_1x1gemm_ = true; // only implement pointwise conv now.
    
    // init index data for temp test of intragroup sparsity.
    index.Resize((*(param.filter)).dims());
    auto index_ptr = index.mutable_data<int8_t>();
    init_index(index_ptr, (*(param.filter)).dims().count(0,4));

    if (!flag_trans_weights_ && n > 1 && m > 1) {
      lite::arm::math::trans_gemm_weights<PRECISION(kInt8)>(
          *(param.filter), weights_, param.groups, &ctx);
      lite::arm::math::trans_gemm_weights<PRECISION(kInt8)>(
          index, index_, param.groups, &ctx);
      flag_trans_weights_ = true;
    } else if (n == 1 || m == 1) {
      flag_trans_weights_ = false;
    }
    last_shape_ = x_dims;
  }
  virtual void PrepareForRun();
  virtual void Run();

#ifdef LITE_WITH_PROFILE
  virtual void SetProfileRuntimeKernelInfo(
      paddle::lite::profile::OpCharacter* ch) {
    ch->kernel_func_name = kernel_func_name_;
  }

  std::string kernel_func_name_{"NotImplForConvGemm"};
#endif

  /// todo, support inplace weights transform
 protected:
  using param_t = operators::ConvParam;
  DDim last_shape_;
  std::vector<float> w_scale_;
  bool flag_1x1gemm_{true};
  bool flag_trans_weights_{false};
  bool flag_trans_bias_{false};
  Tensor index; // Temp implement for index in intragroup sparsity.
  Tensor index_; // the real index data like weights_.
  Tensor weights_;
  Tensor bias_;
  int workspace_size_{0};
};

}  // namespace arm
}  // namespace kernels
}  // namespace lite
}  // namespace paddle