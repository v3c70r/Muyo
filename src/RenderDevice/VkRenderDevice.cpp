#include "VkRenderDevice.h"

#include <cassert>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include "Debug.h"
#include "RenderResourceManager.h"
#include "ResourceBarrier.h"

namespace Muyo
{

#define DEBUG_DEVICE
#ifdef DEBUG_DEVICE
static VkDebugRenderDevice renderDevice;
#else
static VkRenderDevice renderDevice;
#endif

VkRenderDevice* GetRenderDevice() { return &renderDevice; }

void VkRenderDevice::Initialize(const std::vector<const char*>& vExtensionNames,
                                const std::vector<const char*>& vLayerNames)
{
    HWInfo info;
    for (const auto& slayerName : vLayerNames)
    {
        assert(info.IsLayerSupported(slayerName));
    }
    for (const auto& sInstanceExtensionName : vExtensionNames)
    {
        assert(info.IsInstanceExtensionSupported(sInstanceExtensionName));
    }

    // Create instance
    // set physical device
    // Populate application info structure
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Muyo";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 3, 0);
    appInfo.pEngineName = "Muyo";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 3, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(vExtensionNames.size());
    createInfo.ppEnabledExtensionNames = vExtensionNames.data();

    createInfo.enabledLayerCount = static_cast<uint32_t>(vLayerNames.size());
    createInfo.ppEnabledLayerNames = vLayerNames.data();

    VK_ASSERT(vkCreateInstance(&createInfo, nullptr, &m_instance));

    // Create

    // Enumerate physical device supports these extensions and layers
    PickPhysicalDevice();
}

void VkRenderDevice::TransitImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout,
                                        VkImageLayout newLayout, uint32_t nMipCount, uint32_t nLayerCount)
{
    VkPipelineStageFlags2 sourceStage;
    VkPipelineStageFlags2 destinationStage;

    VkImageMemoryBarrier2 barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = nMipCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = nLayerCount;

    // UNDEFINED -> DST
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    }
    // DST -> SHADER READ ONLY
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    // UNDEFINED -> DEPTH_ATTACHMENT
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    }
    // UNDEFINED -> COLOR_ATTACHMENT
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else
    {
        throw std::invalid_argument("unsupported layout transition!");
    }

    barrier.srcStageMask = sourceStage;
    barrier.dstStageMask = destinationStage;

    VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    if (commandBuffer == VK_NULL_HANDLE)
    {
        ExecuteImmediateCommand([&](VkCommandBuffer cmdBuf) { vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo); });
    }
    else
    {
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }
}

void VkRenderDevice::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(GetRenderDevice()->GetInstance(), &deviceCount, nullptr);
    assert(deviceCount != 0);
    // assert(deviceCount == 1 && "Has more than 1 physical device, need compatibility check");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(GetRenderDevice()->GetInstance(), &deviceCount, devices.data());
    m_physicalDevice = devices[0];
}

