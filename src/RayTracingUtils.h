#pragma once
#include "Scene.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

// TODO: Put dedicated acceleration structures into resource manager
struct AccelerationStructure
{
    VkAccelerationStructureKHR m_ac = VK_NULL_HANDLE;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
};
struct BLASInput
{
    std::vector<VkAccelerationStructureGeometryKHR> m_vGeometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_vRangeInfo;
    // Buffer is managed by resource manager

    VkAccelerationStructureKHR m_ac = VK_NULL_HANDLE;
    VkBuildAccelerationStructureFlagsKHR m_flags = 0;
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

struct RTInputs
{
    std::vector<BLASInput> BLASs;
    std::vector<Instance> TLASs;
};

class Scene;

RTInputs ConstructRTInputsFromDrawLists(const DrawLists& dls);

class RTBuilder
{
public:
    RTBuilder();
    // Construct BLAS GPU structure
    void BuildBLAS(const std::vector<BLASInput> &vBLASInputs, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
    void BuildTLAS(const std::vector<Instance>& instances, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, bool bUpdate = false);
    void BuildRTPipeline();
    void Cleanup();
    VkAccelerationStructureKHR GetTLAS() const { return m_tlas.m_ac; }

private:

    VkAccelerationStructureInstanceKHR TLASInputToVkGeometryInstance(const Instance& instance);

    std::vector<BLASInput> m_blas;
    AccelerationStructure m_tlas;

    // Ray Tracing Pipelines
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    struct RTLightingPushConstantBlock
    {
        glm::vec4 vClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 vLightPosition = {0.0f, 0.0f, 1.0f};
        float fLightIntensity = 0.0f;
        int nLightType = 0;
    };

    // Raytracing functions
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
};

