#include "RenderGraphBuilder.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "RenderGraph/ResourceUseResolver.h"
#include "PipelineStateBuilder.h"
#include "ShaderReflectionFetcher.h"
#include "VkExtFuncsLoader.h"
#include "vulkan/vulkan_core.h"

namespace
{
bool IsDepthFormat(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

VkImageAspectFlags AspectForFormat(VkFormat format)
{
    if (IsDepthFormat(format)) return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

uint32_t AlignUp(uint32_t nSize, uint32_t nAlignment)
{
    return (nSize + nAlignment - 1) / nAlignment * nAlignment;
}
}  // namespace

namespace Muyo::RenderGraph
{

RenderGraphBuilder::RenderGraphBuilder(VkRenderDevice* renderDevice)
    : m_shaderAssetManager(renderDevice->GetDevice())
    , m_vkDevice(renderDevice->GetDevice())
    , m_descriptorSetManager(*GetDescriptorManager())
{
}

RenderGraphBuilder::~RenderGraphBuilder()
{
    for (auto& rgn : m_compiledGraphNodes)
    {
        DestroyCompiledRenderGraphNode(rgn);
    }
    m_vkDevice = VK_NULL_HANDLE;
}

RenderGraphBuilder& RenderGraphBuilder::AddResource(const ResourceHandle& handle, ResourceDesc desc)
{
    m_resourceDescRegistry[handle] = std::move(desc);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::ImportResource(const ResourceHandle& handle, const IRenderResource* resource)
{
    m_importedResources[handle] = resource;
    return *this;
}

std::optional<ResourceDesc> RenderGraphBuilder::GetResourceDesc(const ResourceHandle& handle) const
{
    auto it = m_resourceDescRegistry.find(handle);
    return it != m_resourceDescRegistry.end() ? std::optional<ResourceDesc>{it->second} : std::nullopt;
}

const IRenderResource* RenderGraphBuilder::ResolveResource(const ResourceHandle& handle) const
{
    if (auto it = m_importedResources.find(handle); it != m_importedResources.end())
    {
        return it->second;
    }
    return GetRenderResourceManager()->template GetResource<IRenderResource>(handle);
}

RenderGraphBuilder::CompiledRenderGraphNode RenderGraphBuilder::CompileRenderGraphNode(
    const RenderGraphBuilder::RenderGraphNode& rgn)
{
    // Compile pipeline and pipeline layout from node
    CompiledRenderGraphNode result{
        .logicalRenderGraphNode = &rgn, .pipeline = VK_NULL_HANDLE, .pipelineLayout = VK_NULL_HANDLE};
    result.queueType = rgn.queueType;
    result.isRayTracing = (rgn.queueType == QueueType::RAY_TRACING);
    result.bindingPoint = (rgn.queueType == QueueType::COMPUTE)  ? VK_PIPELINE_BIND_POINT_COMPUTE
                          : (rgn.queueType == QueueType::RAY_TRACING) ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
                                                                      : VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Retrive and merge shader reflections
    std::vector<ShaderReflection> shaderReflections;
    std::vector<VkShaderModule> shaderModules;
    for (const auto& shaderKey : rgn.shaders)
    {
        if (shaderKey.IsValid())
        {
            const auto* shaderAsset = m_shaderAssetManager.GetShaderAsset(shaderKey);
            if (shaderAsset)
            {
                shaderReflections.push_back(shaderAsset->shaderReflection);
                shaderModules.push_back(shaderAsset->shaderModule);
            }
        }
    }
    // Ray tracing pipeline stages, in the order the RT builder expects (raygen, miss, hit).
    std::vector<VkShaderModule> rtShaderModules;
    for (const auto& shaderKey : rgn.rtShaders)
    {
        if (shaderKey.IsValid())
        {
            const auto* shaderAsset = m_shaderAssetManager.GetShaderAsset(shaderKey);
            if (shaderAsset)
            {
                shaderReflections.push_back(shaderAsset->shaderReflection);
                shaderModules.push_back(shaderAsset->shaderModule);
                rtShaderModules.push_back(shaderAsset->shaderModule);
            }
        }
    }
    result.execute = rgn.execute;

    if (shaderReflections.size() > 0)
    {
        ShaderReflection mergedReflection = MergeShaderReflections(shaderReflections);

        // Descriptor set layouts.
        //  * Graphics nodes use the built-in semantic layouts (set 0/1/2 = PER_VIEW/PER_OBJ/MATERIAL).
        //  * Compute and ray tracing nodes build layouts from the shader reflection so shaders can
        //    bind arbitrary set/binding (TLAS, storage images, ...).
        if (rgn.queueType == QueueType::COMPUTE || rgn.queueType == QueueType::RAY_TRACING)
        {
            BuildReflectionDescriptorSets(result, rgn, mergedReflection);
        }
        else if (!mergedReflection.descriptorBindings.empty())
        {
            result.descriptorSetLayouts.resize(ENUM_COUNT<ResourceBindingSemantic>);
            result.descriptorSetLayouts[0] =
                m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::PER_VIEW);
            result.descriptorSetLayouts[1] =
                m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::PER_OBJ);
            result.descriptorSetLayouts[2] =
                m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::MATERIAL);
            result.descriptorSets = {
                m_descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::PER_VIEW),
                m_descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::PER_OBJ),
                m_descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::MATERIAL)};
        }

        std::vector<VkPushConstantRange> pushConstantRanges;
        for (const auto& pcRange : mergedReflection.pushConstantRanges)
        {
            pushConstantRanges.push_back(
                {.stageFlags = pcRange.stageFlags, .offset = pcRange.offset, .size = pcRange.size});
        }
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(result.descriptorSetLayouts.size()),
            .pSetLayouts = result.descriptorSetLayouts.empty() ? nullptr : result.descriptorSetLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
            .pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data()};
        VK_ASSERT(vkCreatePipelineLayout(m_vkDevice, &pipelineLayoutInfo, nullptr, &result.pipelineLayout));

        // Inspect the node's resolved resource uses to determine attachment formats.
        std::vector<VkFormat> colorAttachmentFormats;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        for (const auto& resourceUse : rgn.resourceUses)
        {
            if (resourceUse.usage == ResourceUsage::COLOR_ATTACHMENT)
            {
                colorAttachmentFormats.push_back(resourceUse.format != VK_FORMAT_UNDEFINED ? resourceUse.format
                                                                                          : VK_FORMAT_R16G16B16A16_SFLOAT);
            }
            else if (resourceUse.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
            {
                depthAttachmentFormat = resourceUse.format != VK_FORMAT_UNDEFINED ? resourceUse.format : VK_FORMAT_D32_SFLOAT;
            }
        }

        VkPipelineRenderingCreateInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.size()),
            .pColorAttachmentFormats = colorAttachmentFormats.empty() ? nullptr : colorAttachmentFormats.data(),
            .depthAttachmentFormat = depthAttachmentFormat,
        };

        if (rgn.queueType == QueueType::GRAPHICS)
        {
            result.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            // Create pipeline
            result.pipeline =
                CreatePipelineFromPSODesc(rgn.psoDesc, m_vkDevice, shaderModules, result.pipelineLayout, renderingInfo);
        }
        else if (rgn.queueType == QueueType::COMPUTE)
        {
            result.bindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = shaderModules.front();
            stageInfo.pName = "main";
            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = result.pipelineLayout;
            VK_ASSERT(vkCreateComputePipelines(m_vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result.pipeline));
        }
        else if (rgn.queueType == QueueType::RAY_TRACING)
        {
            result.bindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
            BuildRayTracingPipeline(result, rgn, rtShaderModules);

            // Trace extent: use the first storage image the node writes to.
            for (const auto& resourceUse : rgn.resourceUses)
            {
                if (resourceUse.usage == ResourceUsage::STORAGE_IMAGE && resourceUse.extent.width > 0)
                {
                    result.traceExtent = {resourceUse.extent.width, resourceUse.extent.height};
                    break;
                }
            }
        }
    }

    return result;
}

