#pragma once
#include <spirv_reflect.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DependencyGraph.h"
#include "MeshResourceManager.h"
#include "PSODesc.h"
#include "PerObjResourceManager.h"
#include "RenderGraphDescriptorSets.h"
#include "RenderGraphNodeResource.h"
#include "RenderGraphResourceDesc.h"
#include "ShaderAsset.h"

namespace Muyo::RenderGraph
{
using ResourceDescRegistry = std::unordered_map<ResourceHandle, ResourceDesc>;

static constexpr int MAX_SHADER_STAGES = 8;

// Context handed to a node's execute callback.
struct RenderGraphNodeContext
{
    QueueType queueType = QueueType::GRAPHICS;
    RenderResourceManager& resourceManager;
    MeshResourceManager& meshManager;

    // GPU-side fields (valid only for GPU nodes)
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Resolve a graph-declared resource to its concrete pointer (allocated at Build()).
    template <class T>
    T* GetResource(const ResourceHandle& handle) const
    {
        return resourceManager.template GetResource<T>(handle);
    }
};

using RenderGraphNodeCallback = std::function<void(RenderGraphNodeContext&)>;

// User-facing declaration of a single render graph node (pass).
struct RenderGraphNodeCreateInfo
{
    std::string nodeName;
    QueueType queueType = QueueType::GRAPHICS;
    std::vector<ResourceUse> resourceUses;
    std::vector<std::string> shaderNames;
    PSODesc psoDesc = {};
    uint32_t costHint = 1;  // reserved for the future scheduler
    // Optional clear values for the node's attachments, in attachment declaration order.
    std::vector<VkClearValue> attachmentClearValues;
    RenderGraphNodeCallback execute;
};

class RenderGraphBuilder
{
public:
    explicit RenderGraphBuilder(VkRenderDevice* renderDevice);
    ~RenderGraphBuilder();

    // ── Resource declaration (data) ──────────────────────────────────────────
    // Graph-owned: allocated at Build() via the desc.
    RenderGraphBuilder& AddResource(const ResourceHandle& handle, ResourceDesc desc);
    // Externally owned: used by graph nodes but not allocated by the graph.
    RenderGraphBuilder& ImportResource(const ResourceHandle& handle, const IRenderResource* resource);

    std::optional<ResourceDesc> GetResourceDesc(const ResourceHandle& handle) const;

    // ── Node declaration ─────────────────────────────────────────────────────
    void AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo);
    void AddDependency(const std::string& fromNode, const std::string& toNode);

    // ── Build / Execute ──────────────────────────────────────────────────────
    // Compiles the graph: topo sort, resource allocation, pipeline compilation, barrier planning.
    void Build();
    // Runs every node once in execution order, inserting barriers between them.
    void Execute();

    std::vector<std::string> GetExecutionOrder() const;

private:
    struct RenderGraphNode
    {
        std::string name;
        QueueType queueType = QueueType::GRAPHICS;
        std::vector<ResolvedResourceUse> resourceUses;
        std::array<ShaderKey, MAX_SHADER_STAGES> shaders;
        PSODesc psoDesc = {};
        uint32_t costHint = 1;
        std::vector<VkClearValue> attachmentClearValues;
        RenderGraphNodeCallback execute;
    };

    struct CompiledRenderGraphNode
    {
        const RenderGraphNode* logicalRenderGraphNode = nullptr;

        // Execution related structures
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        QueueType queueType = QueueType::GRAPHICS;
        VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        RenderGraphNodeCallback execute;
    };

    CompiledRenderGraphNode CompileRenderGraphNode(const RenderGraphNode& rgn);
    void DestroyCompiledRenderGraphNode(CompiledRenderGraphNode& rgn);

    void RecordBarriers(VkCommandBuffer cmdBuf, const std::vector<ResolvedResourceUse>& resourceUses);

    // Auto wraps a graphics node's work in vkCmdBeginRendering/vkCmdEndRendering.
    bool BeginRendering(VkCommandBuffer cmdBuf, const CompiledRenderGraphNode& rgn, RenderGraphNodeContext& ctx,
                        std::unordered_set<ResourceHandle>& alreadyWritten,
                        std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                        std::vector<VkClearValue>& clearValues);

    // Tracked resource access state for barrier generation between nodes.
    struct ResourceAccessState
    {
        bool seen = false;
        VkPipelineStageFlags2 lastStages = 0;
        VkAccessFlags2 lastAccess = 0;
        VkImageLayout lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool writtenByCpu = false;  // last writer was a CPU node; needs host flush
    };

    std::unordered_map<ResourceHandle, ResourceAccessState> m_resourceAccessStates;

    std::unordered_map<std::string, RenderGraphNode> m_renderGraphNodes;
    std::vector<CompiledRenderGraphNode> m_compiledGraphNodes;

    DependencyGraph<std::string> m_dependencyGraph;
    std::unordered_map<ResourceHandle, uint32_t> m_resourceLastUsedVersion;  // Track last used version of resources
    ShaderAssetManager m_shaderAssetManager;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, static_cast<size_t>(QueueType::COUNT)> m_commandBuffers;
    RenderGraphDescriptorSets m_descriptorSetManager;

    ResourceDescRegistry m_resourceDescRegistry;
    std::unordered_map<ResourceHandle, const IRenderResource*> m_importedResources;
};
}  // namespace Muyo::RenderGraph
