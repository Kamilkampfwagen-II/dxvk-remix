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

#include "rtx_game_capturer_utils.h"
#include "rtx_options.h"

#include "../../lssusd/game_exporter_types.h"
#include "../../util/rc/util_rc_ptr.h"
#include "../../util/rc/util_rc.h"
#include "../../util/util_flags.h"
#include "../../util/util_matrix.h"
#include "../../util/xxHash/xxhash.h"
#include "../imgui/dxvk_imgui.h"

#include <vector>
#include <unordered_map>
#include <mutex>

namespace dxvk 
{
class DxvkContext;
class SceneManager;
class AssetExporter;
class ImGUI;
struct RtLight;
struct RtSphereLight;
struct RtRectLight;
struct RtDiskLight;
struct RtCylinderLight;
struct RtDistantLight;
struct BlasEntry;
class RtInstance;
struct RaytraceGeometry;
struct MaterialData;
struct DrawCallState;
struct LegacyMaterialData;

class GameCapturer : public RcObject
{
public:
  RW_RTX_OPTION("rtx.capture", bool, correctBakedTransforms, false,
                "Some games bake world transforms into mesh vertices. If individually captured\n"
                "meshes appear to be way off in the middle of nowhere OR instanced meshes appear\n"
                "to all have identity xform matrices, enabling will attempt to correct this and\n"
                "improve stage + mesh viewability in tools.\n"
                "Hashes are unaffected.");

  GameCapturer(DxvkDevice* const pDevice, SceneManager& sceneManager, AssetExporter& exporter);
  ~GameCapturer();

  void step(const Rc<DxvkContext> ctx, const float dt);
  void triggerNewCapture() {
    m_bTriggerCapture = true;
  }
  
  // Instance Flags
  enum class InstFlag : uint8_t {
    PositionsUpdate = 0,
    NormalsUpdate = 1,
    IndexUpdate = 2,
    XformUpdate = 3
  };
  void setInstanceUpdateFlag(const RtInstance& rtInstance, const InstFlag flag);

  // State
  FLAGS(State, uint8_t,
    Initializing,
    Capturing,
    BeginExport,
    PreppingExport,
    Exporting,
    Complete
  );
  const State& getState() const {
    return m_state;
  }
  bool isIdle() const {
    return m_state.isClear() || m_state.has<State::Complete>();
  }

  struct CompletedCapture {
    std::string stageName;
    std::string stagePath;
  } m_completeCapture;
  const CompletedCapture& queryCompleteCapture() const {
    return m_completeCapture;
  }

  static const std::string s_baseDir;

private:
  GameCapturer() = delete;
  GameCapturer(const GameCapturer& other) = delete;
  GameCapturer(const GameCapturer&& other) = delete;
  
  using Xform = Matrix4;
  struct SampledXform {
    float time;
    Xform xform;
  };
  using SampledXforms = std::vector<SampledXform>;

  struct Material {
    lss::Material lssData;
  };

  struct MeshSync {
    size_t                  numOutstanding = 0;
    dxvk::mutex              mutex;
    dxvk::condition_variable cond;
    void numOutstandingInc() { std::lock_guard lock(mutex); numOutstanding++; }
    void numOutstandingDec() { { std::lock_guard lock(mutex); numOutstanding--; } cond.notify_all(); }
  };

  struct Mesh {
    lss::Mesh        lssData;
    size_t           instanceCount = 0;
    XXH64_hash_t     matHash;
    MeshSync         meshSync;
    AtomicOriginCalc originCalc;
  };

  struct Instance {
    lss::Instance lssData;
    XXH64_hash_t  meshHash = 0;
    XXH64_hash_t  matHash = 0;
    size_t        meshInstNum = 0;
  };