void RenderGraphBuilder::BuildReflectionDescriptorSets(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                                      const ShaderReflection& mergedReflection)
{
    if (mergedReflection.descriptorBindings.empty()) return;

    // Group the reflected bindings by set index.
    uint32_t maxSet = 0;
    for (const auto& binding : mergedReflection.descriptorBindings)
    {
        maxSet = std::max(maxSet, binding.set);
    }
    const uint32_t setCount = maxSet + 1;

    std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindingsPerSet(setCount);
    for (const auto& binding : mergedReflection.descriptorBindings)
    {
        bindingsPerSet[binding.set].push_back({.binding = binding.binding,
                                               .descriptorType = binding.type,
                                               .descriptorCount = binding.count,
                                               .stageFlags = binding.stageFlags,
                                               .pImmutableSamplers = nullptr});
    }

    auto& descriptorManager = *GetDescriptorManager();
    rgn.descriptorSetLayouts.resize(setCount, VK_NULL_HANDLE);
    rgn.descriptorSets.resize(setCount, VK_NULL_HANDLE);
    for (uint32_t set = 0; set < setCount; ++set)
    {
        rgn.descriptorSetLayouts[set] = descriptorManager.AllocateDescriptorSetLayout(bindingsPerSet[set]);
        rgn.descriptorSets[set] = descriptorManager.AllocateDescriptorSet(rgn.descriptorSetLayouts[set]);
    }
    rgn.ownsDescriptorSets = true;

    // Write the node's explicitly-bound resources into the matching set/binding.
    for (const auto& use : logicalNode.resourceUses)
    {
        if (!use.descriptorBinding.has_value()) continue;
        const uint32_t set = use.descriptorBinding->set;
        const uint32_t binding = use.descriptorBinding->binding;
        if (set >= setCount) continue;

        const IRenderResource* pResource = ResolveResource(use.handle);
        if (pResource == nullptr) continue;

        // Resolve the descriptor type from the reflection entry that matches this set/binding.
        VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        for (const auto& reflected : mergedReflection.descriptorBindings)
        {
            if (reflected.set == set && reflected.binding == binding)
            {
                descriptorType = reflected.type;
                break;
            }
        }
        if (descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM) continue;

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = rgn.descriptorSets[set];
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = descriptorType;

        VkDescriptorBufferInfo bufferInfo{};
        VkDescriptorImageInfo imageInfo{};
        VkWriteDescriptorSetAccelerationStructureKHR asWrite{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        if (const auto* pAccel = dynamic_cast<const AccelerationStructure*>(pResource))
        {
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &pAccel->GetAccelerationStructure();
            write.pNext = &asWrite;
        }
        else if (const auto* pBuffer = dynamic_cast<const BufferResource*>(pResource))
        {
            bufferInfo.buffer = pBuffer->buffer();
            bufferInfo.offset = 0;
            bufferInfo.range = pBuffer->GetSize();
            write.pBufferInfo = &bufferInfo;
        }
        else if (const auto* pImage = dynamic_cast<const ImageResource*>(pResource))
        {
            imageInfo.imageView = pImage->getView();
            imageInfo.imageLayout = (descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                        ? VK_IMAGE_LAYOUT_GENERAL
                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            write.pImageInfo = &imageInfo;
        }
        else
        {
            continue;
        }

        vkUpdateDescriptorSets(m_vkDevice, 1, &write, 0, nullptr);
    }
}

void RenderGraphBuilder::BuildRayTracingPipeline(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                                 const std::vector<VkShaderModule>& shaderModules)
{
    if (shaderModules.size() < 3)
    {
        throw std::runtime_error("Ray tracing node '" + logicalNode.name +
                                 "' needs raygen, miss and closest-hit shaders");
    }

    RayTracingPipelineBuilder builder;
    builder.AddShaderModule(shaderModules[0], VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .AddShaderModule(shaderModules[1], VK_SHADER_STAGE_MISS_BIT_KHR)
        .AddShaderModule(shaderModules[2], VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    builder.SetPipelineLayout(rgn.pipelineLayout).SetMaxRecursionDepth(1);
    VkRayTracingPipelineCreateInfoKHR createInfo = builder.Build();
    VK_ASSERT(VkExt::vkCreateRayTracingPipelinesKHR(m_vkDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &createInfo, nullptr,
                                                    &rgn.pipeline));

    // ── Shader binding table ──────────────────────────────────────────────
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    GetRenderDevice()->GetPhysicalDeviceProperties(rtProps);

    const uint32_t nHandleSize = rtProps.shaderGroupHandleSize;
    const uint32_t nHandleSizeAligned = AlignUp(nHandleSize, rtProps.shaderGroupHandleAlignment);

    // 1 raygen, 1 miss, 1 hit group.
    VkStridedDeviceAddressRegionKHR& rgenRegion = rgn.sbtRegions[0];
    VkStridedDeviceAddressRegionKHR& missRegion = rgn.sbtRegions[1];
    VkStridedDeviceAddressRegionKHR& hitRegion = rgn.sbtRegions[2];

    rgenRegion.stride = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);
    rgenRegion.size = rgenRegion.stride;
    missRegion.stride = nHandleSizeAligned;
    missRegion.size = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);
    hitRegion.stride = nHandleSizeAligned;
    hitRegion.size = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);

    const uint32_t sbtSize = rgenRegion.size + missRegion.size + hitRegion.size;
    const uint32_t nHandleCount = 3;
    std::vector<uint8_t> handles(nHandleCount * nHandleSize);
    VK_ASSERT(VkExt::vkGetRayTracingShaderGroupHandlesKHR(m_vkDevice, rgn.pipeline, 0, nHandleCount, handles.size(),
                                                          handles.data()));

    ShaderBindingTableBuffer* pSBT = GetRenderResourceManager()->GetShaderBindingTableBuffer(
        "SBT_" + logicalNode.name, sbtSize);
    const VkDeviceAddress sbtAddress = GetRenderDevice()->GetBufferDeviceAddress(pSBT->buffer());
    rgenRegion.deviceAddress = sbtAddress;
    missRegion.deviceAddress = sbtAddress + rgenRegion.size;
    hitRegion.deviceAddress = sbtAddress + rgenRegion.size + missRegion.size;

    uint8_t* pMapped = static_cast<uint8_t*>(pSBT->Map());
    memcpy(pMapped, handles.data() + 0 * nHandleSize, nHandleSize);
    memcpy(pMapped + rgenRegion.size, handles.data() + 1 * nHandleSize, nHandleSize);
    memcpy(pMapped + rgenRegion.size + missRegion.size, handles.data() + 2 * nHandleSize, nHandleSize);
    pSBT->Unmap();
}

void RenderGraphBuilder::DestroyCompiledRenderGraphNode(CompiledRenderGraphNode& rgn)
{
    if (rgn.ownsDescriptorSets && !rgn.descriptorSets.empty())
    {
        vkFreeDescriptorSets(m_vkDevice, GetDescriptorManager()->GetDescriptorPool(),
                             static_cast<uint32_t>(rgn.descriptorSets.size()), rgn.descriptorSets.data());
        for (VkDescriptorSetLayout layout : rgn.descriptorSetLayouts)
        {
            if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_vkDevice, layout, nullptr);
        }
    }
    if (rgn.pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_vkDevice, rgn.pipelineLayout, nullptr);
    if (rgn.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_vkDevice, rgn.pipeline, nullptr);
}

