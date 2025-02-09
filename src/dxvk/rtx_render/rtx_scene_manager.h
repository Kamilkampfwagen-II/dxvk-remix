/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#include <mutex>
#include <vector>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <list>
#include <variant>

#include "../dxvk_buffer.h"
#include "../dxvk_image.h"
#include "../dxvk_staging.h"
#include "../dxvk_bind_mask.h"
#include "../util/util_hashtable.h"

#include "rtx_types.h"
#include "rtx_common_object.h"
#include "rtx_camera_manager.h"
#include "rtx_draw_call_cache.h"
#include "rtx_sparse_unique_cache.h"
#include "rtx_light_manager.h"
#include "rtx_instance_manager.h"
#include "rtx_accel_manager.h"
#include "rtx_ray_portal_manager.h"
#include "rtx_bindless_resource_manager.h"
#include "rtx_volume_manager.h"
#include <d3d9types.h>

namespace dxvk 
{
class DxvkContext;
class DxvkDevice;
struct AssetReplacement;
struct AssetReplacer;
class OpacityMicromapManager;
class TerrainBaker;

struct Highlighting {
  dxvk::mutex mutex{};
  HighlightColor color {};

  std::optional<uint32_t> finalSurfaceMaterialIndex{};
  uint32_t finalWasUpdatedFrameId { kInvalidFrameIndex };

  // if set, need to traverse draw calls to find a corresponding surfaceMaterialIndex
  // for the given legacy texture hash, and if found, finalSurfaceMaterialIndex is updated
  std::optional<XXH64_hash_t> findSurfaceForLegacyTextureHash{};

  static bool keepRequest(uint32_t frameIdOfRequest, uint32_t curFrameId) {
    constexpr auto numFramesConsiderHighlighting = kMaxFramesInFlight * 2;
    return std::abs(int64_t { frameIdOfRequest } - int64_t{ curFrameId }) < numFramesConsiderHighlighting;
  }
};

// The resource cache can be *searched* by other users
class ResourceCache {
public:
  bool find(const RtSurfaceMaterial& surf, uint32_t& outIdx) const { return m_surfaceMaterialCache.find(surf, outIdx); }

protected:
  BufferRefTable<RaytraceBuffer> m_bufferCache;
  BufferRefTable<Rc<DxvkSampler>> m_materialSamplerCache;

  struct SurfaceMaterialHashFn {
    size_t operator() (const RtSurfaceMaterial& mat) const {
      return (size_t)mat.getHash();
    }
  };
  SparseUniqueCache<RtSurfaceMaterial, SurfaceMaterialHashFn> m_surfaceMaterialCache;
  SparseUniqueCache<RtSurfaceMaterial, SurfaceMaterialHashFn> m_surfaceMaterialExtensionCache;

  struct VolumeMaterialHashFn {
    size_t operator() (const RtVolumeMaterial& mat) const {
      return (size_t)mat.getHash();
    }
  };
  SparseUniqueCache<RtVolumeMaterial, VolumeMaterialHashFn> m_volumeMaterialCache;

  struct SamplerHashFn {
    size_t operator() (const Rc<DxvkSampler>& sampler) const {
      return (size_t) sampler->hash();
    }
  };

  struct SamplerKeyEqual {
    bool operator()(const Rc<DxvkSampler>& lhs, const Rc<DxvkSampler>& rhs) const {
      return lhs->info() == rhs->info();
    }
  };

  SparseUniqueCache<Rc<DxvkSampler>, SamplerHashFn, SamplerKeyEqual> m_samplerCache;
};

// Scene manager is a super manager, it's the interface between rendering and world state
// along with managing the operation of other caches, scene manager also manages the cache
// directly for "SceneObject"'s - which are "unique meshes/geometry", which map 1-to-1 with
// BLAS entries in raytracing terminology.
class SceneManager : public CommonDeviceObject, public ResourceCache {
public:
  SceneManager(SceneManager const&) = delete;
  SceneManager& operator=(SceneManager const&) = delete;

  explicit SceneManager(DxvkDevice* device);
  ~SceneManager();

  void initialize(Rc<DxvkContext> ctx);

  void onDestroy();

  void submitDrawState(Rc<DxvkContext> ctx, const DrawCallState& input, const MaterialData* overrideMaterialData);
  
