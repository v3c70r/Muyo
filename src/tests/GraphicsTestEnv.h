#pragma once
#include "DescriptorManager.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "SceneManager.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
#include "MeshResourceManager.h"
namespace Muyo
{
// RAII graphics environment
class GraphicsTestEnv
{
public:
    GraphicsTestEnv()
    {
        GetRenderDevice()->Initialize({}, {});
        GetRenderDevice()->CreateDevice({}, std::vector<const char*>(), nullptr, {});
        GetRenderDevice()->CreateCommandPools();
        GetMemoryAllocator()->Initalize(GetRenderDevice());
        GetRenderResourceManager()->Initialize();
        GetDescriptorManager()->CreateDescriptorPool();
        GetDescriptorManager()->CreateDescriptorSetLayouts();
        GetSamplerManager()->createSamplers();
        //GetMeshResourceManager()->PrepareSimpleMeshes();
        //GetMeshResourceManager()->UploadMeshData();
        // Load a scene to populate mesh and per object buffer
        GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5_spot/untitled.gltf");
        GetSceneManager()->GatherDrawLists();

        //GetMaterialManager()->CreateDefaultMaterial();
        //GetMaterialManager()->UploadMaterialBuffer();
    }
    ~GraphicsTestEnv()
    {
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
};
}  // namespace Muyo