void RenderGraphBuilder::AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo)
{
    const std::string& nodeName = nodeCreateInfo.nodeName;

    if (m_renderGraphNodes.find(nodeName) != m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node with name '" + nodeCreateInfo.nodeName + "' already exists in the render graph.");
    }

    m_renderGraphNodes[nodeName] = {.name = nodeName};

    auto& rgn = m_renderGraphNodes.at(nodeName);
    rgn.queueType = nodeCreateInfo.queueType;
    rgn.costHint = nodeCreateInfo.costHint;
    rgn.attachmentClearValues = nodeCreateInfo.attachmentClearValues;

    // Load shaders
    int shaderIdx = 0;
    for (const auto& shaderName : nodeCreateInfo.shaderNames)
    {
        auto key = m_shaderAssetManager.LoadShader(shaderName);
        if (key)
        {
            rgn.shaders[shaderIdx++] = key.value();
        }
    }

    // Load ray tracing shaders (raygen / miss / closest hit).
    for (size_t i = 0; i < nodeCreateInfo.rtShaderNames.size() && i < rgn.rtShaders.size(); ++i)
    {
        auto key = m_shaderAssetManager.LoadShader(nodeCreateInfo.rtShaderNames[i]);
        if (key)
        {
            rgn.rtShaders[i] = key.value();
        }
    }

    // Resolve resource uses
    for (const auto& resourceUse : nodeCreateInfo.resourceUses)
    {
        ResolvedResourceUse resolved = ResolveResourceUse(resourceUse);
        // Fill in allocation-time properties (format / extent) so pipeline + barrier generation can use them.
        if (const auto desc = GetResourceDesc(resourceUse.handle))
        {
            if (const auto* imageDesc = std::get_if<ImageResourceDesc>(&*desc))
            {
                resolved.format = imageDesc->format;
                resolved.extent = {imageDesc->extent.width, imageDesc->extent.height, 1};
            }
        }
        rgn.resourceUses.push_back(std::move(resolved));
    }
    rgn.psoDesc = nodeCreateInfo.psoDesc;
    rgn.execute = nodeCreateInfo.execute;
}

