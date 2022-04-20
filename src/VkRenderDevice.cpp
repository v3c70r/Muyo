#include "VkRenderDevice.h"
#include "ResourceBarrier.h"

#include <cassert>
#include <set>

#include "Debug.h"
#include "RenderResourceManager.h"

#define DEBUG_DEVICE
#ifdef DEBUG_DEVICE
static VkDebugRenderDevice renderDevice;
#else
static VkRenderDevice renderDevice;
#endif

VkRenderDevice* GetRenderDevice()
{
    return &renderDevice;
}

void VkRenderDevice::Initialize(
    const std::vector<const char*>& vExtensionNames,
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
    createInfo.enabledExtensionCount = vExtensionNames.size();
    createInfo.ppEnabledExtensionNames = vExtensionNames.data();

    createInfo.enabledLayerCount = vLayerNames.size();
    createInfo.ppEnabledLayerNames = vLayerNames.data();

    assert(vkCreateInstance(&createInfo, nullptr, &m_instance) == VK_SUCCESS);

    // Create

    // Enumerate physical device supports these extensions and layers
    PickPhysicalDevice();
}

void VkRenderDevice::TransitImageLayout(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t nMipCount,
        uint32_t nLayerCount)
{
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    // DST -> SHADER READ ONLY
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    // UNDEFINED -> DEPTH_ATTACHMENT
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    // UNDEFINED -> COLOR_ATTACHMENT
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else
    {
        throw std::invalid_argument("unsupported layout transition!");
    }

    if (commandBuffer == VK_NULL_HANDLE)
    {
        ExecuteImmediateCommand(
            [&](VkCommandBuffer commandBuffer) {
                vkCmdPipelineBarrier(commandBuffer, sourceStage,
                                     /*srcStage*/ destinationStage, /*dstStage*/
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            });
    }
    else
    {
        vkCmdPipelineBarrier(commandBuffer, sourceStage,
                             /*srcStage*/ destinationStage, /*dstStage*/
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

void VkRenderDevice::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(GetRenderDevice()->GetInstance(), &deviceCount, nullptr);
    assert(deviceCount != 0);
    //assert(deviceCount == 1 && "Has more than 1 physical device, need compatibility check");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(GetRenderDevice()->GetInstance(), &deviceCount, devices.data());
    m_physicalDevice = devices[0];
}

void VkRenderDevice::CreateDevice(
    const std::vector<const char*>& vDeviceExtensions,
    const std::vector<const char*>& layers,
    const VkSurfaceKHR& surface,
    const std::vector<void*>& vpFeatures)    // surface for compatibility check
{
    // Find supported queue
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    // Find the first queue families support all the queues
    int nQueueFamilyIdx = 0;
    for (const auto& queueFamily : queueFamilies)
    {
        // Check for graphics support
        if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            m_queueFamilyIndices.nGraphicsQueueFamily = nQueueFamilyIdx;
        }

        // Check for presentation support ( they can be in the same queeu family)
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, nQueueFamilyIdx, surface, &presentSupport);

        if (queueFamily.queueCount > 0 && presentSupport)
        {
            m_queueFamilyIndices.nPresentQueneFamily = nQueueFamilyIdx;
        }

        if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            m_queueFamilyIndices.nComputeQueueFamily = nQueueFamilyIdx;
        }

        if (m_queueFamilyIndices.isComplete()) 
        {
            break;
        }

        nQueueFamilyIdx++;
    }

    assert(m_queueFamilyIndices.isComplete());

    struct ExtensionHeader  // Helper struct to link extensions together
    {
        VkStructureType sType;
        void* pNext;
    };

    VkPhysicalDeviceFeatures2 features2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features features11 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};

    features2.pNext = &features11;
    features11.pNext = &features12;
    
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
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &features2);
    }

    // Handle queue family indices and add them to the device creation info

    float fQueuePriority = 1.0f;
    // Create a queue for each of the family

    auto cmp = [](VkDeviceQueueCreateInfo info1, VkDeviceQueueCreateInfo info2)
    { return info1.queueFamilyIndex < info2.queueFamilyIndex; };
    std::set<VkDeviceQueueCreateInfo, decltype(cmp)> sQueueCreateInfos(cmp);

    sQueueCreateInfos.insert(VkDeviceQueueCreateInfo({
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,         // sType;
                nullptr,                                            // pNext;
                0,                                                  // flags;
                (uint32_t)m_queueFamilyIndices.nGraphicsQueueFamily,  // queueFamilyIndex;
                1,                                                  // queueCount;
                &fQueuePriority                                     // pQueuePriorities;
        }));

    sQueueCreateInfos.insert(VkDeviceQueueCreateInfo({
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,         // sType;
                nullptr,                                            // pNext;
                0,                                                  // flags;
                (uint32_t)m_queueFamilyIndices.nPresentQueneFamily,   // queueFamilyIndex;
                1,                                                  // queueCount;
                &fQueuePriority                                     // pQueuePriorities;
        }));

    sQueueCreateInfos.insert(VkDeviceQueueCreateInfo({
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,         // sType;
                nullptr,                                            // pNext;
                0,                                                  // flags;
                (uint32_t)m_queueFamilyIndices.nComputeQueueFamily,   // queueFamilyIndex;
                1,                                                  // queueCount;
                &fQueuePriority                                     // pQueuePriorities;
        }));

    std::vector<VkDeviceQueueCreateInfo> vQueueCreateInfos(sQueueCreateInfos.begin(), sQueueCreateInfos.end());

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = vQueueCreateInfos.data();
    createInfo.queueCreateInfoCount = (uint32_t)vQueueCreateInfos.size();

    createInfo.pEnabledFeatures = nullptr;
    createInfo.pNext = &features2;

    createInfo.enabledExtensionCount = vDeviceExtensions.size();
    createInfo.ppEnabledExtensionNames = vDeviceExtensions.data();

    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    assert(vkCreateDevice(GetRenderDevice()->GetPhysicalDevice(), &createInfo, nullptr, &m_device) == VK_SUCCESS);

    {

        vkGetDeviceQueue(m_device, m_queueFamilyIndices.nGraphicsQueueFamily, 0, &m_graphicsQueue);
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_graphicsQueue), VK_OBJECT_TYPE_QUEUE, "Graphics Queue");

        vkGetDeviceQueue(m_device, m_queueFamilyIndices.nPresentQueneFamily, 0, &m_presentQueue);
        if (m_presentQueue == m_graphicsQueue)
        {
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_presentQueue), VK_OBJECT_TYPE_QUEUE, "Graphics/Present Queue");
        }
        else
        {
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_presentQueue), VK_OBJECT_TYPE_QUEUE, "Present Queue");
        }

        vkGetDeviceQueue(m_device, m_queueFamilyIndices.nComputeQueueFamily, 0, &m_computeQueue);
        if (m_computeQueue == m_graphicsQueue)
        {
            if (m_computeQueue == m_presentQueue)
            {
                setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE, "Graphics/Present/Compute Queue");
            }
            else
            {
                setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE, "Graphics/Compute Queue");
            }
        }
        else if (m_computeQueue == m_presentQueue)
        {
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE, "Prsent/Compute Queue");
        }
        else
        {
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_computeQueue), VK_OBJECT_TYPE_QUEUE, "Compute Queue");
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
        assert(vkCreateCommandPool(m_device,
                                   &commandPoolInfo, nullptr,
                                   &m_aCommandPools[MAIN_CMD_POOL]) == VK_SUCCESS);
    }
    // transient pool
    {
        commandPoolInfo.queueFamilyIndex = m_queueFamilyIndices.nGraphicsQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        assert(vkCreateCommandPool(m_device,
                                   &commandPoolInfo, nullptr,
                                   &m_aCommandPools[IMMEDIATE_CMD_POOL]) == VK_SUCCESS);
    }
    // Reusable pool
    {
        commandPoolInfo.queueFamilyIndex = m_queueFamilyIndices.nGraphicsQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        assert(vkCreateCommandPool(m_device,
                                   &commandPoolInfo, nullptr,
                                   &m_aCommandPools[PER_FRAME_CMD_POOL]) == VK_SUCCESS);
    }

    // Compute pool
    {
        commandPoolInfo.queueFamilyIndex = m_queueFamilyIndices.nComputeQueueFamily;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        assert(vkCreateCommandPool(m_device,
                                   &commandPoolInfo, nullptr,
                                   &m_aCommandPools[COMPUTE_CMD_POOL]) == VK_SUCCESS);
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
    vkFreeCommandBuffers(m_device, m_aCommandPools[MAIN_CMD_POOL], 1,
                         &commandBuffer);
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
    assert(vkCreateSampler(m_device, &samplerInfo,
                           nullptr, &sampler) == VK_SUCCESS);
    return sampler;
}

