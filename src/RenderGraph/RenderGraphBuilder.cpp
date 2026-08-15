#include "RenderGraphBuilder.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "RenderGraph/ResourceUseResolver.h"
#include "ShaderReflectionFetcher.h"
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
}  // namespace

namespace Muyo::RenderGraph
{

RenderGraphBuilder::RenderGraphBuilder(VkRenderDevice* renderDevice)
    : m_shaderAssetManager(renderDevice->GetDevice())
    , m_vkDevice(renderDevice->GetDevice())
    , m_descriptorSetManager(*GetDescriptorManager())
{
    m_commandBuffers[static_cast<size_t>(QueueType::GRAPHICS)] = renderDevice->AllocateReusablePrimaryCommandbuffer();
    m_commandBuffers[static_cast<size_t>(QueueType::COMPUTE)] = renderDevice->AllocateComputeCommandBuffer();
    m_commandBuffers[static_cast<size_t>(QueueType::COPY)] = renderDevice->AllocateImmediateCommandBuffer();
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

RenderGraphBuilder::CompiledRenderGraphNode RenderGraphBuilder::CompileRenderGraphNode(
    const RenderGraphBuilder::RenderGraphNode& rgn)
{
    // Compile pipeline and pipeline layout from node
    CompiledRenderGraphNode result{
        .logicalRenderGraphNode = &rgn, .pipeline = VK_NULL_HANDLE, .pipelineLayout = VK_NULL_HANDLE};
    result.queueType = rgn.queueType;
    result.bindingPoint = (rgn.queueType == QueueType::COMPUTE) ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

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
    result.execute = rgn.execute;

    if (shaderReflections.size() > 0)
    {
        ShaderReflection mergedReflection = MergeShaderReflections(shaderReflections);

        // Descriptor set layouts: bind the built-in semantic layouts whenever the shaders declare any descriptors.
        if (!mergedReflection.descriptorBindings.empty())
        {
            result.descriptorSetLayouts.resize(ENUM_COUNT<ResourceBindingSemantic>);
            result.descriptorSetLayouts[0] =
                m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::PER_VIEW);
            result.descriptorSetLayouts[1] =
                m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::PER_OBJ);
            result.descriptorSetLayouts[2] =
                m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::MATERIAL);
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
    }

    return result;
}

void RenderGraphBuilder::DestroyCompiledRenderGraphNode(CompiledRenderGraphNode& rgn)
{
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

        const auto* image = ctx.resourceManager.template GetResource<ImageResource>(use.handle);
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

void RenderGraphBuilder::RecordBarriers(VkCommandBuffer cmdBuf, const std::vector<ResolvedResourceUse>& resourceUses)
{
    std::vector<VkImageMemoryBarrier> imageBarriers;
    std::vector<VkBufferMemoryBarrier> bufferBarriers;

    for (const auto& use : resourceUses)
    {
        ResourceAccessState& state = m_resourceAccessStates[use.handle];

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

        const bool needsBarrier = fromCpu || !state.seen ||
                                  (state.lastAccess != static_cast<VkAccessFlags2>(use.access)) ||
                                  (state.lastLayout != use.imageLayout);

        if (use.kind == ResourceKind::IMAGE)
        {
            const auto* image = GetRenderResourceManager()->template GetResource<ImageResource>(use.handle);
            if (!image) continue;

            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = static_cast<VkAccessFlags>(use.access);
            barrier.oldLayout = state.seen ? oldLayout : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = use.imageLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->getImage();
            barrier.subresourceRange = {AspectForFormat(image->GetImageFormat()), 0, 1, 0, 1};
            if (needsBarrier)
            {
                imageBarriers.push_back(barrier);
            }
        }
        else if (use.kind == ResourceKind::BUFFER)
        {
            const auto* buffer = GetRenderResourceManager()->template GetResource<BufferResource>(use.handle);
            if (!buffer) continue;

            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = static_cast<VkAccessFlags>(use.access);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
    }

    if (imageBarriers.empty() && bufferBarriers.empty()) return;

    VkPipelineStageFlags srcStageMask = 0;
    VkPipelineStageFlags dstStageMask = 0;
    // Conservative stage masks: use ALL_COMMANDS so any prior stage is flushed and any later stage is blocked.
    srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    vkCmdPipelineBarrier(cmdBuf, srcStageMask, dstStageMask, 0, 0, nullptr, bufferBarriers.size(), bufferBarriers.data(),
                         imageBarriers.size(), imageBarriers.data());
}

void RenderGraphBuilder::Execute()
{
    m_resourceAccessStates.clear();

    RenderGraphNodeContext context{
        .queueType = QueueType::GRAPHICS,
        .resourceManager = *GetRenderResourceManager(),
        .meshManager = *GetMeshResourceManager(),
    };

    VkCommandBuffer cmdBuf = m_commandBuffers[static_cast<size_t>(QueueType::GRAPHICS)];
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_ASSERT(vkBeginCommandBuffer(cmdBuf, &beginInfo));

    std::unordered_set<ResourceHandle> alreadyWritten;
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    std::vector<VkClearValue> clearValues;

    for (const auto& rgn : m_compiledGraphNodes)
    {
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

        context.commandBuffer = cmdBuf;
        context.pipeline = rgn.pipeline;
        context.pipelineLayout = rgn.pipelineLayout;
        context.bindingPoint = rgn.bindingPoint;

        // 1. Barrier batch: transition all resources used by this node.
        RecordBarriers(cmdBuf, rgn.logicalRenderGraphNode->resourceUses);

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
                // Write the node's resources into the semantic descriptor sets, then bind all three.
                for (const auto& use : rgn.logicalRenderGraphNode->resourceUses)
                {
                    if (use.bindingSemantic == ResourceBindingSemantic::NONE) continue;
                    const IRenderResource* pResource = nullptr;
                    if (use.kind == ResourceKind::BUFFER)
                    {
                        pResource = context.resourceManager.template GetResource<BufferResource>(use.handle);
                    }
                    else if (use.kind == ResourceKind::IMAGE)
                    {
                        pResource = context.resourceManager.template GetResource<ImageResource>(use.handle);
                    }
                    if (pResource)
                    {
                        m_descriptorSetManager.BindResourceToDescriptorSet(pResource, use.bindingSemantic, 0);
                    }
                }

                std::array<VkDescriptorSet, 3> sets = {
                    m_descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::PER_VIEW),
                    m_descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::PER_OBJ),
                    m_descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::MATERIAL)};
                vkCmdBindDescriptorSets(cmdBuf, rgn.bindingPoint, rgn.pipelineLayout, 0,
                                        static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
            }
        }

        // 4. User GPU work.
        rgn.execute(context);

        // 5. End auto render pass.
        if (rendering)
        {
            vkCmdEndRendering(cmdBuf);
        }
    }

    VK_ASSERT(vkEndCommandBuffer(cmdBuf));

    std::vector<VkCommandBuffer> cmdBuffers = {cmdBuf};
    GetRenderDevice()->SubmitCommandBuffersAndWait(cmdBuffers);
}

std::vector<std::string> RenderGraphBuilder::GetExecutionOrder() const { return m_dependencyGraph.TopologicalSort(); }

}  // namespace Muyo::RenderGraph