  bool areReplacementsLoaded() const;
  bool areReplacementsLoading() const;
  const std::string getReplacementStatus() const;

  uint64_t getGameTimeSinceStartMS();

  Rc<DxvkBuffer> getSurfaceMaterialBuffer() { return m_surfaceMaterialBuffer; }
  Rc<DxvkBuffer> getSurfaceMaterialExtensionBuffer() { return m_surfaceMaterialExtensionBuffer; }
  Rc<DxvkBuffer> getVolumeMaterialBuffer() { return m_volumeMaterialBuffer; }
  Rc<DxvkBuffer> getSurfaceBuffer() const { return m_accelManager.getSurfaceBuffer(); }
  Rc<DxvkBuffer> getSurfaceMappingBuffer() const { return m_accelManager.getSurfaceMappingBuffer(); }
  Rc<DxvkBuffer> getCurrentFramePrimitiveIDPrefixSumBuffer() const { return m_accelManager.getCurrentFramePrimitiveIDPrefixSumBuffer(); }
  Rc<DxvkBuffer> getLastFramePrimitiveIDPrefixSumBuffer() const { return m_accelManager.getLastFramePrimitiveIDPrefixSumBuffer(); }
  Rc<DxvkBuffer> getBillboardsBuffer() const { return m_accelManager.getBillboardsBuffer(); }
  bool isPreviousFrameSceneAvailable() const { return m_previousFrameSceneAvailable && getSurfaceMappingBuffer().ptr() != nullptr; }

  const std::vector<Rc<DxvkSampler>>& getSamplerTable() const { return m_samplerCache.getObjectTable(); }
  const std::vector<RaytraceBuffer>& getBufferTable() const { return m_bufferCache.getObjectTable(); }
  const std::vector<RtInstance*>& getInstanceTable() const { return m_instanceManager.getInstanceTable(); }
  
  const InstanceManager& getInstanceManager() const { return m_instanceManager; }
  const AccelManager& getAccelManager() const { return m_accelManager; }
  const LightManager& getLightManager() const { return m_lightManager; }
  const RayPortalManager& getRayPortalManager() const { return m_rayPortalManager; }
  const BindlessResourceManager& getBindlessResourceManager() const { return m_bindlessResourceManager; }
  const OpacityMicromapManager* getOpacityMicromapManager() const { return m_opacityMicromapManager.get(); }
  const VolumeManager& getVolumeManager() const { return m_volumeManager; }
  LightManager& getLightManager() { return m_lightManager; }
  std::unique_ptr<AssetReplacer>& getAssetReplacer() { return m_pReplacer; }
  TerrainBaker& getTerrainBaker() { return *m_terrainBaker.get(); }

  // Scene utility functions
  static Vector3 getSceneUp();
  static Vector3 getSceneForward();
  static Vector3 calculateSceneRight();

  // Reswizzles input vector to an output that has xy coordinates on scene's horizontal axes and z coordinate to be on the scene's vertical axis
  static Vector3 worldToSceneOrientedVector(const Vector3& worldVector); 

  static Vector3 sceneToWorldOrientedVector(const Vector3& sceneVector);

  void addLight(const D3DLIGHT9& light);
  
  CameraType::Enum processCameraData(const DrawCallState& input) {
    return m_cameraManager.processCameraData(input);
  }

  const CameraManager& getCameraManager() const { return m_cameraManager; }
  const RtCamera& getCamera() const { return m_cameraManager.getMainCamera(); }
  RtCamera& getCamera() { return m_cameraManager.getMainCamera(); }

  FogState& getFogState() { return m_fog; }
  void clearFogState();
  
  uint32_t getActivePOMCount() {return m_activePOMCount;}

  float getTotalMipBias();

  // ISceneManager but not really
  void clear(Rc<DxvkContext> ctx, bool needWfi);
  void garbageCollection();
  void prepareSceneData(Rc<DxvkContext> ctx, class DxvkBarrierSet& execBarriers, const float frameTimeSecs);

  void onFrameEnd(Rc<DxvkContext> ctx);
  void onFrameEndNoRTX();

  // GameCapturer
  void triggerUsdCapture() const;
  bool isGameCapturerIdle() const;