void RenderGraphBuilder::AddDependency(const std::string& fromNode, const std::string& toNode)
{
    if (m_renderGraphNodes.find(fromNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + fromNode + "' does not exist in the render graph.");
    }

    if (m_renderGraphNodes.find(toNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + toNode + "' does not exist in the render graph.");
    }

    if (!m_dependencyGraph.AddEdge(fromNode, toNode))
    {
        throw std::runtime_error("Adding dependency from '" + fromNode + "' to '" + toNode +
                                 "' creates a cycle in the render graph.");
    }
}

void RenderGraphBuilder::Build()
{
    if (m_dependencyGraph.HasCycle())
    {
        throw std::runtime_error("Render graph contains a cycle!");
    }

    m_compiledGraphNodes.clear();
    m_compiledGraphNodes.reserve(m_renderGraphNodes.size());
    std::vector<std::string> executionOrder = m_renderGraphNodes.size() == 1
                                                  ? std::vector<std::string>{m_renderGraphNodes.begin()->first}
                                                  : m_dependencyGraph.TopologicalSort();

    // Allocate all graph-owned resources before compiling nodes.
    for (const auto& [handle, desc] : m_resourceDescRegistry)
    {
        if (m_importedResources.find(handle) != m_importedResources.end()) continue;
        AllocateResource(desc, *GetRenderResourceManager(), handle);
    }

    std::unordered_map<ResourceHandle, uint32_t> resourceCurrentVersions;
    for (const auto& nodeName : executionOrder)
    {
        // Update handle versions
        auto& node = m_renderGraphNodes.at(nodeName);
        for (auto& resource : node.resourceUses)
        {
            ResourceHandle handle = std::string(resource.handle);
            if (resourceCurrentVersions.find(handle) == resourceCurrentVersions.end())
            {
                resourceCurrentVersions[handle] = 0;
                m_resourceLastUsedVersion[handle] = 0;
            }
            else
            {
                resource.version = resourceCurrentVersions[handle];
                if (resource.io == ResourceIOType::WRITE || resource.io == ResourceIOType::READ_WRITE)
                {
                    resourceCurrentVersions[handle]++;
                }
            }
        }

        // Compile RenderGraphNode
        m_compiledGraphNodes.push_back(CompileRenderGraphNode(node));
    }
}

