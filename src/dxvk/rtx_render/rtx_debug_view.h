/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx/utility/shader_types.h"
#include "rtx/utility/debug_view_indices.h"
#include "rtx/pass/debug_view/debug_view_args.h"

#include "dxvk_context.h"
#include "../dxvk_include.h"

#include "rtx_resources.h"
#include "rtx_options.h"

struct DebugViewArgs;

namespace dxvk {
  class Config;
  class DxvkDevice;
  class DxvkContext;
  class DxvkObjects;

  struct FindSurfaceResult {
    uint32_t surfaceMaterialIndex { 0 };
    // corresponding legacy texture hash for SurfaceMaterialIndex
    std::future<XXH64_hash_t> legacyTextureHash{};
  };

  class DebugView : public RtxPass {

  public:

    DebugView(dxvk::DxvkDevice* device);
    ~DebugView() = default;

    void dispatch(Rc<DxvkContext> ctx,
                  Rc<DxvkSampler> nearestSampler,
                  Rc<DxvkSampler> linearSampler, 
                  Rc<DxvkImage>& outputImage, 
                  const Resources::RaytracingOutput& rtOutput, 
                  DxvkObjects& common);

    void initSettings(const dxvk::Config& config);
    void showImguiSettings();
    const vec4& debugKnob() const { return m_debugKnob; }

    const Rc<DxvkImageView>& getDebugOutput() {
      return m_debugView.view;
    }

    const Rc<DxvkImageView>& getFinalDebugOutput() {
      return static_cast<CompositeDebugView>(m_composite.compositeViewIdx()) != CompositeDebugView::Disabled
        ? m_composite.compositeView.view
        : m_debugView.view;
    }

    const Rc<DxvkImageView>& getInstrumentation() {
      return m_instrumentation.view;
    }

    // GPU Print
    static struct GpuPrint {
      friend class DebugView;
      RTX_OPTION("rtx.debugView.gpuPrint", bool, enable, false, "Enables writing into a GPU buffer that's read by CPU when CTRL is pressed. The value is printed to console.");
      RTX_OPTION("rtx.debugView.gpuPrint", bool, useMousePosition, true, "Uses mouse position to select a pixel to GPU print for.");
      RTX_OPTION("rtx.debugView.gpuPrint", Vector2i, pixelIndex, Vector2i(INT32_MAX, INT32_MAX), "Pixel position to GPU print for. Requires useMousePosition to be turned off.");
    } gpuPrint;

  protected:
    virtual void onFrameBegin(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) override;

  private:
    void createConstantsBuffer();
    Rc<DxvkBuffer> getDebugViewConstantsBuffer();

    DebugViewArgs getCommonDebugViewArgs(DxvkContext* ctx, const Resources::RaytracingOutput& rtOutput, DxvkObjects& common);

    void generateCompositeImage(Rc<DxvkContext> ctx, Rc<DxvkImage>& outputImage);
    void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent);
    void releaseDownscaledResource();

    bool isActive();

    Rc<DxvkBuffer> m_debugViewConstants;
    Rc<vk::DeviceFn> m_vkd;
    dxvk::DxvkDevice* m_device;
    std::chrono::time_point<std::chrono::system_clock> m_startTime;

    RTX_OPTION_ENV("rtx.debugView", uint32_t, debugViewIdx, DEBUG_VIEW_DISABLED, "DXVK_RTX_DEBUG_VIEW_INDEX", "Index of a debug view to show when Debug View is enabled. The index must be a valid value from DEBUG_VIEW_* macro defined indices. Value of 0 disables Debug View.");
    // Note: Used for preserving the debug view state only for ImGui purposes. Not to be used for anything else
    // and should not ever be set to the disabled debug view index.
    uint32_t m_lastDebugViewIdx;
    RTX_OPTION_ENV("rtx.debugView", DebugViewDisplayType, displayType, DebugViewDisplayType::Standard, "DXVK_RTX_DEBUG_VIEW_DISPLAY_TYPE", "");

    RTX_OPTION("rtx.debugView", DebugViewSamplerType, samplerType, DebugViewSamplerType::NormalizedLinear, "Sampler type for debug views that sample from a texture (applies only to a subset of debug views).\n"
                                                                                                           "0: Nearest.\n"
                                                                                                           "1: Normalized Nearest.\n"
                                                                                                           "2: Normalized Linear.");

    struct Composite {
      friend class ImGUI; // <-- we want to modify these values directly.
      friend class DebugView; // <-- we want to modify these values directly.