  void trigger(const Rc<DxvkContext> ctx);
  void initCapture(const Rc<DxvkContext> ctx);
  void prepareInstanceStage(const Rc<DxvkContext> ctx);
  void capture(const Rc<DxvkContext> ctx, const float dt);
  void captureFrame(const Rc<DxvkContext> ctx);
  void captureCamera();
  void captureLights();
  void captureSphereLight(const dxvk::RtSphereLight& rtLight);
  void captureDistantLight(const RtDistantLight& rtLight);
  void captureInstances(const Rc<DxvkContext> ctx);
  void newInstance(const Rc<DxvkContext> ctx, const RtInstance& rtInstance);
  void captureMaterial(const Rc<DxvkContext> ctx, const LegacyMaterialData& materialData, const bool bEnableOpacity);
  void captureMesh(const Rc<DxvkContext> ctx,
                   const XXH64_hash_t currentMeshHash,
                   const BlasEntry& blas,
                   const CategoryFlags& flags,
                   const bool bIsNewMesh,
                   const bool bCapturePositions,
                   const bool bCaptureNormals,
                   const bool bCaptureIndices);
  template <typename T>
  void captureMeshPositions(const Rc<DxvkContext> ctx,
                            const size_t numVertices,
                            const T& inputPositionBuffer,
                            const float currentCaptureTime,
                            std::shared_ptr<Mesh> pMesh);
  template <typename T>
  void captureMeshNormals(const Rc<DxvkContext> ctx,
                          const size_t numVertices,
                          const T& inputNormalBuffer,
                          const float currentCaptureTime,
                          std::shared_ptr<Mesh> pMesh);
  void captureMeshIndices(const Rc<DxvkContext> ctx,
                          const RaytraceGeometry& geomData,
                          const float currentCaptureTime,
                          std::shared_ptr<Mesh> pMesh);
  void captureMeshTexCoords(const Rc<DxvkContext> ctx,
                            const RaytraceGeometry& geomData,
                            const float currentCaptureTime,
                            std::shared_ptr<Mesh> pMesh);
  void captureMeshColor(const Rc<DxvkContext> ctx,
                        const RaytraceGeometry& geomData,
                        const float currentCaptureTime,
                        std::shared_ptr<Mesh> pMesh);
  void captureMeshBlending(const Rc<DxvkContext> ctx,
                           const RasterGeometry& geomData,
                           const float currentCaptureTime,
                           std::shared_ptr<Mesh> pMesh);
  template <typename T, typename CompareTReturnBool>
  static void evalNewBufferAndCache(std::shared_ptr<Mesh> pMesh,
                                    std::map<float,pxr::VtArray<T>>& bufferCache,
                                    pxr::VtArray<T>& newBuffer,
                                    const float currentCaptureTime,
                                    CompareTReturnBool compareT);
  void exportUsd(const Rc<DxvkContext> ctx);
  struct Capture;
  static lss::Export prepExport(const Capture& cap,
                                const float framesPerSecond,
                                const bool bUseLssUsdPlugins);
  static void prepExportMetaData(const Capture& cap,
                                 const float framesPerSecond,
                                 const bool bUseLssUsdPlugins,
                                 lss::Export& exportPrep);
  static void prepExportMaterials(const Capture& cap,
                                  lss::Export& exportPrep);
  static void prepExportMeshes(const Capture& cap,
                               lss::Export& exportPrep);
  static void prepExportInstances(const Capture& cap,
                                  lss::Export& exportPrep);
  static void prepExportLights(const Capture& cap,
                               lss::Export& exportPrep);
  static void flattenExport(const lss::Export& exportPrep);

  static bool checkInstanceUpdateFlag(const uint8_t flags, const InstFlag flag) {
    return flags & (1 << uint8_t(flag));
  }
  
  // Options
  struct Options {
    // General
    bool bShowMenu;
    bool bCaptureInstances;
    std::string instanceStageName;
    // Multiframe
    bool bEnableMultiframe;
    uint32_t numFrames;
    // Advanced
    uint32_t fps;
    //   Mesh capture deltas
    float dPos;
    float dNorm;
    float dTexcoord;
    float dColor;
    float dBlendweight;
  } m_options;

  static Options getOptions() {
    return { RtxOptions::Get()->getCaptureShowMenuOnHotkey(),
             RtxOptions::Get()->getCaptureInstances(),
             RtxOptions::Get()->getCaptureInstanceStageName(),
             RtxOptions::Get()->getCaptureEnableMultiframe(),
             RtxOptions::Get()->getCaptureMaxFrames(),
             RtxOptions::Get()->getCaptureFramesPerSecond(),
             RtxOptions::Get()->getCaptureMeshPositionDelta(),
             RtxOptions::Get()->getCaptureMeshNormalDelta(),
             RtxOptions::Get()->getCaptureMeshTexcoordDelta(),
             RtxOptions::Get()->getCaptureMeshColorDelta(),
             RtxOptions::Get()->getCaptureMeshBlendWeightDelta() };
  }

  // State
  bool m_bTriggerCapture = false;
  State m_state;
  
  // Constants
  const bool m_bUseLssUsdPlugins;

  // Handles
  DxvkDevice* const m_pDevice;
  SceneManager& m_sceneManager; // We only use SceneManager get()ers, but none are const
  AssetExporter& m_exporter;

  // Capturing
  dxvk::mutex m_meshMutex;
  struct Capture {
    static size_t nextId;
    std::string idStr = "INVALID";
    bool bCaptureInstances;
    struct InstanceCapture {
      std::string stageName;
      std::string stagePath;
    } instance;
    bool bSkyProbeBaked;
    size_t numFramesCaptured = 0;
    float currentFrameNum = 0.f;
    lss::Camera camera;
    std::unordered_map<XXH64_hash_t, lss::SphereLight> sphereLights;
    std::unordered_map<XXH64_hash_t, lss::DistantLight> distantLights;
    std::unordered_map<XXH64_hash_t, std::shared_ptr<Mesh>> meshes;
    std::unordered_map<XXH64_hash_t, Material> materials;
    std::unordered_map<XXH64_hash_t, Instance> instances;
    std::unordered_map<XXH64_hash_t, uint8_t> instanceFlags;
  };
  std::unique_ptr<Capture> m_pCap;
};

}