void VkRenderDevice::CreateDevice(const std::vector<const char*>& vDeviceExtensions,
                                  const std::vector<const char*>& layers,
                                  const VkSurfaceKHR* pSurface,  // surface for compatibility check
                                  const std::vector<void*>& vpFeatures)
{
    // Device layers were removed in Vulkan 1.0, and passing one fails device creation on a loader
    // that enforces VUID-VkDeviceCreateInfo-enabledLayerCount-12384 (Vulkan SDK 1.4.363 onwards).
    // Ignore any that a caller still supplies rather than forwarding them. Deliberately not an
    // assert: asserts compile out under NDEBUG, so the invariant would only hold in Debug builds,
    // and aborting a shipping build over a legacy argument is worse than dropping it. Validation
    // belongs to the *instance* layer list, which VkDebugRenderDevice::Initialize applies.
    if (!layers.empty())
    {
        std::cerr << "[WARNING]: VkRenderDevice::CreateDevice ignoring " << layers.size()
                  << " device layer(s): device layers were removed in Vulkan 1.0 and validation is an "
                     "instance layer."
                  << std::endl;
    }

    // Find supported queue
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    struct ExtensionHeader  // Helper struct to link extensions together
    {
        VkStructureType sType;
        void* pNext;
    };

    VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.features.multiDrawIndirect = VK_TRUE;
    // Required by shaders that use 64-bit integers / buffer references (pathTracing.rchit)
    // and by shaders that read/write storage images with unknown formats (testPrimary.rgen).
    features2.features.shaderInt64 = VK_TRUE;
    features2.features.shaderStorageImageReadWithoutFormat = VK_TRUE;
    features2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.maintenance4 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;
    // sync2 (core in 1.3) is the only synchronization API the engine uses.
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = VK_TRUE;
    // Timeline semaphores back the render graph's internal cross-queue synchronization.
    features12.timelineSemaphore = VK_TRUE;
    features12.separateDepthStencilLayouts = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.drawIndirectCount = VK_TRUE;
    // Shaders declare layout(scalar) / layout(buffer_reference, scalar); e.g. the packed
    // Vertex array in pathTracing.rchit has a 40-byte stride, which is only valid with
    // scalar block layout.
    features12.scalarBlockLayout = VK_TRUE;
    VkPhysicalDeviceVulkan11Features features11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features11.multiview = VK_TRUE;
    features11.shaderDrawParameters = VK_TRUE;

    features2.pNext = &features11;
    features11.pNext = &features12;
    features12.pNext = &features13;

    if (!vpFeatures.empty())
    {
        // build up chain of all used extension features
        for (size_t i = 0; i < vpFeatures.size(); i++)
        {
            auto* header = reinterpret_cast<ExtensionHeader*>(vpFeatures[i]);
            header->pNext = i < vpFeatures.size() - 1 ? vpFeatures[i + 1] : nullptr;
        }

        //// append to the end of current feature2 struct
        ExtensionHeader* lastCoreFeature = (ExtensionHeader*)&features2;
        while (lastCoreFeature->pNext != nullptr)
        {
            lastCoreFeature = (ExtensionHeader*)lastCoreFeature->pNext;
        }
        lastCoreFeature->pNext = vpFeatures[0];

        // query support
        // This will qury support for the chain
    }

    // Verify every core feature we are about to enable is actually supported. Query into a
    // dedicated chain: vkGetPhysicalDeviceFeatures2 overwrites the structs it is given with the
    // supported values, so querying the desired chain would silently enable whatever came back.
    VkPhysicalDeviceVulkan13Features supported13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features supported12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features supported11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 supported2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supported2.pNext = &supported11;
    supported11.pNext = &supported12;
    supported12.pNext = &supported13;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported2);

    const auto require = [](VkBool32 bSupported, const char* sName)
    {
        if (!bSupported)
        {
            throw std::runtime_error(std::string("Required device feature is not supported: ") + sName);
        }
    };
    require(supported2.features.multiDrawIndirect, "multiDrawIndirect");
    require(supported2.features.shaderInt64, "shaderInt64");
    require(supported2.features.shaderStorageImageReadWithoutFormat, "shaderStorageImageReadWithoutFormat");
    require(supported2.features.shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat");
    require(supported11.multiview, "multiview");
    require(supported11.shaderDrawParameters, "shaderDrawParameters");
    require(supported12.bufferDeviceAddress, "bufferDeviceAddress");
    require(supported12.separateDepthStencilLayouts, "separateDepthStencilLayouts");
    require(supported12.runtimeDescriptorArray, "runtimeDescriptorArray");
    require(supported12.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing");
    require(supported12.drawIndirectCount, "drawIndirectCount");
    require(supported12.scalarBlockLayout, "scalarBlockLayout");
    require(supported12.timelineSemaphore, "timelineSemaphore");
    require(supported13.maintenance4, "maintenance4");
    require(supported13.dynamicRendering, "dynamicRendering");
    require(supported13.synchronization2, "synchronization2");

    // Handle queue family indices and add them to the device creation info

    float fQueuePriority = 1.0f;
    // Create a queue for each of the family

    auto cmp = [](VkDeviceQueueCreateInfo info1, VkDeviceQueueCreateInfo info2)
    { return info1.queueFamilyIndex < info2.queueFamilyIndex; };
    std::set<VkDeviceQueueCreateInfo, decltype(cmp)> sQueueCreateInfos(cmp);

    // Find the first queue families support all the queues
    m_queueFamilyIndices.nGraphicsQueueFamily = -1;
    m_queueFamilyIndices.nPresentQueneFamily = -1;
    m_queueFamilyIndices.nComputeQueueFamily = -1;
    bool bComputeFamilyIsDedicated = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilies.size()); ++i)
    {
        const auto& queueFamily = queueFamilies[i];
        const bool bSupportsGraphics = queueFamily.queueCount > 0 && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT);
        const bool bSupportsCompute = queueFamily.queueCount > 0 && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT);

        // Check for graphics support
        if (bSupportsGraphics && m_queueFamilyIndices.nGraphicsQueueFamily < 0)
        {
            m_queueFamilyIndices.nGraphicsQueueFamily = static_cast<int>(i);
        }

        // Check for presentation support ( they can be in the same queeu family)
        if (pSurface && m_queueFamilyIndices.nPresentQueneFamily < 0)
        {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, *pSurface, &presentSupport);
            if (presentSupport)
            {
                m_queueFamilyIndices.nPresentQueneFamily = static_cast<int>(i);
            }
        }

        // Prefer a dedicated compute-only queue family so compute work can run asynchronously;
        // otherwise fall back to any compute-capable family (usually the graphics family).
        if (bSupportsCompute)
        {
            const bool bDedicated = !bSupportsGraphics;
            if (m_queueFamilyIndices.nComputeQueueFamily < 0 || (bDedicated && !bComputeFamilyIsDedicated))
            {
                m_queueFamilyIndices.nComputeQueueFamily = static_cast<int>(i);
                bComputeFamilyIsDedicated = bDedicated;
            }
        }
    }

    // We should at least have one graphics queue
    assert(m_queueFamilyIndices.nGraphicsQueueFamily >= 0);

    if (m_queueFamilyIndices.nGraphicsQueueFamily >= 0)
    {
        sQueueCreateInfos.insert(VkDeviceQueueCreateInfo({
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,           // sType;
            nullptr,                                              // pNext;
            0,                                                    // flags;
            (uint32_t)m_queueFamilyIndices.nGraphicsQueueFamily,  // queueFamilyIndex;
            1,                                                    // queueCount;
            &fQueuePriority                                       // pQueuePriorities;
        }));
    }

    if (m_queueFamilyIndices.nPresentQueneFamily >= 0)
    {
        sQueueCreateInfos.insert(VkDeviceQueueCreateInfo({
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,          // sType;
            nullptr,                                             // pNext;
            0,                                                   // flags;
            (uint32_t)m_queueFamilyIndices.nPresentQueneFamily,  // queueFamilyIndex;
            1,                                                   // queueCount;
            &fQueuePriority                                      // pQueuePriorities;
        }));
    }

    if (m_queueFamilyIndices.nComputeQueueFamily >= 0)
    {
        sQueueCreateInfos.insert(VkDeviceQueueCreateInfo({
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,          // sType;
            nullptr,                                             // pNext;
            0,                                                   // flags;
            (uint32_t)m_queueFamilyIndices.nComputeQueueFamily,  // queueFamilyIndex;
            1,                                                   // queueCount;
            &fQueuePriority                                      // pQueuePriorities;
        }));
    }

    // Make sure we have at least one queue
    assert(sQueueCreateInfos.size() > 0);

    std::vector<VkDeviceQueueCreateInfo> vQueueCreateInfos(sQueueCreateInfos.begin(), sQueueCreateInfos.end());

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = vQueueCreateInfos.data();
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(vQueueCreateInfos.size());

    createInfo.pEnabledFeatures = nullptr;
    createInfo.pNext = &features2;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(vDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = vDeviceExtensions.data();

    // Always zero: device layers are not a thing since Vulkan 1.0 (see above).
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    VK_ASSERT(vkCreateDevice(GetRenderDevice()->GetPhysicalDevice(), &createInfo, nullptr, &m_device));

    {
        assert(m_queueFamilyIndices.nGraphicsQueueFamily != -1);
        vkGetDeviceQueue(m_device, m_queueFamilyIndices.nGraphicsQueueFamily, 0, &m_graphicsQueue);
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_graphicsQueue), VK_OBJECT_TYPE_QUEUE, "Graphics Queue");

        if (m_queueFamilyIndices.nPresentQueneFamily != -1)
        {
            vkGetDeviceQueue(m_device, m_queueFamilyIndices.nPresentQueneFamily, 0, &m_presentQueue);
            if (m_presentQueue == m_graphicsQueue)
            {
                setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_presentQueue), VK_OBJECT_TYPE_QUEUE,
                                        "Graphics/Present Queue");
            }
            else
            {
                setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_presentQueue), VK_OBJECT_TYPE_QUEUE,
                                        "Present Queue");
            }
        }

        if (m_queueFamilyIndices.nComputeQueueFamily != -1)
        {
            vkGetDeviceQueue(m_device, m_queueFamilyIndices.nComputeQueueFamily, 0, &m_computeQueue);
            if (m_computeQueue == m_graphicsQueue)
            {
                if (m_computeQueue == m_presentQueue)
                {
                    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE,
                                            "Graphics/Present/Compute Queue");
                }
                else
                {
                    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE,
                                            "Graphics/Compute Queue");
                }
            }
            else if (m_computeQueue == m_presentQueue)
            {
                setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE,
                                        "Prsent/Compute Queue");
            }
            else
            {
                setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE,
                                        "Compute Queue");
            }
        }
    }
}