void VkRenderDevice::AddResourceBarrier(VkCommandBuffer cmdBuf, IResourceBarrier &barrier)
{
    barrier.AddBarrier(cmdBuf);
}

VkCommandBuffer VkRenderDevice::AllocatePrimaryCommandbuffer(CommandPools pool)
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_aCommandPools[pool];
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    assert(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer) ==
           VK_SUCCESS);
    return commandBuffer;
}

void VkRenderDevice::FreePrimaryCommandbuffer(VkCommandBuffer& commandBuffer,
                                              CommandPools pool)
{
    vkFreeCommandBuffers(m_device, m_aCommandPools[pool], 1, &commandBuffer);
}

void VkRenderDevice::SubmitCommandBuffers(std::vector<VkCommandBuffer>& vCmdBuffers, VkQueue queue, std::vector<VkSemaphore> &waitSemaphores, std::vector<VkSemaphore> &signalSemaphores, std::vector<VkPipelineStageFlags> flags, VkFence signalFence)
{
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = flags.data();

    submitInfo.commandBufferCount = static_cast<uint32_t>(vCmdBuffers.size());
    submitInfo.pCommandBuffers = vCmdBuffers.data();

    submitInfo.signalSemaphoreCount = (uint32_t)signalSemaphores.size();
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    assert(vkQueueSubmit(queue, 1, &submitInfo, signalFence) == VK_SUCCESS);
}

