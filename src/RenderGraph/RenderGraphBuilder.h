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
    // Ray tracing only: ray generation / miss / closest-hit shader names, in that order.
    // When queueType == RAY_TRACING these are compiled into a ray tracing pipeline (with a
    // graph-managed shader binding table) and the node automatically issues vkCmdTraceRaysKHR
    // over the extent of its first STORAGE_IMAGE resource. Resources are bound by their
    // explicit DescriptorBinding (reflection-derived set/binding), so a node can bind the TLAS,
    // storage images and uniform buffers it declares.
    std::vector<std::string> rtShaderNames;
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
        // Ray tracing shader keys: [0] = raygen, [1] = miss, [2] = closest hit.
        std::array<ShaderKey, 3> rtShaders;
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
        // Descriptor sets actually bound for this node. For semantic nodes these alias the
        // shared PER_VIEW/PER_OBJ/MATERIAL sets; for reflection-bound (compute/RT) nodes these are
        // freshly allocated sets owned by this compiled node.
        std::vector<VkDescriptorSet> descriptorSets;
        // True when descriptorSetLayouts/descriptorSets were derived from the shader reflection
        // (raw set/binding) rather than the built-in semantic sets. Such sets are destroyed with the node.
        bool ownsDescriptorSets = false;
        // True when this node is a ray tracing dispatch (pipeline is a RT pipeline).
        bool isRayTracing = false;
        // Shader binding table regions for ray tracing nodes.
        std::array<VkStridedDeviceAddressRegionKHR, 3> sbtRegions{};
        VkExtent2D traceExtent = {0, 0};
        QueueType queueType = QueueType::GRAPHICS;
        VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        RenderGraphNodeCallback execute;
    };

    CompiledRenderGraphNode CompileRenderGraphNode(const RenderGraphNode& rgn);
    void DestroyCompiledRenderGraphNode(CompiledRenderGraphNode& rgn);

    // Build descriptor set layouts + allocate sets from a compute/RT node's merged shader reflection.
    // Resources declared with an explicit DescriptorBinding are written into the matching set/binding.
    void BuildReflectionDescriptorSets(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                       const ShaderReflection& mergedReflection);

    // Build a ray tracing pipeline + shader binding table for a RAY_TRACING node.
    void BuildRayTracingPipeline(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                 const std::vector<VkShaderModule>& shaderModules);

    void RecordBarriers(VkCommandBuffer cmdBuf, const std::vector<ResolvedResourceUse>& resourceUses,
                        uint32_t queueFamily);

    // Emit queue-family ownership transfer barriers for resources crossing queues.
    void RecordQueueTransferBarriers(VkCommandBuffer cmdBuf, uint32_t srcQueueFamily, uint32_t dstQueueFamily,
                                     bool bAcquire, const std::vector<ResourceHandle>& handles);

    // Resolve a handle to its concrete resource: imported resources take priority,
    // otherwise look in the graph-owned resource manager.
    const IRenderResource* ResolveResource(const ResourceHandle& handle) const;

    // Queue routing. RAY_TRACING / COPY follow the graphics queue; only COMPUTE can run on the
    // dedicated async compute queue.
    static QueueType GetQueueKey(QueueType type);
    VkQueue GetQueueForType(QueueType type) const;
    uint32_t GetQueueFamilyForType(QueueType type) const;
    VkCommandBuffer AllocateCommandBufferForType(QueueType type) const;
    void FreeCommandBufferForType(QueueType type, VkCommandBuffer cmdBuf) const;

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
        uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;  // queue family that currently owns the resource
        // Set before recording a queue segment when the resource is handed over from another queue
        // family; consumed by RecordBarriers to emit an acquire barrier instead of a normal one.
        int32_t pendingAcquireFamily = -1;
    };

    std::unordered_map<ResourceHandle, ResourceAccessState> m_resourceAccessStates;

    std::unordered_map<std::string, RenderGraphNode> m_renderGraphNodes;
    std::vector<CompiledRenderGraphNode> m_compiledGraphNodes;

    DependencyGraph<std::string> m_dependencyGraph;
    std::unordered_map<ResourceHandle, uint32_t> m_resourceLastUsedVersion;  // Track last used version of resources
    ShaderAssetManager m_shaderAssetManager;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    RenderGraphDescriptorSets m_descriptorSetManager;

    ResourceDescRegistry m_resourceDescRegistry;
    std::unordered_map<ResourceHandle, const IRenderResource*> m_importedResources;
};
}  // namespace Muyo::RenderGraph
