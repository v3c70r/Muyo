#pragma once
#include "Scene.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>

struct BLASInput
{
    std::vector<VkAccelerationStructureGeometryKHR> m_vGeometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_vRangeInfo;

    VkAccelerationStructureKHR m_ac = VK_NULL_HANDLE;
    VkFlags m_flags = {};
};

// This is an instance of a BLAS
struct Instance
{
    uint32_t blasId{0};      // Index of the BLAS in m_blas
    uint32_t instanceId{0};  // Instance Index (gl_InstanceID)
    uint32_t hitGroupId{0};  // Hit group index in the SBT
    uint32_t mask{0xFF};     // Visibility mask, will be AND-ed with ray mask
    VkGeometryInstanceFlagsNV flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
    glm::mat4 transform = glm::mat4(1.0);  // Identity
};

class Scene;
std::vector<BLASInput> ConstructBLASInputs(const DrawLists& dls);

class RTBuilder
{
public:
    RTBuilder();
    // Construct BLAS GPU structure
    void BuildBLAS(const std::vector<BLASInput> &vBLASInputs, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
    void BuildTLAS(const std::vector<Instance>& instances, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, bool bUpdate = false);
    void Cleanup(std::vector<BLASInput> &vBLASInputs);


private:
    std::vector<BLASInput> m_blas;

    // Raytracing functions
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = nullptr;
};

