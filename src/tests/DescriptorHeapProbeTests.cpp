// B0 descriptor-heap probe.
//
// The probe is the go/no-go for docs/DescriptorHeap-Bindless-Design.md: it creates one resource heap and
// one sampler heap, writes an image, a texel buffer, a structured buffer, a sampler and a real TLAS
// descriptor with the implementation's queried sizes, binds the heaps, records vkCmdPushDataEXT, and reads
// values through function-local .Handle() calls in null-layout heap pipelines. It creates no
// VkDescriptorSetLayout, VkDescriptorSet, descriptor pool or VkPipelineLayout.
//
// The AS representation is provisional by design: Slang's heap lowering loads an 8-byte value for a
// RaytracingAccelerationStructure, while RADV reports a 16-byte acceleration-structure descriptor. The
// test records both and only requires the direct trace where the sizes agree (llvmpipe); on a mismatch it
// records the single-slot attempt and leaves the mapping/reject decision to B1a.

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DescriptorHeapService.h"
#include "GraphicsTestEnv.h"
#include "ShaderAsset.h"
#include "VkExtFuncsLoader.h"
#include "VkRenderDevice.h"

namespace
{
using Muyo::VK_ASSERT;
using Muyo::VkExt;
using Muyo::VkRenderDevice;

constexpr VkDeviceSize kProbeBufferSize = 16;
#ifdef FEATURE_RAY_TRACING
VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    if (alignment == 0) return value;
    return (value + alignment - 1) / alignment * alignment;
}
#endif
constexpr uint32_t kProbeWidth = 1;
constexpr uint32_t kProbeHeight = 1;

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeBits & (1U << i)) != 0 && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("descriptor-heap probe: no memory type satisfies the requested properties");
}

struct BufferAllocation
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    VkDeviceSize size = 0;
    void *mapped = nullptr;

    void Destroy(VkDevice device)
    {
        if (mapped != nullptr)
        {
            vkUnmapMemory(device, memory);
            mapped = nullptr;
        }
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }
};

BufferAllocation CreateBuffer(VkRenderDevice *device, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags memoryProperties, bool map)
{
    BufferAllocation result;
    result.size = size;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_ASSERT(vkCreateBuffer(device->GetDevice(), &bufferInfo, nullptr, &result.buffer));

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device->GetDevice(), result.buffer, &requirements);

    VkMemoryAllocateFlagsInfo allocateFlags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
    {
        allocateFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }
    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0 ? &allocateFlags : nullptr;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        FindMemoryType(device->GetPhysicalDevice(), requirements.memoryTypeBits, memoryProperties);
    VK_ASSERT(vkAllocateMemory(device->GetDevice(), &allocateInfo, nullptr, &result.memory));
    VK_ASSERT(vkBindBufferMemory(device->GetDevice(), result.buffer, result.memory, 0));

    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
    {
        result.address = device->GetBufferDeviceAddress(result.buffer);
    }
    if (map)
    {
        VK_ASSERT(vkMapMemory(device->GetDevice(), result.memory, 0, VK_WHOLE_SIZE, 0, &result.mapped));
    }
    return result;
}

struct ImageAllocation
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};

    void Destroy(VkDevice device)
    {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
        if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
        view = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }
};

ImageAllocation CreateImage(VkRenderDevice *device, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                            VkMemoryPropertyFlags memoryProperties)
{
    ImageAllocation result;
    result.format = format;
    result.extent = extent;

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_ASSERT(vkCreateImage(device->GetDevice(), &imageInfo, nullptr, &result.image));

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device->GetDevice(), result.image, &requirements);
    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        FindMemoryType(device->GetPhysicalDevice(), requirements.memoryTypeBits, memoryProperties);
    VK_ASSERT(vkAllocateMemory(device->GetDevice(), &allocateInfo, nullptr, &result.memory));
    VK_ASSERT(vkBindImageMemory(device->GetDevice(), result.image, result.memory, 0));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY};
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_ASSERT(vkCreateImageView(device->GetDevice(), &viewInfo, nullptr, &result.view));
    return result;
}