bool RenderGraphBuilder::BeginRendering(VkCommandBuffer cmdBuf, const CompiledRenderGraphNode& rgn,
                                        RenderGraphNodeContext& ctx, std::unordered_set<ResourceHandle>& alreadyWritten,
                                        std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                                        std::vector<VkClearValue>& clearValues)
{
    colorAttachments.clear();
    clearValues.clear();

    VkRenderingAttachmentInfo depthAttachment{};
    bool hasDepth = false;
    VkExtent2D renderArea{0, 0};

    uint32_t clearIndex = 0;
    for (const auto& use : rgn.logicalRenderGraphNode->resourceUses)
    {
        if (use.usage != ResourceUsage::COLOR_ATTACHMENT && use.usage != ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
        {
            continue;
        }

        const auto* image = dynamic_cast<const ImageResource*>(ResolveResource(use.handle));
        if (!image) continue;

        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = image->getView();
        attachment.imageLayout = use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT
                                     ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const bool firstWrite = alreadyWritten.find(use.handle) == alreadyWritten.end();
        attachment.loadOp = firstWrite ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (firstWrite)
        {
            VkClearValue clearValue{};
            if (use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
            {
                clearValue.depthStencil = {1.0f, 0};
            }
            else if (clearIndex < rgn.logicalRenderGraphNode->attachmentClearValues.size())
            {
                clearValue = rgn.logicalRenderGraphNode->attachmentClearValues[clearIndex++];
            }
            attachment.clearValue = clearValue;
            alreadyWritten.insert(use.handle);
        }
        else if (use.usage == ResourceUsage::COLOR_ATTACHMENT)
        {
            ++clearIndex;
        }

        if (use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
        {
            depthAttachment = attachment;
            hasDepth = true;
        }
        else
        {
            colorAttachments.push_back(attachment);
        }

        if (renderArea.width == 0 && renderArea.height == 0)
        {
            renderArea = {use.extent.width, use.extent.height};
        }
    }

    if (colorAttachments.empty() && !hasDepth) return false;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {.offset = {0, 0}, .extent = renderArea};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.empty() ? nullptr : colorAttachments.data();
    if (hasDepth) renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmdBuf, &renderingInfo);

    // Viewport / scissor from the render area.
    VkViewport viewport = {0.0F, 0.0F, static_cast<float>(renderArea.width), static_cast<float>(renderArea.height), 0.0F, 1.0F};
    VkRect2D scissor = {{0, 0}, renderArea};
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    return true;
}

void RenderGraphBuilder::RecordBarriers(VkCommandBuffer cmdBuf, const std::vector<ResolvedResourceUse>& resourceUses,
                                        uint32_t queueFamily)
{
    std::vector<VkImageMemoryBarrier> imageBarriers;
    std::vector<VkBufferMemoryBarrier> bufferBarriers;

    for (const auto& use : resourceUses)
    {
        ResourceAccessState& state = m_resourceAccessStates[use.handle];

        // A pending acquire means this resource is being handed over from another queue family;
        // the first use emits a queue-family transfer barrier instead of a normal one.
        const bool bAcquire = state.pendingAcquireFamily >= 0;
        const uint32_t acquireSrcFamily =
            bAcquire ? static_cast<uint32_t>(state.pendingAcquireFamily) : VK_QUEUE_FAMILY_IGNORED;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkAccessFlags srcAccess = 0;
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        const bool fromCpu = state.writtenByCpu;
        if (fromCpu)
        {
            // CPU node wrote this resource (host-visible memory); flush before first GPU use.
            srcStage = VK_PIPELINE_STAGE_HOST_BIT;
            srcAccess = VK_ACCESS_HOST_WRITE_BIT;
        }
        else if (state.seen)
        {
            srcStage = static_cast<VkPipelineStageFlags>(state.lastStages);
            srcAccess = static_cast<VkAccessFlags>(state.lastAccess);
            oldLayout = state.lastLayout;
        }

        const bool needsBarrier = bAcquire || fromCpu || !state.seen ||
                                  (state.lastAccess != static_cast<VkAccessFlags2>(use.access)) ||
                                  (state.lastLayout != use.imageLayout);

        if (use.kind == ResourceKind::IMAGE)
        {
            const auto* image = dynamic_cast<const ImageResource*>(ResolveResource(use.handle));
            if (!image) continue;

            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = bAcquire ? 0 : srcAccess;
            barrier.dstAccessMask = static_cast<VkAccessFlags>(use.access);
            barrier.oldLayout = state.seen ? oldLayout : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = use.imageLayout;
            barrier.srcQueueFamilyIndex = bAcquire ? acquireSrcFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = bAcquire ? queueFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->getImage();
            barrier.subresourceRange = {AspectForFormat(image->GetImageFormat()), 0, 1, 0, 1};
            if (needsBarrier)
            {
                imageBarriers.push_back(barrier);
            }
        }
        else if (use.kind == ResourceKind::BUFFER)
        {
            const auto* buffer = dynamic_cast<const BufferResource*>(ResolveResource(use.handle));
            if (!buffer) continue;

            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = bAcquire ? 0 : srcAccess;
            barrier.dstAccessMask = static_cast<VkAccessFlags>(use.access);
            barrier.srcQueueFamilyIndex = bAcquire ? acquireSrcFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = bAcquire ? queueFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            if (needsBarrier)
            {
                bufferBarriers.push_back(barrier);
            }
        }

        state.seen = true;
        state.lastStages = use.stages;
        state.lastAccess = use.access;
        state.lastLayout = use.imageLayout;
        state.queueFamily = queueFamily;
        state.pendingAcquireFamily = -1;
    }

    if (imageBarriers.empty() && bufferBarriers.empty()) return;

    VkPipelineStageFlags srcStageMask = 0;
    VkPipelineStageFlags dstStageMask = 0;
    // Conservative stage masks: use ALL_COMMANDS so any prior stage is flushed and any later stage is blocked.
    // HOST must be included explicitly so CPU-written (host-visible) buffers with HOST_WRITE srcAccess are valid.
    srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT;
    dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    vkCmdPipelineBarrier(cmdBuf, srcStageMask, dstStageMask, 0, 0, nullptr, bufferBarriers.size(), bufferBarriers.data(),
                         imageBarriers.size(), imageBarriers.data());
}

void RenderGraphBuilder::RecordQueueTransferBarriers(VkCommandBuffer cmdBuf, uint32_t srcQueueFamily,
                                                     uint32_t dstQueueFamily, bool bAcquire,
                                                     const std::vector<ResourceHandle>& handles)
{
    if (bAcquire)
    {
        // The actual barrier is emitted by RecordBarriers on the first use in this queue.
        for (const auto& handle : handles)
        {
            m_resourceAccessStates[handle].pendingAcquireFamily = static_cast<int32_t>(srcQueueFamily);
        }
        return;
    }

    // Release: hand ownership of the resource from the producing queue family to the consumer.
    std::vector<VkImageMemoryBarrier> imageBarriers;
    std::vector<VkBufferMemoryBarrier> bufferBarriers;
    for (const auto& handle : handles)
    {
        ResourceAccessState& state = m_resourceAccessStates[handle];
        const IRenderResource* pResource = ResolveResource(handle);
        if (pResource == nullptr) continue;

        if (const auto* image = dynamic_cast<const ImageResource*>(pResource))
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = static_cast<VkAccessFlags>(state.lastAccess);
            barrier.dstAccessMask = 0;
            barrier.oldLayout = state.lastLayout;
            barrier.newLayout = state.lastLayout;
            barrier.srcQueueFamilyIndex = srcQueueFamily;
            barrier.dstQueueFamilyIndex = dstQueueFamily;
            barrier.image = image->getImage();
            barrier.subresourceRange = {AspectForFormat(image->GetImageFormat()), 0, 1, 0, 1};
            imageBarriers.push_back(barrier);
        }
        else if (const auto* buffer = dynamic_cast<const BufferResource*>(pResource))
        {
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = static_cast<VkAccessFlags>(state.lastAccess);
            barrier.dstAccessMask = 0;
            barrier.srcQueueFamilyIndex = srcQueueFamily;
            barrier.dstQueueFamilyIndex = dstQueueFamily;
            barrier.buffer = buffer->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            bufferBarriers.push_back(barrier);
        }
    }

    if (imageBarriers.empty() && bufferBarriers.empty()) return;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
                         static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());
}

