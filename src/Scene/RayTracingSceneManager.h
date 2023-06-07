#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <unordered_map>
#include <vector>

#include "Material.h"
namespace Muyo
{

struct SubmeshDescription
{
    VkDeviceAddress m_vertexBufferAddress;
    VkDeviceAddress m_indextBufferAddress;

    VkDeviceAddress m_pbrFactorAddress;
    std::array<uint32_t, Material::TEX_COUNT> m_aPbrTextureIndices;
};

class SceneNode;
class RayTracingSceneManager
{
public:
    void BuildScene(const std::vector<const SceneNode*>& vpGeometries);

private:
    std::vector<SubmeshDescription> m_vSubmeshDescs;
    std::unordered_map<const SceneNode*, uint32_t> m_mSubmeshBeginIndices;

private:
    AccelerationStructure* BuildBLASfromNode(const SceneNode& geometry, VkBuildAccelerationStructureFlagsKHR flags);
    AccelerationStructure* BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& vInstances);
    uint32_t AlignUp(uint32_t nSize, uint32_t nAlignment);
};
}  // namespace Muyo
