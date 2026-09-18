#pragma once
#include "DescriptorManager.h"
#include "Material.h"
#include "MeshResourceManager.h"
#include "PerObjResourceManager.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "SceneManager.h"
#include "VkExtFuncsLoader.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
namespace Muyo {
// RAII graphics environment
class GraphicsTestEnv {
public:
  GraphicsTestEnv() {
    GetRenderDevice()->Initialize({}, {});
#ifdef FEATURE_RAY_TRACING
    // Device-level ray tracing entry points are resolved through the instance.
    VkExt::LoadInstanceFunctions(GetRenderDevice()->GetInstance());
#endif
    GetRenderDevice()->CreateDevice(GetTestDeviceExtensions(),
                                    std::vector<const char *>(), nullptr,
                                    GetTestDeviceFeatures());
    GetRenderDevice()->CreateCommandPools();
    GetMemoryAllocator()->Initalize(GetRenderDevice());
    GetRenderResourceManager()->Initialize();
    GetDescriptorManager()->CreateDescriptorPool();
    GetDescriptorManager()->CreateDescriptorSetLayouts();
    GetSamplerManager()->createSamplers();
  }
  ~GraphicsTestEnv() {
    GetSamplerManager()->destroySamplers();
    GetTextureResourceManager()->Destroy();
    GetDescriptorManager()->DestroyDescriptorSetLayouts();
    GetDescriptorManager()->DestroyDescriptorPool();
    GetRenderResourceManager()->Unintialize();
    GetRenderDevice()->DestroyCommandPools();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
  }

protected:
  DrawLists m_mDrawList;

private:
  // Device extensions required by the tests. Ray tracing must be requested here so the
  // acceleration-structure / SBT pools created by the memory allocator are valid.
  static const std::vector<const char *> &GetTestDeviceExtensions() {
    static const std::vector<const char *> extensions = {
#ifdef FEATURE_RAY_TRACING
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
#endif
    };
    return extensions;
  }

  static const std::vector<void *> &GetTestDeviceFeatures() {
    static std::vector<void *> features = [] {
      std::vector<void *> f;
#ifdef FEATURE_RAY_TRACING
      static VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeature = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
      static VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeature = {
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
      rtFeature.rayTracingPipeline = VK_TRUE;
      asFeature.accelerationStructure = VK_TRUE;
      f.push_back(&asFeature);
      f.push_back(&rtFeature);
#endif
      return f;
    }();
    return features;
  }
};

class GraphicsTestEnvMazdaScene : public GraphicsTestEnv {
public:
  GraphicsTestEnvMazdaScene() {
    // SceneManager / MeshResourceManager / PerObjResourceManager are process-wide caches, while
    // the device (and therefore every GPU resource) is recreated for each test case. Load the
    // scene once and re-upload the shared buffers against the new device.
    if (GetSceneManager()->GetAllScenes().empty()) {
      GetSceneManager()->LoadSceneFromFile(
          "assets/mazda_mx-5_spot/untitled.gltf");
    }
    // GatherDrawLists populates the per-object data on first use.
    m_mDrawList = GetSceneManager()->GatherDrawLists();
    GetMeshResourceManager()->UploadMeshData();
    GetPerObjResourceManager()->Upload();
    GetMaterialManager()->RefreshGPUResources();
  }
};
} // namespace Muyo