QueueType RenderGraphBuilder::GetQueueKey(QueueType type)
{
    // Only compute runs on the dedicated async queue; RT / copy follow the graphics queue.
    return (type == QueueType::COMPUTE) ? QueueType::COMPUTE : QueueType::GRAPHICS;
}

VkQueue RenderGraphBuilder::GetQueueForType(QueueType type) const
{
    if (type == QueueType::COMPUTE) return GetRenderDevice()->GetComputeQueue();
    return GetRenderDevice()->GetGraphicsQueue();
}

uint32_t RenderGraphBuilder::GetQueueFamilyForType(QueueType type) const
{
    if (type == QueueType::COMPUTE) return GetRenderDevice()->GetComputeQueueFamily();
    return GetRenderDevice()->GetGraphicsQueueFamily();
}

VkCommandBuffer RenderGraphBuilder::AllocateCommandBufferForType(QueueType type) const
{
    if (type == QueueType::COMPUTE) return GetRenderDevice()->AllocateComputeCommandBuffer();
    return GetRenderDevice()->AllocateReusablePrimaryCommandbuffer();
}

void RenderGraphBuilder::FreeCommandBufferForType(QueueType type, VkCommandBuffer cmdBuf) const
{
    if (cmdBuf == VK_NULL_HANDLE) return;
    if (type == QueueType::COMPUTE)
    {
        GetRenderDevice()->FreeComputeCommandBuffer(cmdBuf);
    }
    else
    {
        GetRenderDevice()->FreeReusablePrimaryCommandbuffer(cmdBuf);
    }
}