void VkRenderDevice::DestroyDevice()
{
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
}

void VkRenderDevice::CreateCommandPools()
{
    // I assumes that the queue family supports both graphcs and present
    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // Static comamand pools
    {
        commandPoolInfo.queueFamilyIndex = m_queueFamilyIndices.nGraphicsQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_ASSERT(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_aCommandPools[MAIN_CMD_POOL]));
    }
    // transient pool
    {
        commandPoolInfo.queueFamilyIndex = m_queueFamilyIndices.nGraphicsQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VK_ASSERT(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_aCommandPools[IMMEDIATE_CMD_POOL]));
    }
    // Reusable pool
    {
        commandPoolInfo.queueFamilyIndex = m_queueFamilyIndices.nGraphicsQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_ASSERT(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_aCommandPools[PER_FRAME_CMD_POOL]));
    }

    // Compute pool
    if (m_queueFamilyIndices.nComputeQueueFamily >= 0)
    {
        commandPoolInfo.queueFamilyIndex = m_queueFamilyIndices.nComputeQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_ASSERT(vkCreateCommandPool(m_device, &commandPoolInfo, nullptr, &m_aCommandPools[COMPUTE_CMD_POOL]));
    }
}

void VkRenderDevice::DestroyCommandPools()
{
    for (auto& cmdPool : m_aCommandPools)
    {
        vkDestroyCommandPool(m_device, cmdPool, nullptr);
    }
}

