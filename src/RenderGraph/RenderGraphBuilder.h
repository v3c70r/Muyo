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
#include "DescriptorManager.h"
#include "PSODesc.h"
#include "PerObjResourceManager.h"
#include "RenderGraphExecutionPlan.h"
#include "RenderGraphExecutor.h"
#include "RenderGraphNodeContext.h"
#include "RenderGraphNodeResource.h"
#include "RenderGraphResourceDesc.h"
#include "ShaderAsset.h"
#include "TransientMemoryPlanner.h"
#include "TransientResources.h"

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

/// The services a graph is *given*, rather than looking up as globals (#53).
///
/// The split follows the design document: the graph owns the **policy** - transient lifetimes, barriers,
/// scheduling, descriptor content - while the **mechanism** belongs to the host. The device, the memory
/// and the descriptors are the host's, so they arrive by construction. A caller that supplies all three
/// runs a graph without the engine's globals, which is what lets a compute-only consumer use it without
/// the renderer. `RenderGraphBuilder(VkRenderDevice*)` still fills the other two from those globals for
/// in-tree callers that have not moved, and is marked as the transition rather than the shape.
///
/// Deliberately not here yet: a shader source provider (the graph still loads `.spv` by file name) and a
/// pipeline cache (there is no channel for one at all). Both are capabilities rather than
/// de-globalisation, and each belongs with the guarantee it closes - "a client can supply SPIR-V bytes"
/// and the cost work.
struct RenderGraphServices
{
    /// Device the graph creates pipelines, command buffers and synchronisation with. Required.
    VkRenderDevice* device = nullptr;
    /// Allocates and owns the graph's declared resources. Required.
    RenderResourceManager* resourceManager = nullptr;
    /// Allocates descriptor set layouts and sets. Required.
    DescriptorManager* descriptorManager = nullptr;
};