  void trackTexture(Rc<DxvkContext> ctx, TextureRef inputTexture, uint32_t& textureIndex, bool hasTexcoords, bool allowAsync = true);
  void trackSampler(Rc<DxvkSampler> sampler, bool patchSampler, uint32_t& samplerIndex);

  std::future<XXH64_hash_t> findLegacyTextureHashBySurfaceMaterialIndex(uint32_t surfaceMaterialIndex);

  void requestHighlighting(std::variant<uint32_t, XXH64_hash_t> surfaceMaterialIndexOrLegacyTextureHash,
                           HighlightColor color,
                           uint32_t frameId);
  std::optional<std::pair<uint32_t, HighlightColor>> accessSurfaceMaterialIndexToHighlight(uint32_t frameId);

private:
  enum class ObjectCacheState
  {
    kUpdateInstance = 0,
    kUpdateBVH = 1,
    KBuildBVH = 2,
    kInvalid = -1
  };
  // Handles conversion of geometry data coming from a draw call, to the data used by the raytracing backend
  template<bool isNew>
  ObjectCacheState processGeometryInfo(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, RaytraceGeometry& modifiedGeometryData);

  // Consumes a draw call state and updates the scene state accordingly
  uint64_t processDrawCallState(Rc<DxvkContext> ctx, const DrawCallState& blasInput, const MaterialData* replacementMaterialData);

  // Updates ref counts for new buffers
  void updateBufferCache(RaytraceGeometry& newGeoData);

  // Called whenever a new BLAS scene object is added to the cache
  ObjectCacheState onSceneObjectAdded(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, BlasEntry* pBlas);
  // Called whenever a BLAS scene object is updated
  ObjectCacheState onSceneObjectUpdated(Rc<DxvkContext> ctx, const DrawCallState& drawCallState, BlasEntry* pBlas);
  // Called whenever a BLAS scene object is destroyed
  void onSceneObjectDestroyed(const BlasEntry& pBlas);

  // Called whenever a new instance has been added to the database
  void onInstanceAdded(const RtInstance& instance);
  // Called whenever instance metadata is updated
  void onInstanceUpdated(RtInstance& instance, const RtSurfaceMaterial& material, const bool hasTransformChanged, const bool hasVerticesdChanged);
  // Called whenever an instance has been removed from the database
  void onInstanceDestroyed(const RtInstance& instance);

  uint64_t drawReplacements(Rc<DxvkContext> ctx, const DrawCallState* input, const std::vector<AssetReplacement>* pReplacements, const MaterialData* overrideMaterialData);

  void createEffectLight(Rc<DxvkContext> ctx, const DrawCallState& input, const RtInstance* instance);

  uint32_t m_beginUsdExportFrameNum = -1;
  bool m_enqueueDelayedClear = false;
  bool m_previousFrameSceneAvailable = false;

  // Hash/Cache's
  InstanceManager m_instanceManager;
  AccelManager m_accelManager;
  LightManager m_lightManager;
  RayPortalManager m_rayPortalManager;
  BindlessResourceManager m_bindlessResourceManager;
  std::unique_ptr<OpacityMicromapManager> m_opacityMicromapManager;
  VolumeManager m_volumeManager;

  DrawCallCache m_drawCallCache;

  CameraManager m_cameraManager;

  std::unique_ptr<AssetReplacer> m_pReplacer;

  std::unique_ptr<TerrainBaker> m_terrainBaker;

  FogState m_fog;

  // TODO: Move the following resources and getters to RtResources class
  Rc<DxvkBuffer> m_surfaceMaterialBuffer;
  Rc<DxvkBuffer> m_surfaceMaterialExtensionBuffer;
  Rc<DxvkBuffer> m_volumeMaterialBuffer;

  uint32_t m_currentFrameIdx = -1;
  bool m_useFixedFrameTime = false;
  std::chrono::time_point<std::chrono::steady_clock> m_startTime;
  uint32_t m_activePOMCount = 0;

  Highlighting m_highlighting {};

  struct PromisedSurfMaterialIndex
  {
    uint32_t targetSurfMaterialIndex { 0 };
    std::promise<XXH64_hash_t> promise{};
  };
  std::optional<PromisedSurfMaterialIndex> m_findLegacyTexture {};
  dxvk::mutex m_findLegacyTextureMutex{};
};

}  // namespace nvvk

