#pragma once
#include "Material.h"

#include <vector>
#include <array>
#include <vulkan/vulkan.h>
#include <unordered_map>


enum SBTRegions
{
    SBT_REGION_RAY_GEN = 0,
    SBT_REGION_RAY_MISS,
    SBT_REGION_RAY_HIT,
    SBT_REGION_CALLABLE,
    SBT_REGION_COUNT

};
struct PrimitiveDescription
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
    void AllocateShaderBindingTable();
    void BuildRayTracingPipeline();
    void DestroyPipeline();


    const VkStridedDeviceAddressRegionKHR* GetRayGenRegion() const {return &m_aSBTRegions[SBT_REGION_RAY_GEN];}
    const VkStridedDeviceAddressRegionKHR* GetRayMissRegion() const {return &m_aSBTRegions[SBT_REGION_RAY_MISS];}
    const VkStridedDeviceAddressRegionKHR* GetRayHitRegion() const {return &m_aSBTRegions[SBT_REGION_RAY_HIT];}
    VkPipelineLayout GetPipelineLayout() const {return m_pipelineLayout;}
    VkPipeline GetPipeline() const {return m_pipeline;}

private:
    std::vector<PrimitiveDescription> m_vPrimitiveDescs;
    std::unordered_map<const SceneNode*, uint32_t> m_mPrimitiveDescStartingIndices;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

private:
    AccelerationStructure* BuildBLASfromNode(const SceneNode& geometry, VkBuildAccelerationStructureFlagsKHR flags);
    AccelerationStructure* BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& vInstances);
    uint32_t AlignUp(uint32_t nSize, uint32_t nAlignment);

    std::array<VkStridedDeviceAddressRegionKHR, SBT_REGION_COUNT> m_aSBTRegions;
};