/// Declares and runs a render graph.
///
/// Typical use:
/// @code
/// RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
///                                               .resourceManager = GetRenderResourceManager(),
///                                               .descriptorManager = GetDescriptorManager()});
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
    /// @param services The device, resource allocator and descriptor allocator the graph uses. Each is
    ///                 required; a missing one is reported by name rather than dereferenced later, and
    ///                 the check throws so it survives `NDEBUG`.
    explicit RenderGraphBuilder(const RenderGraphServices& services);
    /// Transitional: fills the remaining services from the engine's globals, for the callers that still
    /// use them. Prefer the constructor above - this one keeps the dependency that #53 removes, which is
    /// why it is documented as the transition rather than as an equal alternative.
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

    /// Resolve a handle to the resource it names: a transient the graph owns (bound into an aliasing
    /// block), one the caller imported, or one the engine's resource manager owns.
    ///
    /// This is the graph's own resolution order, exposed because a caller reading back what a node wrote to
    /// a graph-owned transient needs the same answer the graph's barrier and descriptor paths use. A
    /// graph-owned transient is deliberately **not** registered with the resource manager: its memory
    /// belongs to a block the graph frees, and a manager-owned copy would be a second resource under one
    /// name with a different lifetime and a different address.
    /// @param handle Resource handle.
    /// @return The resource, or nullptr when the handle names nothing the graph can see.
    const IRenderResource* ResolveResource(const ResourceHandle& handle) const;

    /// @return The last build's transient memory plan: the blocks, their slots, and the two byte totals.
    ///         `reservedBytes < individualBytes` is what sharing memory looks like from outside, and
    ///         `reservedBytes == individualBytes` is a legitimate answer for a graph whose transients all
    ///         overlap in time. Empty before the first build.
    const TransientMemoryPlan& GetTransientMemoryPlan() const { return m_transientMemoryPlan; }

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

    // The offset/range to write into VkDescriptorBufferInfo for a buffer binding, validated against
    // the buffer and this device's alignment limit. Whole buffer when the use declares no range.
    std::pair<VkDeviceSize, VkDeviceSize> ResolveBufferRange(const ResourceHandle& nodeName,
                                                             const ResolvedResourceUse& use,
                                                             const BufferResource& buffer,
                                                             VkDescriptorType descriptorType) const;

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

    // A graph-owned transient's live interval, in execution order.
    //
    // Derived from first to last use. It is the input aliasing needs - two transients may share memory
    // exactly when their intervals do not overlap - and it is what makes the read-before-write check
    // below a statement about a range rather than about one node.
    struct TransientLiveRange
    {
        size_t firstUse = 0;
        size_t lastUse = 0;
    };

    // Derive every graph-owned transient's live range and reject a read that no write precedes.
    //
    // A transient promises content from its first write to its last read *within one execution*, so a
    // read with nothing writing it first is a declaration error. Without the check it silently reads
    // whatever the memory holds: the first reader's barrier moves an image out of
    // VK_IMAGE_LAYOUT_UNDEFINED, and a transition from UNDEFINED discards contents rather than keeping
    // them. `Persistent` and `Imported` are exempt because their content is not this execution's to
    // produce - which is what declaring either of them means.
    void ValidateTransientLifetimes(const std::vector<std::string>& executionOrder);

    // Live ranges from the last `Build()`, keyed by handle. Nothing reads this until aliasing lands;
    // it is kept because re-deriving it later would mean re-walking the order for a fact this pass
    // already computed.
    std::unordered_map<ResourceHandle, TransientLiveRange> m_transientLiveRanges;

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
    /// The injected services, declared **before** the members whose construction dereferences them: a
    /// member initialiser list runs in declaration order, so putting this first is what lets
    /// `RequireServices` reject a missing one by name before anything dereferences it.
    RenderGraphServices m_services;
    ShaderAssetManager m_shaderAssetManager;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkDeviceSize m_minUniformBufferOffsetAlignment = 1;
    VkDeviceSize m_minStorageBufferOffsetAlignment = 1;
    // Owns submission, cross-queue synchronization and waiting. The graph only records.
    RenderGraphExecutor m_executor;

    /// Transient buffers whose memory the graph owns as aliasing blocks rather than per resource.
    ///
    /// Keyed by handle so `ResolveResource` finds them, and owned here rather than by the resource
    /// manager, because their memory belongs to a block and the block's lifetime is the graph's. They are
    /// destroyed before the blocks are freed, since a block cannot be released while something is still
    /// bound to it, and they are recreated on every build - unlike manager-owned resources - because the
    /// plan, and therefore the offsets, can differ between builds.
    std::unordered_map<ResourceHandle, std::unique_ptr<TransientBufferResource>> m_aliasedBuffers;
    /// Transient images bound into those same blocks. Separate map, same ownership and same teardown:
    /// the two differ only in which allocator call binds them and whether they can be mapped.
    std::unordered_map<ResourceHandle, std::unique_ptr<TransientImageResource>> m_aliasedImages;
    /// The blocks those buffers are bound into. Freed after every resource above is destroyed.
    std::vector<VmaAllocation> m_aliasBlocks;
    /// The last plan's numbers, so a caller can ask what aliasing bought without reaching into the build.
    TransientMemoryPlan m_transientMemoryPlan;

    /// Create this build's aliased transients, plan their memory, and bind them into blocks.
    /// @param executionOrder Node order, which is what the live ranges are indexed by.
    void BuildAliasedTransients(const std::vector<std::string>& executionOrder);
    /// Destroy the aliased resources and then free their blocks. Safe to call repeatedly.
    void DestroyAliasedTransients();

    /// Throw when a node callback returned with a graph-owned transient still host-mapped.
    ///
    /// A transient buffer is mapped through its aliasing block, so the pointer is valid only while no
    /// other transient shares its slot. The live range promises that for the node whose use declared the
    /// resource and nothing beyond it, which is why the mapping must be released before the callback
    /// returns: a later node may be bound to the same bytes. Nothing enforced that, and a leaked pointer
    /// is a silent overwrite rather than an error, so this runs at every node boundary and names both the
    /// node and the resource.
    ///
    /// Cost is one `IsMapped()` scan of the graph's aliased buffers per node - O(nodes x transient
    /// buffers) per execution, and transient buffers are the only graph-owned resources that can be
    /// mapped.
    /// @param nodeName Node whose callback just returned, named in the error.
    void EnsureMappingsReleased(const std::string& nodeName) const;

    ResourceDescRegistry m_resourceDescRegistry;
    std::unordered_map<ResourceHandle, const IRenderResource*> m_importedResources;
};
}  // namespace Muyo::RenderGraph
