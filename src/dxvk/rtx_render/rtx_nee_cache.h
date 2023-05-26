/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#include "../dxvk_format.h"
#include "../dxvk_include.h"

#include "../spirv/spirv_code_buffer.h"
#include "rtx_resources.h"
#include "rtx_option.h"

namespace dxvk {

  class RtxContext;

  class NeeCachePass {

  public:

    NeeCachePass(dxvk::DxvkDevice* device);
    ~NeeCachePass();

    void dispatch(
      RtxContext* ctx, 
      const Resources::RaytracingOutput& rtOutput);

    void showImguiSettings();
    
    void setRaytraceArgs(RaytraceArgs& raytraceArgs) const;

    RW_RTX_OPTION("rtx.neeCache", bool, enabled, true, "Enable NEE cache.");
    RTX_OPTION("rtx.neeCache", bool, enableImportanceSampling, true, "Enable importance sampling.");
    RTX_OPTION("rtx.neeCache", bool, enableMIS, true, "Enable MIS.");
    RTX_OPTION("rtx.neeCache", bool, enableInFirstBounce, true, "Enable NEE Cache in first bounce.");
    RTX_OPTION("rtx.neeCache", bool, enableInHigherBounces, true, "Enable NEE Cache in higher bounces.");
    RTX_OPTION("rtx.neeCache", bool, enableRandomReplacement, false, "Enable random replacement.");
    RTX_OPTION("rtx.neeCache", float, range, 2000, "World space range.");    
    RTX_OPTION("rtx.neeCache", float, textureSampleFootprintSize, 1.0, "Texture sample footprint size.");
    RTX_OPTION("rtx.neeCache", float, ageCullingSpeed, 0.02, "A triangle without being detected for several frames will be less important. This threshold determines the culling speed.");
  private:
    Rc<vk::DeviceFn> m_vkd;

    dxvk::DxvkDevice* m_device;
  };
} // namespace dxvk