      RTX_OPTION_ENV("rtx.debugView.composite", uint32_t, compositeViewIdx, CompositeDebugView::Disabled, "RTX_DEBUG_VIEW_COMPOSITE_VIEW_INDEX", "Index of a composite view to show when Composite Debug View is enabled. The index must be a a valid value from CompositeDebugView enumeration. Value of 0 disables Composite Debug View.");
    
      std::vector<uint32_t> debugViewIndices;
      // Note: Used for preserving the debug view state only for ImGui purposes. Not to be used for anything else
      // and should not ever be set to the disabled debug view index.
      CompositeDebugView lastCompositeViewIdx = CompositeDebugView::FinalRenderWithMaterialProperties;
      Resources::Resource compositeView;
    } m_composite;

    // Common Display
    bool m_enableInfNanView = true;
    int m_colorCodeRadius = 4;

    // Standard Display
    RTX_OPTION_ENV("rtx.debugView", bool, enablePseudoColor, false, "RTX_DEBUG_VIEW_ENABLE_PSEUDO_COLOR", "Enables RGB color coding of a scalar debug view value.");
    RTX_OPTION_ENV("rtx.debugView", bool, enableGammaCorrection, false, "RTX_DEBUG_VIEW_ENABLE_GAMMA_CORRECTION", "Enables gamma correction of a debug view value.");
    bool m_enableAlphaChannel = false;
    float m_scale = 1.f;
    RTX_OPTION_ENV("rtx.debugView", float, minValue, 0.f, "DXVK_RTX_DEBUG_VIEW_MIN_VALUE", "");
    RTX_OPTION_ENV("rtx.debugView", float, maxValue, 1.f, "DXVK_RTX_DEBUG_VIEW_MAX_VALUE", "");

    // EV100 Display
    RTX_OPTION_ENV("rtx.debugView", int32_t, evMinValue, -4, "DXVK_RTX_DEBUG_VIEW_EV_MIN_VALUE", "");
    RTX_OPTION_ENV("rtx.debugView", int32_t, evMaxValue,  4, "DXVK_RTX_DEBUG_VIEW_EV_MAX_VALUE", "");

    // HDR Waveform Display
    bool m_enableLuminanceMode = false;
    int32_t m_log10MinValue = -3;
    int32_t m_log10MaxValue = 2;
    // Note: Resolution scale will always be >=2.
    uint32_t m_hdrWaveformResolutionScaleFactor = 2;
    uvec2 m_hdrWaveformPosition = uvec2{ 25, 25 };
    float m_hdrWaveformHistogramNormalizationScale = 8.0f;

    vec4 m_debugKnob = vec4{ 0.f, 0.f, 0.f, 0.f };

    bool m_cacheCurrentImage = false;
    bool m_showCachedImage = false;

    Resources::Resource m_cachedImage;
    Resources::Resource m_debugView;
    Resources::Resource m_hdrWaveformRed;
    Resources::Resource m_hdrWaveformGreen;
    Resources::Resource m_hdrWaveformBlue;
    Resources::Resource m_instrumentation;

  public:
    void requestFindSurfaceUnder(Vector2i pixel, uint32_t frameIdOfTheRequest) {
      std::lock_guard lock{ m_texturePickMutex };
      m_texturePickRequest = TexturePickingRequest { pixel, frameIdOfTheRequest };
    }

    std::optional<FindSurfaceResult>&& consumeLastAvailableFindSurfaceResult() {
      std::lock_guard lock{ m_texturePickMutex };
      return std::move(m_texturePickResult_prev);
    }

    std::optional<Vector2i> isFindSurfaceRequestActive(uint32_t currentFrameId) const {
      std::lock_guard lock{ m_texturePickMutex };
      constexpr auto numFramesToConsiderRequest = kMaxFramesInFlight * 2;
      if (std::abs(int64_t { m_texturePickRequest.frameId } - int64_t{ currentFrameId }) < numFramesToConsiderRequest) {
        return m_texturePickRequest.pixel;
      }
      return {};
    }

  private:
    friend class RtxContext;
    void placeFindSurfaceResult(std::optional<FindSurfaceResult>&& result) {
      std::lock_guard lock{ m_texturePickMutex };
      m_texturePickResult_prev = std::move(m_texturePickResult);
      m_texturePickResult = std::move(result);
    }

  private:
    mutable dxvk::mutex m_texturePickMutex{};
    struct TexturePickingRequest {
      Vector2i pixel { 0,0 };
      uint32_t frameId { kInvalidFrameIndex };
    };
    TexturePickingRequest m_texturePickRequest{};
    std::optional<FindSurfaceResult> m_texturePickResult{};
    std::optional<FindSurfaceResult> m_texturePickResult_prev{};
  };
} // namespace dxvk
