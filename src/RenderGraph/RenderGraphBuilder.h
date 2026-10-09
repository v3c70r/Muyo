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

#include "CompiledRenderGraph.h"
#include "DependencyGraph.h"
#include "MeshResourceManager.h"
#include "PSODesc.h"
#include "PerObjResourceManager.h"
#include "RenderGraphDescriptorSets.h"
#include "RenderGraphExecutionPlan.h"
#include "RenderGraphExecutor.h"
#include "RenderGraphNodeContext.h"
#include "RenderGraphNodeResource.h"
#include "RenderGraphResourceDesc.h"
#include "ShaderAsset.h"

namespace Muyo::RenderGraph
{
/// Map of every declared resource handle to its allocation description.
using ResourceDescRegistry = std::unordered_map<ResourceHandle, ResourceDesc>;

/// Maximum number of shader stages a node may declare (vertex + fragment, or raygen/miss/hit...).
static constexpr int MAX_SHADER_STAGES = 8;

/// A value supplied for one of a shader's specialization constants.
///
/// Its width is deliberately absent: the shader's reflection knows whether the constant is 8, 16, 32 or
/// 64 bits, so the graph takes the width from there. A caller therefore cannot disagree with the shader
/// about the size of its own constant, which is the failure a raw byte array invites.
struct SpecializationValue
{
    uint32_t id = 0;     ///< The `constant_id` the shader declared.
    uint64_t value = 0;  ///< Interpreted as the constant's declared width, little-endian.
};

/// User-facing declaration of a single render graph node (pass).
struct RenderGraphNodeCreateInfo
{
    std::string nodeName;                       ///< Unique node name (used by AddDependency).
    QueueType queueType = QueueType::GRAPHICS;  ///< Queue the node runs on.
    /// Opt-in: only when set (and queueType == COMPUTE) may the node be scheduled on the dedicated
    /// async compute queue and run concurrently with the graphics queue. Without it the node is
    /// recorded on the graphics queue, so no cross-queue synchronization is generated for it.
    bool async = false;
    std::vector<ResourceUse> resourceUses;  ///< Resources the node reads/writes.
    std::vector<std::string> shaderNames;   ///< Shader names for graphics (vert+frag) or compute.
    /// Values for this node's shader specialization constants, by `constant_id`.
    ///
    /// A shader that declares a constant and is not given one keeps the default baked into its SPIR-V,
    /// which is rarely what a caller wants: llama.cpp's compute shaders make the workgroup size and the
    /// operand types specialization constants, so the baked defaults read out of bounds or compute the
    /// wrong shape. An id that none of the node's shaders declare is a Build() error rather than an
    /// ignored entry, because the driver ignores unknown ids silently and the symptom would be a wrong
    /// number rather than a failure.
    std::vector<SpecializationValue> specializationConstants;
    /// Ray tracing only: ray generation / miss / closest-hit shader names, in that order.
    /// When queueType == RAY_TRACING these are compiled into a ray tracing pipeline (with a
    /// graph-managed shader binding table) and the node automatically issues vkCmdTraceRaysKHR
    /// over the extent of its first STORAGE_IMAGE resource. Resources are bound by their
    /// explicit DescriptorBinding (reflection-derived set/binding), so a node can bind the TLAS,
    /// storage images and uniform buffers it declares.
    std::vector<std::string> rtShaderNames;
    PSODesc psoDesc = {};   ///< Graphics pipeline state (ignored for compute/RT).
    uint32_t costHint = 1;  ///< Reserved for the future scheduler.
    /// Optional clear values for the node's attachments, in attachment declaration order.
    std::vector<VkClearValue> attachmentClearValues;
    RenderGraphNodeCallback execute;  ///< Records the node's work.
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
    /// Record and submit without waiting for the GPU. Note this does **not** enable overlap: the
    /// builder owns a single in-flight slot, so a second call blocks in AcquireSlot until the
    /// first completes. Overlap is an executor-API concern, see issue #11.
    /// @param info External semaphores to wait on before, and signal after, this execution.
    void Execute(const RenderGraphExecuteInfo& info);

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
        std::vector<SpecializationValue> specializationConstants;
        RenderGraphNodeCallback execute;
    };

    CompiledRenderGraphNode CompileRenderGraphNode(const RenderGraphNode& rgn);

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

    // Queue routing. `GetQueueKey` (which node runs where) lives with the execution plan in
    // RenderGraphExecutionPlan.h; these resolve a queue key against this device. Submission itself
    // - queues, handover semaphores, waiting - belongs to RenderGraphExecutor.
    uint32_t GetQueueFamilyForType(QueueType type) const;
    RenderGraphQueueFamilies GetQueueFamilies() const;

    // Auto wraps a graphics node's work in vkCmdBeginRendering/vkCmdEndRendering.
    bool BeginRendering(VkCommandBuffer cmdBuf, const CompiledRenderGraphNode& rgn, RenderGraphNodeContext& ctx,
                        std::unordered_set<ResourceHandle>& alreadyWritten,
                        std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                        std::vector<VkClearValue>& clearValues);

    // Tracked resource access state for barrier generation between nodes.
    struct ResourceAccessState
    {
        bool seen = false;
        VkAccessFlags2 lastAccess = 0;
        VkImageLayout lastLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ResourceIOType lastIo = ResourceIOType::READ;    // previous access direction (WAW/WAR hazards)
        bool writtenByCpu = false;                       // last writer was a CPU node; needs host flush
        uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;  // queue family that currently owns the resource
        // Set before recording a queue segment when the resource is handed over from another queue
        // family; consumed by RecordBarriers to emit an acquire barrier instead of a normal one.
        int32_t pendingAcquireFamily = -1;
    };

    std::unordered_map<ResourceHandle, ResourceAccessState> m_resourceAccessStates;

    std::unordered_map<std::string, RenderGraphNode> m_renderGraphNodes;
    CompiledRenderGraph m_compiledGraph;

    DependencyGraph<std::string> m_dependencyGraph;
    ShaderAssetManager m_shaderAssetManager;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    RenderGraphDescriptorSets m_descriptorSetManager;
    // Owns submission, cross-queue synchronization and waiting. The graph only records.
    RenderGraphExecutor m_executor;

    ResourceDescRegistry m_resourceDescRegistry;
    std::unordered_map<ResourceHandle, const IRenderResource*> m_importedResources;
};
}  // namespace Muyo::RenderGraph
