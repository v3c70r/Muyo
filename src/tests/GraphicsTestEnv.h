#pragma once
#include "DescriptorManager.h"
#include "MeshResourceManager.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "SceneManager.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
namespace Muyo {
// RAII graphics environment
class GraphicsTestEnv {
public:
  GraphicsTestEnv() {
    GetRenderDevice()->Initialize({}, {});
    GetRenderDevice()->CreateDevice({}, std::vector<const char *>(), nullptr,
                                    {});
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
};

class GraphicsTestEnvMazdaScene : public GraphicsTestEnv {
public:
  GraphicsTestEnvMazdaScene() {
    // Prepare a scene
    GetSceneManager()->LoadSceneFromFile(
        "assets/mazda_mx-5_spot/untitled.gltf");
    m_mDrawList = GetSceneManager()->GatherDrawLists();
  }
};
} // namespace Muyo
