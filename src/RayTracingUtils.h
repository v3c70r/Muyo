#pragma once
#include <vulkan/vulkan.h>
#include <vector>
struct BLASInput
{
    std::vector<VkAccelerationStructureGeometryKHR> m_vGeometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_vRangeInfo;
};

class Scene;
// Convert scene to BLASInput
BLASInput ConstructBLASInput(const Scene& Scene);

// Construct BLAS GPU structure
void BuildBLAS(const std::vector<BLASInput>& vBLASInputs, VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

