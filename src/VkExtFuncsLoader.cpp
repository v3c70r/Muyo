#include "VkExtFuncsLoader.h"

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
bool VkExt::bExtensionLoaded = false;

void VkExt::LoadInstanceFunctions(VkInstance instance)
{
    if (!bExtensionLoaded)
    {
#define GET_VK_INSTANCE_FUNC_RT(FUNC_NAME) \
    FUNC_NAME = (PFN_##FUNC_NAME)vkGetInstanceProcAddr(instance, #FUNC_NAME);

        GET_VK_INSTANCE_FUNC_RT(vkCreateAccelerationStructureKHR);
        GET_VK_INSTANCE_FUNC_RT(vkDestroyAccelerationStructureKHR);
        GET_VK_INSTANCE_FUNC_RT(vkCmdBuildAccelerationStructuresKHR);
        GET_VK_INSTANCE_FUNC_RT(vkCmdWriteAccelerationStructuresPropertiesKHR);
        GET_VK_INSTANCE_FUNC_RT(vkGetAccelerationStructureBuildSizesKHR);
        GET_VK_INSTANCE_FUNC_RT(vkCmdCopyAccelerationStructureKHR);
        GET_VK_INSTANCE_FUNC_RT(vkGetAccelerationStructureDeviceAddressKHR);
        GET_VK_INSTANCE_FUNC_RT(vkCreateRayTracingPipelinesKHR);
        GET_VK_INSTANCE_FUNC_RT(vkGetRayTracingShaderGroupHandlesKHR);
        GET_VK_INSTANCE_FUNC_RT(vkCmdTraceRaysKHR);
        GET_VK_INSTANCE_FUNC_RT(vkCmdPipelineBarrier2KHR);

        bExtensionLoaded = true;
    }
}
