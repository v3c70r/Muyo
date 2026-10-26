#pragma once

#include <vulkan/vulkan.h>

namespace Muyo
{
/// Extension entry points used by the engine.
///
/// Instance-level commands are resolved with `vkGetInstanceProcAddr` and device-level commands with
/// `vkGetDeviceProcAddr`; the split matters because a loader may return a non-null instance trampoline
/// for a device command that is not available on the selected device, and because the heap extension's
/// device commands are what B0 has to prove. `LoadDeviceFunctions` is called from
/// `VkRenderDevice::CreateDevice` after `vkCreateDevice`.
class VkExt
{
public:
    static PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    static PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    static PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    static PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR;
    static PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    static PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR;
    static PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    static PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
    static PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    static PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    static PFN_vkCmdPipelineBarrier2KHR vkCmdPipelineBarrier2KHR;
    static PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;

    // VK_EXT_descriptor_heap. The physical-device query is instance-level; the rest are device-level.
    static PFN_vkGetPhysicalDeviceDescriptorSizeEXT vkGetPhysicalDeviceDescriptorSizeEXT;
    static PFN_vkWriteResourceDescriptorsEXT vkWriteResourceDescriptorsEXT;
    static PFN_vkWriteSamplerDescriptorsEXT vkWriteSamplerDescriptorsEXT;
    static PFN_vkCmdBindResourceHeapEXT vkCmdBindResourceHeapEXT;
    static PFN_vkCmdBindSamplerHeapEXT vkCmdBindSamplerHeapEXT;
    static PFN_vkCmdPushDataEXT vkCmdPushDataEXT;

    static bool bExtensionLoaded;

    /// Resolve the instance-level extension entry points.
    ///
    /// May be called more than once (a test host re-creates the instance per fixture); the most recent
    /// instance wins. `bExtensionLoaded` records that the call succeeded at least once.
    static void LoadInstanceFunctions(VkInstance instance);

    /// Resolve the device-level extension entry points for `device`.
    ///
    /// Called by `VkRenderDevice::CreateDevice` so callers do not have to know which commands are device
    /// commands. Pointers stay null when the extension is not supported; callers check the feature bit
    /// before using them.
    static void LoadDeviceFunctions(VkDevice device);
};

}  // namespace Muyo