void VkRenderDevice::SubmitCommandBuffersAndWait(std::vector<VkCommandBuffer>& vCmdBuffers)
{
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<uint32_t>(vCmdBuffers.size());
    submitInfo.pCommandBuffers = vCmdBuffers.data();

    assert(vkQueueSubmit(GetGraphicsQueue(), 1, &submitInfo, nullptr) == VK_SUCCESS);
    assert(vkQueueWaitIdle(GetGraphicsQueue()) == VK_SUCCESS);
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
VkPipelineLayout VkRenderDevice::CreatePipelineLayout(
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
        const std::vector<VkPushConstantRange>& pushConstantRanges)
    {
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = descriptorSetLayouts.size();
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

        if (pushConstantRanges.size() > 0)
        {
            pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();
            pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
        }

        assert(vkCreatePipelineLayout(m_device,
                                      &pipelineLayoutInfo, nullptr,
                                      &pipelineLayout) == VK_SUCCESS);
        return pipelineLayout;
    }

// Debug device 
//
void VkDebugRenderDevice::Initialize(
    const std::vector<const char*>& vExtensionNames,
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

void VkDebugRenderDevice::CreateDevice(const std::vector<const char*>& vExtensionNames, const std::vector<const char*>& vLayerNames, const VkSurfaceKHR& surface, const std::vector<void*>& vpFeatures)
{
    std::vector<const char*> vDebugLayerNames = vLayerNames;
    vDebugLayerNames.push_back(GetValidationLayerName());
    VkRenderDevice::CreateDevice(
        vExtensionNames, vDebugLayerNames, surface, vpFeatures);
}

