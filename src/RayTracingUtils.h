#pragma once
#include <vulkan/vulkan.h>
#include <vector>
struct BLASInput
{
    std::vector<VkAccelerationStructureGeometryKHR> m_vGeometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_vRangeInfo;
    VkAccelerationStructureKHR m_ac = VK_NULL_HANDLE;
    VkFlags m_flags = {};
};

class Scene;
// Convert scene to BLASInput
BLASInput ConstructBLASInput(const Scene& Scene);

class RTBuilder
{
public:
    // Construct BLAS GPU structure
    void BuildBLAS(const std::vector<BLASInput> &vBLASInputs, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

private:
    std::vector<BLASInput> m_blas;
};

