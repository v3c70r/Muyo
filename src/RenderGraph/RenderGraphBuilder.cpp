#include "RenderGraphBuilder.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "RenderGraph/ResourceUseResolver.h"
#include "ShaderReflectionFetcher.h"
#include "vulkan/vulkan_core.h"

namespace Muyo::RenderGraph
{

RenderGraphBuilder::CompiledRenderGraphNode RenderGraphBuilder::CompileRenderGraphNode(
    const RenderGraphBuilder::RenderGraphNode& rgn)
{
    // Compile pipeline and pipeline layout from node
    CompiledRenderGraphNode result{
        .logicalRenderGraphNode = &rgn, .pipeline = VK_NULL_HANDLE, .pipelineLayout = VK_NULL_HANDLE};

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
    result.cpuCallback = std::move(rgn.cpuCallback);
    result.gpuCallBack = std::move(rgn.gpuCallBack);

    if (shaderReflections.size() > 0)
    {
        ShaderReflection mergedReflection = MergeShaderReflections(shaderReflections);
        
        if (!mergedReflection.descriptorBindings.empty())
        {
            result.descriptorSetLayouts.resize(ENUM_COUNT<ResourceBindingSemantic>);
            result.descriptorSetLayouts[0] = m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::PER_VIEW);
            result.descriptorSetLayouts[1] = m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::PER_OBJ);
            result.descriptorSetLayouts[2] = m_descriptorSetManager.GetDescriptorSetLayout(ResourceBindingSemantic::MATERIAL);
            
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
            .pSetLayouts = result.descriptorSetLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
            .pPushConstantRanges = pushConstantRanges.data()};
        VK_ASSERT(vkCreatePipelineLayout(m_vkDevice, &pipelineLayoutInfo, nullptr, &result.pipelineLayout));

        // Inspect number of attachments
        // Assume color attachments and depth attachments has predefined formats; RGBA16 and D32
        // TODO(qgu): expose interface to change the attachment formats
        std::vector<VkFormat> colorAttachmentFormats;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        for (const auto& resourceUse : rgn.resourceUses)
        {
            if (resourceUse.usage == ResourceUsage::COLOR_ATTACHMENT)
            {
                colorAttachmentFormats.push_back(VK_FORMAT_R16G16B16A16_SFLOAT);
            }
            else if (resourceUse.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
            {
                depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
            }
        }

        VkPipelineRenderingCreateInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.size()),
            .pColorAttachmentFormats = colorAttachmentFormats.data(),
            .depthAttachmentFormat = depthAttachmentFormat,
        };

        // Create pipeline
        result.pipeline =
            CreatePipelineFromPSODesc(rgn.psoDesc, m_vkDevice, shaderModules, result.pipelineLayout, renderingInfo);
    }

    return result;
}

void RenderGraphBuilder::DestroyCompiledRenderGraphNode(CompiledRenderGraphNode& rgn)
{
    vkDestroyPipelineLayout(m_vkDevice, rgn.pipelineLayout, nullptr);
    vkDestroyPipeline(m_vkDevice, rgn.pipeline, nullptr);
}

void RenderGraphBuilder::AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo)
{
    const std::string& nodeName = nodeCreateInfo.nodeName;

    if (m_renderGraphNodes.find(nodeName) != m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node with name '" + nodeCreateInfo.nodeName +
                                 "' already exists in the render graph.");
    }

    m_renderGraphNodes[nodeName] = {.name = nodeName};

    auto& rgn = m_renderGraphNodes.at(nodeName);

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

    // Resolve resource
    for (const auto& resourceUse : nodeCreateInfo.resourceUses)
    {
        // Resolve resource uses
        rgn.resourceUses.push_back(ResolveResourceUse(resourceUse));
    }
    rgn.psoDesc = std::move(nodeCreateInfo.psoDesc);
    rgn.cpuCallback = std::move(nodeCreateInfo.cpuCallback);
    rgn.gpuCallBack = std::move(nodeCreateInfo.gpuCallback);
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

    std::unordered_map<ResourceHandle, uint32_t> resourceCurrentVersions;
    for (const auto& nodeName : executionOrder)
    {
        // Update handle versions
        auto& node = m_renderGraphNodes.at(nodeName);
        for (auto& resource : node.resourceUses)
        {
            ResourceHandle handle = std::string(resource.handle);
            // If handle never used before, initialize versioning
            if (resourceCurrentVersions.find(handle) == resourceCurrentVersions.end())
            {
                resourceCurrentVersions[handle] = 0;
                m_resourceLastUsedVersion[handle] = 0;
            }
            else
            {
                // Assign version to resource use
                resource.version = resourceCurrentVersions[handle];
                // Increment version for write usages
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

void RenderGraphBuilder::Execute()
{
    RenderGraphNodeCpuContext cpuContext = {.resourceManager = *GetRenderResourceManager(),
                                            .meshManager = *GetMeshResourceManager()};
    for (const auto& rgn : m_compiledGraphNodes)
    {
        rgn.cpuCallback(cpuContext);
        GetRenderDevice()->ExecuteImmediateCommand(
            [&rgn, this](VkCommandBuffer buf)
            {
                RenderGraphNodeGpuContext gpuContext = {.resourceManager = *GetRenderResourceManager(),
                                                        .meshManager = *GetMeshResourceManager(),
                                                        .descriptorSetManager = m_descriptorSetManager,
                                                        .commandBuffer = buf,
                                                        .pipelineLayout = rgn.pipelineLayout,
                                                        .pipeline = rgn.pipeline,
                                                        .bindingPoint = rgn.bindingPoint,
                                                        };
                rgn.gpuCallBack(gpuContext);
            });
    }
};

std::vector<std::string> RenderGraphBuilder::GetExecutionOrder() const { return m_dependencyGraph.TopologicalSort(); }

}  // namespace Muyo::RenderGraph