VkShaderModule LoadHeapShader(VkDevice device, const char *name)
{
    const std::string path = std::string("shaders/") + name;
    Muyo::SpirvCode spirv = Muyo::ReadSpv(path);
    if (spirv.empty())
    {
        throw std::runtime_error("descriptor-heap probe: missing shader " + path +
                                 " (rebuild the Shaders target after reconfiguring)");
    }

    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateShaderModule(device, &createInfo, nullptr, &module));
    return module;
}

VkPipeline CreateComputePipeline(VkDevice device, VkShaderModule module)
{
    VkPipelineCreateFlags2CreateInfo flags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
    flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkComputePipelineCreateInfo createInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    createInfo.pNext = &flags2;
    createInfo.stage = stage;
    createInfo.layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
    return pipeline;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, VkShaderModule vertexModule, VkShaderModule fragmentModule,
                                  VkFormat colorFormat)
{
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                                  VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main", nullptr},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                                  VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main", nullptr}};

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    VkPipelineCreateFlags2CreateInfo flags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
    flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
    flags2.pNext = &renderingInfo;

    VkGraphicsPipelineCreateInfo createInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    createInfo.pNext = &flags2;
    createInfo.stageCount = 2;
    createInfo.pStages = stages;
    createInfo.pVertexInputState = &vertexInput;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState = &viewportState;
    createInfo.pRasterizationState = &rasterization;
    createInfo.pMultisampleState = &multisample;
    createInfo.pColorBlendState = &colorBlend;
    createInfo.pDynamicState = &dynamicState;
    createInfo.layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline));
    return pipeline;
}

struct PushCompute
{
    uint32_t inputIndex = 0;
    uint32_t outputIndex = 0;
    uint32_t texelIndex = 0;
    uint32_t addend = 0;
};

struct PushGraphics
{
    uint32_t imageIndex = 0;
    uint32_t samplerIndex = 0;
};

void WritePushData(VkCommandBuffer commandBuffer, const void *data, size_t size)
{
    VkPushDataInfoEXT pushInfo{VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT};
    pushInfo.offset = 0;
    pushInfo.data = {data, size};
    VkExt::vkCmdPushDataEXT(commandBuffer, &pushInfo);
}

void TransitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess, VkPipelineStageFlags2 srcStage,
                     VkPipelineStageFlags2 dstStage)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}
}  // namespace

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "DescriptorHeap: the device reports the heap feature and descriptor sizes",
                 "[DescriptorHeap]")
{
    Muyo::VkRenderDevice *device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapSupported())
    {
        SKIP(
            "VK_EXT_descriptor_heap + shaderUntypedPointers are not available on this device; B0 records the "
            "row as a gap, not a pass");
    }
    REQUIRE(device->IsDescriptorHeapEnabled());
    REQUIRE(device->IsShaderUntypedPointersEnabled());

    const VkPhysicalDeviceDescriptorHeapPropertiesEXT &properties = device->GetDescriptorHeapProperties();
    INFO("image size/alignment: " << properties.imageDescriptorSize << "/" << properties.imageDescriptorAlignment);
    INFO("buffer size/alignment: " << properties.bufferDescriptorSize << "/" << properties.bufferDescriptorAlignment);
    INFO("sampler size/alignment: " << properties.samplerDescriptorSize << "/"
                                    << properties.samplerDescriptorAlignment);
    INFO("maxPushDataSize: " << properties.maxPushDataSize);
    INFO("maxResourceHeapSize: " << properties.maxResourceHeapSize);
    INFO("maxSamplerHeapSize: " << properties.maxSamplerHeapSize);
    INFO("RT descriptor size: " << device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR));

    VkPhysicalDeviceDescriptorHeapFeaturesEXT features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 featureQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    featureQuery.pNext = &features;
    vkGetPhysicalDeviceFeatures2(device->GetPhysicalDevice(), &featureQuery);
    // This line, not an INFO, is what a passing run records: the B0 report needs the limits even when
    // nothing fails, and CI's summary greps it beside the [test device] line.
    std::cerr << "[descriptor heap] maxResourceHeapSize=" << properties.maxResourceHeapSize
              << " maxSamplerHeapSize=" << properties.maxSamplerHeapSize
              << " minResourceReserved=" << properties.minResourceHeapReservedRange
              << " minSamplerReserved=" << properties.minSamplerHeapReservedRange
              << " alignments(resource/sampler/image/buffer/samplerDesc)=" << properties.resourceHeapAlignment << "/"
              << properties.samplerHeapAlignment << "/" << properties.imageDescriptorAlignment << "/"
              << properties.bufferDescriptorAlignment << "/" << properties.samplerDescriptorAlignment
              << " captureReplay=" << (features.descriptorHeapCaptureReplay == VK_TRUE ? "true" : "false") << std::endl;

    REQUIRE(properties.imageDescriptorSize > 0);
    REQUIRE(properties.bufferDescriptorSize > 0);
    REQUIRE(properties.samplerDescriptorSize > 0);
    REQUIRE(properties.maxPushDataSize >= sizeof(PushCompute));
    REQUIRE(device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) > 0);
    REQUIRE(device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) > 0);
    REQUIRE(device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER) > 0);
}