void VkRenderDevice::Unintialize()
{
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
}

VkCommandBuffer VkRenderDevice::AllocateComputeCommandBuffer()
{
    return AllocatePrimaryCommandbuffer(COMPUTE_CMD_POOL);
}

void VkRenderDevice::FreeComputeCommandBuffer(VkCommandBuffer& commandBuffer)
{
    FreePrimaryCommandbuffer(commandBuffer, COMPUTE_CMD_POOL);
}

VkCommandBuffer VkRenderDevice::AllocateStaticPrimaryCommandbuffer()
{
    return AllocatePrimaryCommandbuffer(MAIN_CMD_POOL);
}

void VkRenderDevice::FreeStaticPrimaryCommandbuffer(VkCommandBuffer& commandBuffer)
{
    FreePrimaryCommandbuffer(commandBuffer, MAIN_CMD_POOL);
}

VkCommandBuffer VkRenderDevice::AllocateReusablePrimaryCommandbuffer()
{
    return AllocatePrimaryCommandbuffer(PER_FRAME_CMD_POOL);
}

void VkRenderDevice::FreeReusablePrimaryCommandbuffer(VkCommandBuffer& commandBuffer)
{
    FreePrimaryCommandbuffer(commandBuffer, PER_FRAME_CMD_POOL);
}

