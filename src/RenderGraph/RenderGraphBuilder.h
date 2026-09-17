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
/// Map of every declared resource handle to its allocation description.
using ResourceDescRegistry = std::unordered_map<ResourceHandle, ResourceDesc>;

/// Maximum number of shader stages a node may declare (vertex + fragment, or raygen/miss/hit...).
static constexpr int MAX_SHADER_STAGES = 8;

/// Context handed to a node's execute callback.
///
/// The graph has already recorded the barriers, opened the render pass (graphics nodes) and bound
/// the pipeline and descriptor sets, so most callbacks only issue draw/dispatch/trace commands.
struct RenderGraphNodeContext
{
    QueueType queueType = QueueType::GRAPHICS;  ///< Queue this node is running on.
    RenderResourceManager& resourceManager;    ///< Global resource manager (graph-owned resources).
    MeshResourceManager& meshManager;          ///< Mesh manager (shared vertex/index buffers).

    // GPU-side fields (valid only for GPU nodes)
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;                        ///< Command buffer to record into.
    VkPipeline pipeline = VK_NULL_HANDLE;                                  ///< Bound pipeline.
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;                      ///< Bound pipeline layout.
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;    ///< Bind point for the pipeline.

    /// Resolve a graph-declared resource to its concrete pointer (allocated at Build()).
    /// @tparam T Concrete resource type (e.g. `BufferResource`, `RenderTarget`).
    /// @param handle Resource name used in the node's `resourceUses`.
    /// @return The resource, or `nullptr` if it is not of type `T`.
    template <class T>
    T* GetResource(const ResourceHandle& handle) const
    {
        return resourceManager.template GetResource<T>(handle);
    }
};

/// Callback a node provides to record its GPU or host work.
using RenderGraphNodeCallback = std::function<void(RenderGraphNodeContext&)>;

/// User-facing declaration of a single render graph node (pass).
struct RenderGraphNodeCreateInfo
{
    std::string nodeName;                       ///< Unique node name (used by AddDependency).
    QueueType queueType = QueueType::GRAPHICS;  ///< Queue the node runs on.
    /// Opt-in: only when set (and queueType == COMPUTE) may the node be scheduled on the dedicated
    /// async compute queue and run concurrently with the graphics queue. Without it the node is
    /// recorded on the graphics queue, so no cross-queue synchronization is generated for it.
    bool async = false;
    std::vector<ResourceUse> resourceUses;      ///< Resources the node reads/writes.
    std::vector<std::string> shaderNames;       ///< Shader names for graphics (vert+frag) or compute.
    /// Ray tracing only: ray generation / miss / closest-hit shader names, in that order.
    /// When queueType == RAY_TRACING these are compiled into a ray tracing pipeline (with a
    /// graph-managed shader binding table) and the node automatically issues vkCmdTraceRaysKHR
    /// over the extent of its first STORAGE_IMAGE resource. Resources are bound by their
    /// explicit DescriptorBinding (reflection-derived set/binding), so a node can bind the TLAS,
    /// storage images and uniform buffers it declares.
    std::vector<std::string> rtShaderNames;
    PSODesc psoDesc = {};                       ///< Graphics pipeline state (ignored for compute/RT).
    uint32_t costHint = 1;                      ///< Reserved for the future scheduler.
    /// Optional clear values for the node's attachments, in attachment declaration order.
    std::vector<VkClearValue> attachmentClearValues;
    RenderGraphNodeCallback execute;            ///< Records the node's work.
};

/// Declares and runs a render graph.
///
/// Typical use:
/// @code
/// RenderGraphBuilder builder(GetRenderDevice());
/// builder.AddResource("Color", ImageResourceDesc{ .format = ..., .extent = ..., .usage = ... });
/// builder.ImportResource("Vertices", meshManager.m_pVertexBuffer);
/// builder.AddNode({ .nodeName = "Opaque", .queueType = QueueType::GRAPHICS, ... });
/// builder.Build();
/// builder.Execute();
/// @endcode
///
/// `Build()` is idempotent for a fixed declaration: it topologically sorts the nodes, allocates
/// graph-owned resources, compiles pipelines/descriptor sets and plans barriers. `Execute()`
/// records and submits the frame, splitting work across queues when nodes opt into async compute.
class RenderGraphBuilder
{
public:
    /// @param renderDevice Device used to create pipelines, command buffers and synchronisation.
    explicit RenderGraphBuilder(VkRenderDevice* renderDevice);
    /// Releases compiled pipelines, descriptor sets and command buffers.
    ~RenderGraphBuilder();

    // ── Resource declaration (data) ──────────────────────────────────────────
    /// Declare a graph-owned resource. It is allocated at `Build()` from `desc`.
    /// @return `*this` for chaining.
    RenderGraphBuilder& AddResource(const ResourceHandle& handle, ResourceDesc desc);
    /// Register an externally owned resource (mesh buffers, scene data, TLAS...). Not allocated
    /// or freed by the graph.
    /// @return `*this` for chaining.
    RenderGraphBuilder& ImportResource(const ResourceHandle& handle, const IRenderResource* resource);

    /// @return The allocation description for a graph-owned resource, if any.
    std::optional<ResourceDesc> GetResourceDesc(const ResourceHandle& handle) const;

    // ── Node declaration ─────────────────────────────────────────────────────
    /// Declare a node. Throws if the name is already used.
    void AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo);
    /// Add an ordering edge: `toNode` runs after `fromNode`. Throws if either node is unknown or
    /// if the edge would create a cycle.
    void AddDependency(const std::string& fromNode, const std::string& toNode);

    // ── Build / Execute ──────────────────────────────────────────────────────
    /// Compile the graph: topological sort, resource allocation, pipeline/descriptor compilation
    /// and barrier planning. Call after all resources and nodes are declared.
    void Build();
    /// Run every node once in execution order, inserting barriers between nodes and synchronising
    /// cross-queue handovers.
    void Execute();

    /// @return The node names in dependency (topological) order.
    std::vector<std::string> GetExecutionOrder() const;

private:
    struct RenderGraphNode
    {
        std::string name;
        QueueType queueType = QueueType::GRAPHICS;
        bool async = false;
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

    // Queue routing. A node may only run on the dedicated async compute queue when it is
    // explicitly marked async (and is a compute node); everything else follows the graphics queue.
    static QueueType GetQueueKey(QueueType type, bool bAsync);
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