TEST_CASE("DescriptorHeap: positive .heap.spv modules carry no binding or descriptor-set decorations",
          "[DescriptorHeap]")
{
    // The shader half of the B0 gate. A module-scope `.Handle()` compiles but lowers to an ordinary
    // parameter binding; on a null-layout heap pipeline that is a binding against a layout that does not
    // exist. Checking the emitted words is the negative assertion the design requires, and it runs without
    // a device so CI covers it even where the heap feature is absent.
    // Explicit rather than a glob: the negative control in docs/DescriptorHeap-Probes is deliberately
    // excluded, and each new positive graph module is added with its converter.
    const std::array<const char *, 10> modules = {
        "shaders/heapProbe.comp.heap.spv",     "shaders/heapProbe.frag.heap.spv",
        "shaders/heapProbe.vert.heap.spv",     "shaders/heapProbe.rgen.heap.spv",
        "shaders/heapProbe.rchit.heap.spv",    "shaders/heapProbe.rmiss.heap.spv",
        "shaders/graphHeapCopy.comp.heap.spv", "shaders/ui.frag.heap.spv",
        "shaders/testFillColor.comp.heap.spv", "shaders/prepareDrawCmdBuffer.comp.heap.spv"};
    constexpr uint32_t kMagic = 0x07230203;
    constexpr uint16_t kOpExtension = 10;
    constexpr uint16_t kOpDecorate = 71;
    constexpr uint16_t kOpDecorateId = 332;
    constexpr uint32_t kDecorationBinding = 33;
    constexpr uint32_t kDecorationDescriptorSet = 34;

    for (const char *module : modules)
    {
        INFO("module: " << module);
        const Muyo::SpirvCode words = Muyo::ReadSpv(module);
        REQUIRE(words.size() > 5);
        REQUIRE(words[0] == kMagic);

        bool hasHeapExtension = false;
        for (size_t index = 5; index < words.size();)
        {
            const uint32_t instruction = words[index];
            const uint32_t wordCount = instruction >> 16;
            const uint16_t opcode = static_cast<uint16_t>(instruction & 0xFFFFU);
            if (wordCount == 0 || index + wordCount > words.size())
            {
                FAIL("malformed SPIR-V instruction in " << module);
            }
            if (opcode == kOpExtension)
            {
                const auto *name = reinterpret_cast<const char *>(&words[index + 1]);
                hasHeapExtension = hasHeapExtension || std::strcmp(name, "SPV_EXT_descriptor_heap") == 0;
            }
            else if ((opcode == kOpDecorate || opcode == kOpDecorateId) && wordCount >= 3)
            {
                REQUIRE(words[index + 2] != kDecorationBinding);
                REQUIRE(words[index + 2] != kDecorationDescriptorSet);
            }
            index += wordCount;
        }
        // The vertex, closest-hit and miss stages use no descriptor, so their modules carry no heap
        // extension. The stages that do use a heap handle must actually emit it.
        const bool bExpectsHeap = std::strstr(module, "comp") != nullptr || std::strstr(module, "frag") != nullptr ||
                                  std::strstr(module, "rgen") != nullptr;
        if (bExpectsHeap) REQUIRE(hasHeapExtension);
    }
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv,
                 "DescriptorHeap: compute and graphics read image, texel, buffer and sampler through push data",
                 "[DescriptorHeap]")
{
    Muyo::VkRenderDevice *device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B0 records the row as a gap, not a pass");
    }

    Muyo::DescriptorHeapService heap;
    heap.Initialize(device);

    BufferAllocation input = CreateBuffer(
        device, kProbeBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    *static_cast<uint32_t *>(input.mapped) = 1;
    BufferAllocation output = CreateBuffer(
        device, kProbeBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    *static_cast<uint32_t *>(output.mapped) = 0;

    VkDeviceAddressRangeEXT inputRange{input.address, input.size};
    VkResourceDescriptorInfoEXT inputDescriptor{};
    inputDescriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    inputDescriptor.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    inputDescriptor.data.pAddressRange = &inputRange;
    const uint32_t inputIndex = heap.AcquireResource(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, inputDescriptor).index;

    VkDeviceAddressRangeEXT outputRange{output.address, output.size};
    VkResourceDescriptorInfoEXT outputDescriptor{};
    outputDescriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    outputDescriptor.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    outputDescriptor.data.pAddressRange = &outputRange;
    const uint32_t outputIndex = heap.AcquireResource(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, outputDescriptor).index;

    // Write the texel descriptor first so its index is 0: this isolates a stride/offset problem from a
    // driver/descriptor-type problem when the probe fails.
    BufferAllocation texel = CreateBuffer(
        device, kProbeBufferSize, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    *static_cast<uint32_t *>(texel.mapped) = 2;
    VkTexelBufferDescriptorInfoEXT texelInfo{};
    texelInfo.sType = VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT;
    texelInfo.format = VK_FORMAT_R32_UINT;
    texelInfo.addressRange = {texel.address, texel.size};
    VkResourceDescriptorInfoEXT texelDescriptor{};
    texelDescriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    texelDescriptor.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    texelDescriptor.data.pTexelBuffer = &texelInfo;
    const uint32_t texelIndex = heap.AcquireResource(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, texelDescriptor).index;

    ImageAllocation inputImage =
        CreateImage(device, {kProbeWidth, kProbeHeight}, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkImageViewCreateInfo inputViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    inputViewInfo.image = inputImage.image;
    inputViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    inputViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    inputViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageDescriptorInfoEXT imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
    imageInfo.pView = &inputViewInfo;
    imageInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
    VkResourceDescriptorInfoEXT imageDescriptor{};
    imageDescriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    imageDescriptor.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    imageDescriptor.data.pImage = &imageInfo;
    const uint32_t imageIndex = heap.AcquireResource(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, imageDescriptor).index;

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.maxLod = 0.0F;
    const uint32_t samplerIndex = heap.AcquireSampler(samplerInfo).index;

    ImageAllocation targetImage = CreateImage(device, {kProbeWidth, kProbeHeight}, VK_FORMAT_R32G32B32A32_SFLOAT,
                                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    BufferAllocation readback =
        CreateBuffer(device, static_cast<VkDeviceSize>(kProbeWidth) * kProbeHeight * 4 * sizeof(float),
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);

    VkShaderModule computeModule = LoadHeapShader(device->GetDevice(), "heapProbe.comp.heap.spv");
    VkShaderModule vertexModule = LoadHeapShader(device->GetDevice(), "heapProbe.vert.heap.spv");
    VkShaderModule fragmentModule = LoadHeapShader(device->GetDevice(), "heapProbe.frag.heap.spv");
    VkPipeline computePipeline = CreateComputePipeline(device->GetDevice(), computeModule);
    VkPipeline graphicsPipeline =
        CreateGraphicsPipeline(device->GetDevice(), vertexModule, fragmentModule, targetImage.format);

    VkCommandBuffer commandBuffer = device->AllocateStaticPrimaryCommandbuffer();
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_ASSERT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    // The input image is cleared in GENERAL, the canonical heap layout (D8). The clear is a transfer
    // operation, so the descriptor's GENERAL layout is also the layout the copy leaves.
    TransitionImage(commandBuffer, inputImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    VkClearColorValue clearColor{};
    clearColor.float32[0] = 0.25F;
    clearColor.float32[1] = 0.5F;
    clearColor.float32[2] = 0.75F;
    clearColor.float32[3] = 1.0F;
    VkImageSubresourceRange imageRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(commandBuffer, inputImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &imageRange);
    TransitionImage(commandBuffer, inputImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    TransitionImage(commandBuffer, targetImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    heap.Upload(commandBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    PushCompute pushCompute{};
    pushCompute.inputIndex = inputIndex;
    pushCompute.outputIndex = outputIndex;
    pushCompute.texelIndex = texelIndex;
    pushCompute.addend = 5;
    WritePushData(commandBuffer, &pushCompute, sizeof(pushCompute));
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    VkBufferMemoryBarrier2 outputBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    outputBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    outputBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    outputBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    outputBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    outputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputBarrier.buffer = output.buffer;
    outputBarrier.offset = 0;
    outputBarrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo outputDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    outputDependency.bufferMemoryBarrierCount = 1;
    outputDependency.pBufferMemoryBarriers = &outputBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &outputDependency);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = targetImage.view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearColorValue targetClear{};
    targetClear.float32[3] = 1.0F;
    colorAttachment.clearValue.color = targetClear;
    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, {kProbeWidth, kProbeHeight}};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    const VkViewport viewport{0.0F, 0.0F, static_cast<float>(kProbeWidth), static_cast<float>(kProbeHeight),
                              0.0F, 1.0F};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, {kProbeWidth, kProbeHeight}};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    PushGraphics pushGraphics{};
    pushGraphics.imageIndex = imageIndex;
    pushGraphics.samplerIndex = samplerIndex;
    WritePushData(commandBuffer, &pushGraphics, sizeof(pushGraphics));
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(commandBuffer);

    TransitionImage(commandBuffer, targetImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {kProbeWidth, kProbeHeight, 1};
    vkCmdCopyImageToBuffer(commandBuffer, targetImage.image, VK_IMAGE_LAYOUT_GENERAL, readback.buffer, 1, &copyRegion);

    VK_ASSERT(vkEndCommandBuffer(commandBuffer));
    std::vector<VkCommandBuffer> commandBuffers{commandBuffer};
    device->SubmitCommandBuffersAndWait(commandBuffers);
    device->FreeStaticPrimaryCommandbuffer(commandBuffer);

    REQUIRE(*static_cast<uint32_t *>(output.mapped) == 8U);
    const auto *pixels = static_cast<const float *>(readback.mapped);
    REQUIRE(std::abs(pixels[0] - 0.5F) < 0.01F);
    REQUIRE(std::abs(pixels[1] - 1.0F) < 0.01F);
    REQUIRE(std::abs(pixels[2] - 1.5F) < 0.01F);
    REQUIRE(std::abs(pixels[3] - 2.0F) < 0.01F);

    vkDestroyPipeline(device->GetDevice(), graphicsPipeline, nullptr);
    vkDestroyPipeline(device->GetDevice(), computePipeline, nullptr);
    vkDestroyShaderModule(device->GetDevice(), fragmentModule, nullptr);
    vkDestroyShaderModule(device->GetDevice(), vertexModule, nullptr);
    vkDestroyShaderModule(device->GetDevice(), computeModule, nullptr);
    readback.Destroy(device->GetDevice());
    targetImage.Destroy(device->GetDevice());
    inputImage.Destroy(device->GetDevice());
    texel.Destroy(device->GetDevice());
    output.Destroy(device->GetDevice());
    input.Destroy(device->GetDevice());
}

#ifdef FEATURE_RAY_TRACING
namespace
{
struct PushRayTracing
{
    uint32_t asIndex = 0;
    uint32_t outputIndex = 0;
};

VkAccelerationStructureKHR CreateAccelerationStructure(VkRenderDevice *device, VkDeviceSize size,
                                                       VkAccelerationStructureTypeKHR type, BufferAllocation &storage)
{
    VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = storage.buffer;
    createInfo.offset = 0;
    createInfo.size = size;
    createInfo.type = type;
    VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
    VK_ASSERT(
        VkExt::vkCreateAccelerationStructureKHR(device->GetDevice(), &createInfo, nullptr, &accelerationStructure));
    return accelerationStructure;
}
}  // namespace

TEST_CASE_METHOD(Muyo::GraphicsTestEnv,
                 "DescriptorHeap: ray tracing pipeline is null-layout and the AS representation is recorded",
                 "[DescriptorHeap]")
{
    Muyo::VkRenderDevice *device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B0 records the row as a gap, not a pass");
    }

    const VkDeviceSize asDescriptorSize = device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
    INFO("AS descriptor size: " << asDescriptorSize);
    INFO(
        "Slang heap lowering represents RaytracingAccelerationStructure as an 8-byte value; a different "
        "descriptor size means per-slot direct indexing cannot match without a mapping");

    Muyo::DescriptorHeapService heap;
    heap.Initialize(device);

    BufferAllocation output = CreateBuffer(
        device, kProbeBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    *static_cast<uint32_t *>(output.mapped) = 0;
    VkDeviceAddressRangeEXT outputRange{output.address, output.size};
    VkResourceDescriptorInfoEXT outputDescriptor{};
    outputDescriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    outputDescriptor.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    outputDescriptor.data.pAddressRange = &outputRange;
    const uint32_t outputIndex = heap.AcquireResource(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, outputDescriptor).index;

    const std::array<float, 9> vertices = {-0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F, 0.0F, 0.5F, 0.0F};
    BufferAllocation vertexBuffer =
        CreateBuffer(device, sizeof(vertices),
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    std::memcpy(vertexBuffer.mapped, vertices.data(), sizeof(vertices));

    VkAccelerationStructureGeometryKHR blasGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    blasGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    blasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    blasGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    blasGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    blasGeometry.geometry.triangles.vertexData.deviceAddress = vertexBuffer.address;
    blasGeometry.geometry.triangles.vertexStride = 3 * sizeof(float);
    blasGeometry.geometry.triangles.maxVertex = 2;
    blasGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR blasBuild{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    blasBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    blasBuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    blasBuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    blasBuild.geometryCount = 1;
    blasBuild.pGeometries = &blasGeometry;
    const uint32_t primitiveCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR blasSizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    VkExt::vkGetAccelerationStructureBuildSizesKHR(device->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                   &blasBuild, &primitiveCount, &blasSizes);

    BufferAllocation blasStorage =
        CreateBuffer(device, blasSizes.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    VkAccelerationStructureKHR blas = CreateAccelerationStructure(
        device, blasSizes.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasStorage);
    VkAccelerationStructureDeviceAddressInfoKHR blasAddressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    blasAddressInfo.accelerationStructure = blas;
    const VkDeviceAddress blasAddress =
        VkExt::vkGetAccelerationStructureDeviceAddressKHR(device->GetDevice(), &blasAddressInfo);

    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    device->GetPhysicalDeviceProperties(asProperties);
    BufferAllocation blasScratch =
        CreateBuffer(device, blasSizes.buildScratchSize + asProperties.minAccelerationStructureScratchOffsetAlignment,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    const VkDeviceAddress blasScratchAddress =
        AlignUp(blasScratch.address, asProperties.minAccelerationStructureScratchOffsetAlignment);

    VkAccelerationStructureInstanceKHR instance{};
    instance.transform.matrix[0][0] = 1.0F;
    instance.transform.matrix[1][1] = 1.0F;
    instance.transform.matrix[2][2] = 1.0F;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = blasAddress;

    BufferAllocation instanceBuffer =
        CreateBuffer(device, sizeof(instance),
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    std::memcpy(instanceBuffer.mapped, &instance, sizeof(instance));

    VkAccelerationStructureGeometryKHR tlasGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    tlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlasGeometry.geometry.instances.data.deviceAddress = instanceBuffer.address;
    tlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;

    VkAccelerationStructureBuildGeometryInfoKHR tlasBuild{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tlasBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasBuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasBuild.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasBuild.geometryCount = 1;
    tlasBuild.pGeometries = &tlasGeometry;
    VkAccelerationStructureBuildSizesInfoKHR tlasSizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    VkExt::vkGetAccelerationStructureBuildSizesKHR(device->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                   &tlasBuild, &primitiveCount, &tlasSizes);

    BufferAllocation tlasStorage =
        CreateBuffer(device, tlasSizes.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    VkAccelerationStructureKHR tlas = CreateAccelerationStructure(
        device, tlasSizes.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlasStorage);
    VkAccelerationStructureDeviceAddressInfoKHR tlasAddressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    tlasAddressInfo.accelerationStructure = tlas;
    const VkDeviceAddress tlasAddress =
        VkExt::vkGetAccelerationStructureDeviceAddressKHR(device->GetDevice(), &tlasAddressInfo);
    BufferAllocation tlasScratch =
        CreateBuffer(device, tlasSizes.buildScratchSize + asProperties.minAccelerationStructureScratchOffsetAlignment,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    const VkDeviceAddress tlasScratchAddress =
        AlignUp(tlasScratch.address, asProperties.minAccelerationStructureScratchOffsetAlignment);

    // A real TLAS descriptor: the address comes from vkGetAccelerationStructureDeviceAddressKHR and the
    // range size is 0 (the KHR form the spec allows for an acceleration structure).
    VkDeviceAddressRangeEXT asRange{tlasAddress, 0};
    VkResourceDescriptorInfoEXT asDescriptor{};
    asDescriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    asDescriptor.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    asDescriptor.data.pAddressRange = &asRange;
    const uint32_t asIndex = heap.AcquireResource(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, asDescriptor).index;

    VkShaderModule raygenModule = LoadHeapShader(device->GetDevice(), "heapProbe.rgen.heap.spv");
    VkShaderModule missModule = LoadHeapShader(device->GetDevice(), "heapProbe.rmiss.heap.spv");
    VkShaderModule closestHitModule = LoadHeapShader(device->GetDevice(), "heapProbe.rchit.heap.spv");

    VkPipelineShaderStageCreateInfo stages[3] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_RAYGEN_BIT_KHR, raygenModule,
         "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_MISS_BIT_KHR, missModule,
         "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
         closestHitModule, "main", nullptr}};
    VkRayTracingShaderGroupCreateInfoKHR groups[3] = {
        {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr,
         VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR,
         VK_SHADER_UNUSED_KHR},
        {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr,
         VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR,
         VK_SHADER_UNUSED_KHR},
        {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr,
         VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR,
         VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR}};
    groups[0].generalShader = 0;
    groups[1].generalShader = 1;
    groups[2].closestHitShader = 2;

    VkPipelineCreateFlags2CreateInfo flags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
    flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
    VkRayTracingPipelineCreateInfoKHR pipelineInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    pipelineInfo.pNext = &flags2;
    pipelineInfo.stageCount = 3;
    pipelineInfo.pStages = stages;
    pipelineInfo.groupCount = 3;
    pipelineInfo.pGroups = groups;
    pipelineInfo.maxPipelineRayRecursionDepth = 1;
    pipelineInfo.layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_ASSERT(VkExt::vkCreateRayTracingPipelinesKHR(device->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
                                                    &pipelineInfo, nullptr, &pipeline));

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    device->GetPhysicalDeviceProperties(rtProperties);
    const uint32_t handleSize = rtProperties.shaderGroupHandleSize;
    const uint32_t handleSizeAligned =
        static_cast<uint32_t>(AlignUp(handleSize, rtProperties.shaderGroupHandleAlignment));
    const VkDeviceSize raygenSize = AlignUp(handleSizeAligned, rtProperties.shaderGroupBaseAlignment);
    const VkDeviceSize missSize = AlignUp(handleSizeAligned, rtProperties.shaderGroupBaseAlignment);
    const VkDeviceSize hitSize = AlignUp(handleSizeAligned, rtProperties.shaderGroupBaseAlignment);
    const VkDeviceSize sbtSize = raygenSize + missSize + hitSize;
    BufferAllocation sbt =
        CreateBuffer(device, sbtSize + rtProperties.shaderGroupBaseAlignment,
                     VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    const VkDeviceAddress sbtAddress = AlignUp(sbt.address, rtProperties.shaderGroupBaseAlignment);
    const VkDeviceSize sbtOffset = sbtAddress - sbt.address;
    std::array<uint8_t, 3 * 64> handles{};
    const uint32_t handleBytes = 3 * handleSize;
    VK_ASSERT(
        VkExt::vkGetRayTracingShaderGroupHandlesKHR(device->GetDevice(), pipeline, 0, 3, handleBytes, handles.data()));
    auto *pSbt = static_cast<uint8_t *>(sbt.mapped) + sbtOffset;
    std::memcpy(pSbt, handles.data(), handleSize);
    std::memcpy(pSbt + raygenSize, handles.data() + handleSize, handleSize);
    std::memcpy(pSbt + raygenSize + missSize, handles.data() + 2 * handleSize, handleSize);

    VkStridedDeviceAddressRegionKHR raygenRegion{sbtAddress, raygenSize, raygenSize};
    VkStridedDeviceAddressRegionKHR missRegion{sbtAddress + raygenSize, handleSizeAligned, missSize};
    VkStridedDeviceAddressRegionKHR hitRegion{sbtAddress + raygenSize + missSize, handleSizeAligned, hitSize};
    VkStridedDeviceAddressRegionKHR callableRegion{0, 0, 0};

    VkCommandBuffer commandBuffer = device->AllocateStaticPrimaryCommandbuffer();
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_ASSERT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    blasBuild.dstAccelerationStructure = blas;
    blasBuild.scratchData.deviceAddress = blasScratchAddress;
    const VkAccelerationStructureBuildRangeInfoKHR blasRange{1, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR *pBlasRange = &blasRange;
    VkExt::vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &blasBuild, &pBlasRange);

    tlasBuild.dstAccelerationStructure = tlas;
    tlasBuild.scratchData.deviceAddress = tlasScratchAddress;
    const VkAccelerationStructureBuildRangeInfoKHR tlasRange{1, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRange;
    VkExt::vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &tlasBuild, &pTlasRange);

    VkMemoryBarrier2 asBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    asBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    asBarrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    asBarrier.dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    asBarrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    VkDependencyInfo asDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    asDependency.memoryBarrierCount = 1;
    asDependency.pMemoryBarriers = &asBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &asDependency);

    heap.Upload(commandBuffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);

    // Trace only when the shader representation (8-byte value, from the SPIR-V lowering) matches the
    // implementation's descriptor size. A mismatch is the design's anticipated AS case: B1a routes it to
    // the mapping path or rejects it with a named diagnostic, rather than packing 8 bytes over a 16-byte
    // descriptor.
    if (asDescriptorSize == 8)
    {
        PushRayTracing push{};
        push.asIndex = asIndex;
        push.outputIndex = outputIndex;
        WritePushData(commandBuffer, &push, sizeof(push));
        VkExt::vkCmdTraceRaysKHR(commandBuffer, &raygenRegion, &missRegion, &hitRegion, &callableRegion, 1, 1, 1);
    }
    else
    {
        WARN("AS descriptor size is " << asDescriptorSize
                                      << " but Slang's heap representation is 8 bytes; direct per-slot indexing "
                                         "is not proven, so B1a must use the mapping path or reject AS. The "
                                         "null-layout RT pipeline creation itself is still exercised.");
    }

    VkBufferMemoryBarrier2 outputBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    outputBarrier.srcStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    outputBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    outputBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    outputBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    outputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputBarrier.buffer = output.buffer;
    outputBarrier.offset = 0;
    outputBarrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo outputDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    outputDependency.bufferMemoryBarrierCount = 1;
    outputDependency.pBufferMemoryBarriers = &outputBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &outputDependency);

    VK_ASSERT(vkEndCommandBuffer(commandBuffer));
    std::vector<VkCommandBuffer> commandBuffers{commandBuffer};
    device->SubmitCommandBuffersAndWait(commandBuffers);
    device->FreeStaticPrimaryCommandbuffer(commandBuffer);

    if (asDescriptorSize == 8) REQUIRE(*static_cast<uint32_t *>(output.mapped) == 42U);

    sbt.Destroy(device->GetDevice());
    vkDestroyPipeline(device->GetDevice(), pipeline, nullptr);
    vkDestroyShaderModule(device->GetDevice(), closestHitModule, nullptr);
    vkDestroyShaderModule(device->GetDevice(), missModule, nullptr);
    vkDestroyShaderModule(device->GetDevice(), raygenModule, nullptr);
    // The acceleration structures name their storage buffers, so destroy the objects before the buffers.
    VkExt::vkDestroyAccelerationStructureKHR(device->GetDevice(), tlas, nullptr);
    VkExt::vkDestroyAccelerationStructureKHR(device->GetDevice(), blas, nullptr);
    tlasScratch.Destroy(device->GetDevice());
    tlasStorage.Destroy(device->GetDevice());
    instanceBuffer.Destroy(device->GetDevice());
    blasScratch.Destroy(device->GetDevice());
    blasStorage.Destroy(device->GetDevice());
    vertexBuffer.Destroy(device->GetDevice());
    output.Destroy(device->GetDevice());
}
#endif  // FEATURE_RAY_TRACING
