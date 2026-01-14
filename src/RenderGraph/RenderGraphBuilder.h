#pragma once
#include <spirv_reflect.h>

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "DependencyGraph.h"
#include "MeshResourceManager.h"
#include "PSODesc.h"
#include "PerObjResourceManager.h"
#include "RenderGraphDescriptorSets.h"
#include "RenderGraphParameters.h"
#include "RenderGraphResourceHandle.h"
#include "ShaderAsset.h"

namespace Muyo::RenderGraph
{

static constexpr int MAX_SHADER_STAGES = 8;
enum class RenderGraphNodeType : uint8_t
{
    GRAPHICS,
    COMPUTE,
    RAY_TRACING
};
struct RenderGraphNodeCpuContext
{
    RenderResourceManager& resourceManager;
    MeshResourceManager& meshManager;
};

struct RenderGraphNodeGpuContext
{
    RenderResourceManager& resourceManager;
    MeshResourceManager& meshManager;
    RenderGraphDescriptorSets& descriptorSetManager;
    PerObjResourceManager& perObjResourceManager;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

using RenderGraphNodeCpuCallback = std::function<void(RenderGraphNodeCpuContext&)>;
using RenderGraphNodeGpuCallback = std::function<void(RenderGraphNodeGpuContext&)>;

struct RenderGraphNodeCreateInfo
{
    std::string nodeName;
    RenderGraphNodeType type;
    QueueType queueType;
    std::vector<ResourceUse> resourceUses;
    std::vector<std::string> shaderNames;
    PSODesc psoDesc = {};
    RenderGraphNodeCpuCallback cpuCallback;
    RenderGraphNodeGpuCallback gpuCallback;
};

class RenderGraphBuilder
{
public:
    explicit RenderGraphBuilder(VkRenderDevice* renderDevice)
        : m_shaderAssetManager(renderDevice->GetDevice())
        , m_vkDevice(renderDevice->GetDevice())
        , m_descriptorSetManager(*GetDescriptorManager())
    {
        m_commandBuffers[0] = renderDevice->AllocateReusablePrimaryCommandbuffer();
        m_commandBuffers[1] = renderDevice->AllocateComputeCommandBuffer();
        m_commandBuffers[2] = renderDevice->AllocateImmediateCommandBuffer();
    }

    // Add a render graph node
    void AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo);

    // Add a dependency between two nodes
    void AddDependency(const std::string& fromNode, const std::string& toNode);

    // Build the render graph
    void Build();

    void Execute();

    // Retrieve the execution order of nodes
    std::vector<std::string> GetExecutionOrder() const;

    ~RenderGraphBuilder()
    {
        for (auto& rgn : m_compiledGraphNodes)
        {
            DestroyCompiledRenderGraphNode(rgn);
        }
        m_vkDevice = VK_NULL_HANDLE;
    }

private:
    struct RenderGraphNode
    {
        std::string name;
        std::vector<ResolvedResourceUse> resourceUses;
        std::array<ShaderKey, MAX_SHADER_STAGES> shaders;
        PSODesc psoDesc;
        RenderGraphNodeCpuCallback cpuCallback;
        RenderGraphNodeGpuCallback gpuCallBack;
    };

    struct CompiledRenderGraphNode
    {
        const RenderGraphNode* logicalRenderGraphNode;

        // Execution related structures
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        QueueType queueType = QueueType::GRAPHICS;
        VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        RenderGraphNodeCpuCallback cpuCallback;
        RenderGraphNodeGpuCallback gpuCallBack;
    };

    CompiledRenderGraphNode CompileRenderGraphNode(const RenderGraphNode& rgn);
    void DestroyCompiledRenderGraphNode(CompiledRenderGraphNode& rgn);

    std::unordered_map<std::string, RenderGraphNode> m_renderGraphNodes;
    std::vector<CompiledRenderGraphNode> m_compiledGraphNodes;

    DependencyGraph<std::string> m_dependencyGraph;
    std::unordered_map<ResourceHandle, uint32_t> m_resourceLastUsedVersion;  // Track last used version of resources
    ShaderAssetManager m_shaderAssetManager;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, static_cast<size_t>(QueueType::COUNT)> m_commandBuffers;
    RenderGraphDescriptorSets m_descriptorSetManager;
};
}  // namespace Muyo::RenderGraph
