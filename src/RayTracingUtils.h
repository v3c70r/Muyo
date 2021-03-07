#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#define ENABLE_RAY_TRACING
//#ifdef ENABLE_RAY_TRACINE
struct BLASInput
{
    std::vector<VkAccelerationStructureGeometryKHR> m_vGeometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_vRangeInfo;
};

class Scene;
BLASInput ConstructBLASInput(const Scene& Scene);
//#endif