VkCommandBuffer VkRenderDevice::AllocateImmediateCommandBuffer()
{
    return AllocatePrimaryCommandbuffer(IMMEDIATE_CMD_POOL);
}

void VkRenderDevice::FreeImmediateCommandBuffer(VkCommandBuffer& commandBuffer)
{
    FreePrimaryCommandbuffer(commandBuffer, IMMEDIATE_CMD_POOL);
}

VkCommandBuffer VkRenderDevice::AllocateSecondaryCommandBuffer()
{
    // TODO: Implement this
    return VkCommandBuffer();
}
void VkRenderDevice::FreeSecondaryCommandBuffer(VkCommandBuffer& commandBuffer)
{
    vkFreeCommandBuffers(m_device, m_aCommandPools[MAIN_CMD_POOL], 1, &commandBuffer);
}

// Helper functions
VkSampler VkRenderDevice::CreateSampler()
{
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    // Wrapping
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    // Anistropic filter
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    // Choose of [0, width] or [0, 1]
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    // Used for percentage-closer filter for shadow
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    // mipmaps
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    VkSampler sampler = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler));
    return sampler;
}

void VkRenderDevice::AddResourceBarrier(VkCommandBuffer cmdBuf, IResourceBarrier& barrier)
{
    barrier.AddToCommandBuffer(cmdBuf);
}

VkCommandBuffer VkRenderDevice::AllocatePrimaryCommandbuffer(CommandPools pool)
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_aCommandPools[pool];
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VK_ASSERT(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer));
    return commandBuffer;
}

void VkRenderDevice::FreePrimaryCommandbuffer(VkCommandBuffer& commandBuffer, CommandPools pool)
{
    vkFreeCommandBuffers(m_device, m_aCommandPools[pool], 1, &commandBuffer);
}

void VkRenderDevice::SubmitCommandBuffers(std::vector<VkCommandBuffer>& vCmdBuffers, VkQueue queue,
                                          std::vector<VkSemaphore>& waitSemaphores,
                                          std::vector<VkSemaphore>& signalSemaphores,
                                          std::vector<VkPipelineStageFlags2> flags, VkFence signalFence)
{
    // sync2: the wait stage is attached to each semaphore instead of to the submit.
    std::vector<VkSemaphoreSubmitInfo> waitSemaphoreInfos(waitSemaphores.size());
    for (size_t i = 0; i < waitSemaphores.size(); ++i)
    {
        waitSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSemaphoreInfos[i].semaphore = waitSemaphores[i];
        waitSemaphoreInfos[i].value = 0;  // ignored for binary semaphores
        waitSemaphoreInfos[i].stageMask = i < flags.size() ? flags[i] : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitSemaphoreInfos[i].deviceIndex = 0;
    }

    std::vector<VkSemaphoreSubmitInfo> signalSemaphoreInfos(signalSemaphores.size());
    for (size_t i = 0; i < signalSemaphores.size(); ++i)
    {
        signalSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalSemaphoreInfos[i].semaphore = signalSemaphores[i];
        signalSemaphoreInfos[i].value = 0;  // ignored for binary semaphores
        // A binary signal needs a non-NONE stage; the value carries no meaning for binaries.
        signalSemaphoreInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalSemaphoreInfos[i].deviceIndex = 0;
    }

    std::vector<VkCommandBufferSubmitInfo> commandBufferInfos(vCmdBuffers.size());
    for (size_t i = 0; i < vCmdBuffers.size(); ++i)
    {
        commandBufferInfos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfos[i].commandBuffer = vCmdBuffers[i];
        commandBufferInfos[i].deviceMask = 0;
    }

    VkSubmitInfo2 submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitSemaphoreInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitSemaphoreInfos.empty() ? nullptr : waitSemaphoreInfos.data();
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandBufferInfos.size());
    submitInfo.pCommandBufferInfos = commandBufferInfos.empty() ? nullptr : commandBufferInfos.data();
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalSemaphoreInfos.empty() ? nullptr : signalSemaphoreInfos.data();

    VK_ASSERT(vkQueueSubmit2(queue, 1, &submitInfo, signalFence));
}

