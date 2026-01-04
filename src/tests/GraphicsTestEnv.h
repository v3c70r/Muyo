#pragma once
#include "DescriptorManager.h"
#include "RenderResourceManager.h"
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
        GetMeshResourceManager()->PrepareSimpleMeshes();
        GetMeshResourceManager()->UploadMeshData();
    }
    ~GraphicsTestEnv()
    {
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