void RenderGraphBuilder::Execute()
{
    m_resourceAccessStates.clear();

    RenderGraphNodeContext context{
        .queueType = QueueType::GRAPHICS,
        .resourceManager = *GetRenderResourceManager(),
        .meshManager = *GetMeshResourceManager(),
    };

    std::unordered_set<ResourceHandle> alreadyWritten;
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    std::vector<VkClearValue> clearValues;

    // ── Split the graph into contiguous per-queue segments; CPU nodes run inline on the host. ──
    struct Segment
    {
        QueueType queueType = QueueType::GRAPHICS;
        size_t begin = 0;  // first compiled node index (inclusive)
        size_t end = 0;    // one past the last compiled node index
    };
    std::vector<Segment> segments;
    size_t segmentBegin = 0;
    QueueType segmentQueue = QueueType::COUNT;
    bool bInSegment = false;

    for (size_t i = 0; i < m_compiledGraphNodes.size(); ++i)
    {
        const auto& rgn = m_compiledGraphNodes[i];
        context.queueType = rgn.queueType;

        if (rgn.queueType == QueueType::CPU)
        {
            // Pure CPU node: run host-side work, mark its written resources for a lazy host flush.
            context.commandBuffer = VK_NULL_HANDLE;
            context.pipeline = VK_NULL_HANDLE;
            rgn.execute(context);
            for (const auto& use : rgn.logicalRenderGraphNode->resourceUses)
            {
                if (use.io == ResourceIOType::WRITE || use.io == ResourceIOType::READ_WRITE)
                {
                    ResourceAccessState& state = m_resourceAccessStates[use.handle];
                    state.writtenByCpu = true;
                    state.seen = false;
                }
            }
            continue;
        }

        const QueueType key = GetQueueKey(rgn.queueType);
        if (!bInSegment || key != segmentQueue)
        {
            if (bInSegment) segments.push_back({segmentQueue, segmentBegin, i});
            segmentBegin = i;
            segmentQueue = key;
            bInSegment = true;
        }
    }
    if (bInSegment) segments.push_back({segmentQueue, segmentBegin, m_compiledGraphNodes.size()});

    if (segments.empty()) return;

    // ── Detect resources handed from one queue family to another. ──────────────────────────────
    struct Transfer
    {
        size_t producer = 0;
        size_t consumer = 0;
        ResourceHandle handle;
        uint32_t producerFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t consumerFamily = VK_QUEUE_FAMILY_IGNORED;
    };
    std::vector<Transfer> transfers;
    {
        std::unordered_map<ResourceHandle, size_t> lastSegmentForResource;
        for (size_t s = 0; s < segments.size(); ++s)
        {
            std::unordered_set<ResourceHandle> resourcesInSegment;
            for (size_t i = segments[s].begin; i < segments[s].end; ++i)
            {
                for (const auto& use : m_compiledGraphNodes[i].logicalRenderGraphNode->resourceUses)
                {
                    resourcesInSegment.insert(use.handle);
                }
            }
            for (const auto& handle : resourcesInSegment)
            {
                auto it = lastSegmentForResource.find(handle);
                if (it != lastSegmentForResource.end() && it->second != s)
                {
                    const uint32_t producerFamily = GetQueueFamilyForType(segments[it->second].queueType);
                    const uint32_t consumerFamily = GetQueueFamilyForType(segments[s].queueType);
                    if (producerFamily != consumerFamily)
                    {
                        transfers.push_back({it->second, s, handle, producerFamily, consumerFamily});
                    }
                }
                lastSegmentForResource[handle] = s;
            }
        }
    }

    // ── Record each segment into its own command buffer. ───────────────────────────────────────
    std::vector<VkCommandBuffer> segmentCmdBuffers(segments.size(), VK_NULL_HANDLE);
    for (size_t s = 0; s < segments.size(); ++s)
    {
        const uint32_t queueFamily = GetQueueFamilyForType(segments[s].queueType);
        VkCommandBuffer cmdBuf = AllocateCommandBufferForType(segments[s].queueType);
        segmentCmdBuffers[s] = cmdBuf;

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_ASSERT(vkBeginCommandBuffer(cmdBuf, &beginInfo));

        // Mark resources handed over from another queue; RecordBarriers emits the acquire barrier.
        for (const auto& transfer : transfers)
        {
            if (transfer.consumer == s)
            {
                RecordQueueTransferBarriers(cmdBuf, transfer.producerFamily, transfer.consumerFamily, true,
                                            {transfer.handle});
            }
        }

        for (size_t i = segments[s].begin; i < segments[s].end; ++i)
        {
            const auto& rgn = m_compiledGraphNodes[i];
            context.queueType = rgn.queueType;
            context.commandBuffer = cmdBuf;
            context.pipeline = rgn.pipeline;
            context.pipelineLayout = rgn.pipelineLayout;
            context.bindingPoint = rgn.bindingPoint;

            // 1. Barrier batch: transition all resources used by this node.
            RecordBarriers(cmdBuf, rgn.logicalRenderGraphNode->resourceUses, queueFamily);

            // 2. Auto render pass wrap for graphics nodes.
            bool rendering = false;
            if (rgn.queueType == QueueType::GRAPHICS)
            {
                rendering = BeginRendering(cmdBuf, rgn, context, alreadyWritten, colorAttachments, clearValues);
            }

            // 3. Bind pipeline + descriptor sets.
            if (rgn.pipeline != VK_NULL_HANDLE)
            {
                vkCmdBindPipeline(cmdBuf, rgn.bindingPoint, rgn.pipeline);

                if (!rgn.descriptorSetLayouts.empty())
                {
                    if (rgn.ownsDescriptorSets)
                    {
                        vkCmdBindDescriptorSets(cmdBuf, rgn.bindingPoint, rgn.pipelineLayout, 0,
                                                static_cast<uint32_t>(rgn.descriptorSets.size()),
                                                rgn.descriptorSets.data(), 0, nullptr);
                    }
                    else
                    {
                        for (const auto& use : rgn.logicalRenderGraphNode->resourceUses)
                        {
                            if (use.bindingSemantic == ResourceBindingSemantic::NONE) continue;
                            const IRenderResource* pResource = ResolveResource(use.handle);
                            if (pResource)
                            {
                                m_descriptorSetManager.BindResourceToDescriptorSet(pResource, use.bindingSemantic, 0);
                            }
                        }

                        vkCmdBindDescriptorSets(cmdBuf, rgn.bindingPoint, rgn.pipelineLayout, 0,
                                                static_cast<uint32_t>(rgn.descriptorSets.size()),
                                                rgn.descriptorSets.data(), 0, nullptr);
                    }
                }
            }

            // 4. User GPU work.
            rgn.execute(context);

            // 4b. Ray tracing nodes issue the trace automatically; the SBT is graph-managed.
            if (rgn.isRayTracing && rgn.pipeline != VK_NULL_HANDLE && rgn.traceExtent.width > 0)
            {
                const VkStridedDeviceAddressRegionKHR callable{0, 0, 0};
                VkExt::vkCmdTraceRaysKHR(cmdBuf, &rgn.sbtRegions[0], &rgn.sbtRegions[1], &rgn.sbtRegions[2], &callable,
                                         rgn.traceExtent.width, rgn.traceExtent.height, 1);
            }

            // 5. End auto render pass.
            if (rendering)
            {
                vkCmdEndRendering(cmdBuf);
            }
        }

        // Release ownership of resources consumed by a later queue.
        for (const auto& transfer : transfers)
        {
            if (transfer.producer == s)
            {
                RecordQueueTransferBarriers(cmdBuf, transfer.producerFamily, transfer.consumerFamily, false,
                                            {transfer.handle});
            }
        }

        VK_ASSERT(vkEndCommandBuffer(cmdBuf));
    }

    // ── Submit each queue, synchronizing cross-queue handovers with semaphores. ────────────────
    std::map<std::pair<size_t, size_t>, VkSemaphore> transitionSemaphores;
    for (const auto& transfer : transfers)
    {
        const auto key = std::make_pair(transfer.producer, transfer.consumer);
        if (transitionSemaphores.find(key) == transitionSemaphores.end())
        {
            VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore semaphore = VK_NULL_HANDLE;
            VK_ASSERT(vkCreateSemaphore(m_vkDevice, &semaphoreInfo, nullptr, &semaphore));
            transitionSemaphores[key] = semaphore;
        }
    }

    for (size_t s = 0; s < segments.size(); ++s)
    {
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        for (const auto& [key, semaphore] : transitionSemaphores)
        {
            if (key.second == s)
            {
                waitSemaphores.push_back(semaphore);
                waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            }
            if (key.first == s)
            {
                signalSemaphores.push_back(semaphore);
            }
        }

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        submitInfo.pWaitSemaphores = waitSemaphores.empty() ? nullptr : waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &segmentCmdBuffers[s];
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.empty() ? nullptr : signalSemaphores.data();

        VK_ASSERT(vkQueueSubmit(GetQueueForType(segments[s].queueType), 1, &submitInfo, VK_NULL_HANDLE));
    }

    // Wait for all queues that participated in this frame.
    vkQueueWaitIdle(GetRenderDevice()->GetGraphicsQueue());
    if (GetRenderDevice()->IsComputeQueueDedicated())
    {
        vkQueueWaitIdle(GetRenderDevice()->GetComputeQueue());
    }

    for (const auto& [key, semaphore] : transitionSemaphores)
    {
        vkDestroySemaphore(m_vkDevice, semaphore, nullptr);
    }
    for (size_t s = 0; s < segments.size(); ++s)
    {
        FreeCommandBufferForType(segments[s].queueType, segmentCmdBuffers[s]);
    }
}

std::vector<std::string> RenderGraphBuilder::GetExecutionOrder() const { return m_dependencyGraph.TopologicalSort(); }

}  // namespace Muyo::RenderGraph