void VkRenderDevice::SubmitCommandBuffersAndWait(std::vector<VkCommandBuffer>& vCmdBuffers)
{
    std::vector<VkCommandBufferSubmitInfo> commandBufferInfos(vCmdBuffers.size());
    for (size_t i = 0; i < vCmdBuffers.size(); ++i)
    {
        commandBufferInfos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfos[i].commandBuffer = vCmdBuffers[i];
        commandBufferInfos[i].deviceMask = 0;
    }

    VkSubmitInfo2 submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandBufferInfos.size());
    submitInfo.pCommandBufferInfos = commandBufferInfos.empty() ? nullptr : commandBufferInfos.data();

    VK_ASSERT(vkQueueSubmit2(GetGraphicsQueue(), 1, &submitInfo, nullptr));
    VK_ASSERT(vkQueueWaitIdle(GetGraphicsQueue()));
}

VkDeviceAddress VkRenderDevice::GetBufferDeviceAddress(VkBuffer buffer) const
{
    VkBufferDeviceAddressInfo addInfo = {};
    addInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addInfo.pNext = nullptr;
    addInfo.buffer = buffer;
    VkDeviceAddress deviceAddress = vkGetBufferDeviceAddress(m_device, &addInfo);
    return deviceAddress;
}
VkPipelineLayout VkRenderDevice::CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
                                                      const std::vector<VkPushConstantRange>& pushConstantRanges)
{
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

    if (pushConstantRanges.size() > 0)
    {
        pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
    }

    VK_ASSERT(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
    return pipelineLayout;
}

// Debug device
//
void VkDebugRenderDevice::Initialize(const std::vector<const char*>& vExtensionNames,
                                     const std::vector<const char*>& vLayerNames)
{
    // Append debug extension and layer names to the device
    std::vector<const char*> vDebugExtNames = vExtensionNames;
    vDebugExtNames.push_back(GetValidationExtensionName());

    std::vector<const char*> vDebugLayerNames = vLayerNames;
    vDebugLayerNames.push_back(GetValidationLayerName());

    VkRenderDevice::Initialize(vDebugExtNames, vDebugLayerNames);
    m_debugMessenger.Initialize(m_instance);
}

void VkDebugRenderDevice::Unintialize()
{
    m_debugMessenger.Uninitialize(m_instance);
    VkRenderDevice::Unintialize();
}

void VkDebugRenderDevice::CreateDevice(const std::vector<const char*>& vExtensionNames,
                                       const std::vector<const char*>& /*vLayerNames*/, const VkSurfaceKHR* pSurface,
                                       const std::vector<void*>& vpFeatures)
{
    // Validation is an *instance* layer and is already applied by VkDebugRenderDevice::Initialize.
    // Device layers were removed in Vulkan 1.0, so passing the validation layer here fails device
    // creation on any loader that enforces VUID-VkDeviceCreateInfo-enabledLayerCount-12384 (Vulkan
    // SDK 1.4.363 onwards) - which aborts before a single test runs.
    VkRenderDevice::CreateDevice(vExtensionNames, {}, pSurface, vpFeatures);
}

}  // namespace Muyo
