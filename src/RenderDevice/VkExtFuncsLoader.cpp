#include "VkExtFuncsLoader.h"
#include <vulkan/vulkan_core.h>

namespace Muyo
{

// Load extension functions
PFN_vkCreateAccelerationStructureKHR VkExt::vkCreateAccelerationStructureKHR = nullptr;
PFN_vkDestroyAccelerationStructureKHR VkExt::vkDestroyAccelerationStructureKHR = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR VkExt::vkCmdBuildAccelerationStructuresKHR = nullptr;
PFN_vkCmdWriteAccelerationStructuresPropertiesKHR VkExt::vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR VkExt::vkGetAccelerationStructureBuildSizesKHR = nullptr;
PFN_vkCmdCopyAccelerationStructureKHR VkExt::vkCmdCopyAccelerationStructureKHR = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR VkExt::vkGetAccelerationStructureDeviceAddressKHR = nullptr;
PFN_vkCreateRayTracingPipelinesKHR VkExt::vkCreateRayTracingPipelinesKHR = nullptr;
PFN_vkGetRayTracingShaderGroupHandlesKHR VkExt::vkGetRayTracingShaderGroupHandlesKHR = nullptr;
PFN_vkCmdTraceRaysKHR VkExt::vkCmdTraceRaysKHR = nullptr;
PFN_vkCmdPipelineBarrier2KHR VkExt::vkCmdPipelineBarrier2KHR = nullptr;
PFN_vkCmdDrawMeshTasksEXT VkExt::vkCmdDrawMeshTasksEXT = nullptr;
bool VkExt::bExtensionLoaded = false;

void VkExt::LoadInstanceFunctions(VkInstance instance)
{
    if (!bExtensionLoaded)
    {
#define GET_VK_INSTANCE_FUNC_EXT(FUNC_NAME) \
    FUNC_NAME = (PFN_##FUNC_NAME)vkGetInstanceProcAddr(instance, #FUNC_NAME);

        GET_VK_INSTANCE_FUNC_EXT(vkCreateAccelerationStructureKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkDestroyAccelerationStructureKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkCmdBuildAccelerationStructuresKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkCmdWriteAccelerationStructuresPropertiesKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkGetAccelerationStructureBuildSizesKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkCmdCopyAccelerationStructureKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkGetAccelerationStructureDeviceAddressKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkCreateRayTracingPipelinesKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkGetRayTracingShaderGroupHandlesKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkCmdTraceRaysKHR);
        GET_VK_INSTANCE_FUNC_EXT(vkCmdPipelineBarrier2KHR);
        GET_VK_INSTANCE_FUNC_EXT(vkCmdDrawMeshTasksEXT);

        bExtensionLoaded = true;
    }
}

}  // namespace Muyo
