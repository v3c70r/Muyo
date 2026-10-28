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
#include "DescriptorHeapService.h"
#include "PSODesc.h"
#include "PerObjResourceManager.h"
#include "RenderGraphBarrier.h"
#include "RenderGraphExecutionPlan.h"
#include "RenderGraphExecutor.h"
#include "RenderGraphNodeContext.h"
#include "RenderGraphNodeResource.h"
#include "RenderGraphResourceDesc.h"
#include "RenderGraphTrace.h"
#include "ShaderAsset.h"
#include "TransientMemoryPlanner.h"
#include "TransientResources.h"

namespace Muyo::RenderGraph
{
class PipelineCache;
class ShaderCache;

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
    /// Opt-in: only when set (and queueType == COMPUTE) may the node run concurrently with the graphics
    /// queue. Without it the node is recorded on the graphics queue, so no cross-queue work is generated.
    ///
    /// On a device without a dedicated compute family the node still gets its own compute segment, but on
    /// the graphics family, so it is **serialised rather than concurrent**. `Build()` warns once, and
    /// `RenderDevice::IsComputeQueueDedicated()` is the query an author uses to choose per device.
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
    /// When queueType == RAY_TRACING these are compiled into a heap-flagged ray tracing pipeline (with a
    /// graph-managed shader binding table) and the node automatically issues vkCmdTraceRaysKHR over the
    /// extent of its first STORAGE_IMAGE resource. The TLAS, storage image and buffer handles reach the
    /// shaders through `PushData`, like every other heap node.
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
/// Deliberately not here yet: a shader source provider (the graph still loads `.spv` by file name).
/// That is a capability rather than de-globalisation, and it belongs with the guarantee it closes -
/// "a client can supply SPIR-V bytes".
struct RenderGraphServices
{
    /// Device the graph creates pipelines, command buffers and synchronisation with. Required.
    VkRenderDevice* device = nullptr;
    /// Allocates and owns the graph's declared resources. Required.
    RenderResourceManager* resourceManager = nullptr;
    /// The B-track heap service. Required for every GPU node since B1b; the graph rejects a missing
    /// service by name. The descriptor-set allocator is no longer a graph service.
    DescriptorHeapService* descriptorHeap = nullptr;
    /// Optional pipeline cache shared by every builder that is given the same instance (#17).
    ///
    /// When null, each builder keeps the per-builder cache it has always had and owns its entries. When
    /// set, a cached pipeline belongs to this object and the builder only borrows it: destroying one
    /// builder, or calling its `ReleaseCachedDeviceObjects()` for a swapchain recreation, therefore does
    /// not invalidate a pipeline another builder or graph still uses. The cache is device-scoped, must
    /// outlive every builder that refers to it, and is validated against the builder's `VkDevice` at
    /// construction. See `PipelineCache`.
    PipelineCache* pipelineCache = nullptr;
    /// Optional shader module/reflection cache shared by every builder that is given the same instance
    /// (#17). When null, each builder loads and owns its shader modules as before.
    ///
    /// A shared entry is keyed by shader name and SPIR-V content and owns its `VkShaderModule`; the
    /// builder only resolves it. The cache must outlive every builder that refers to it and is validated
    /// against the builder's `VkDevice` at construction. See `ShaderCache`.
    ShaderCache* shaderCache = nullptr;
};

/// Declares and runs a render graph.
///
/// Typical use:
/// @code
/// RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
///                                               .resourceManager = GetRenderResourceManager(),
///                                               .descriptorHeap = &heap});
/// builder.AddResource("Color", ImageResourceDesc{ .format = ..., .extent = ..., .usage = ... });
/// builder.ImportResource("Vertices", meshManager.m_pVertexBuffer);
/// builder.AddNode({ .nodeName = "Opaque", .queueType = QueueType::GRAPHICS, ... });
/// builder.Build();
/// builder.Execute();
/// @endcode
///
/// `Build()` is idempotent for a fixed declaration: it topologically sorts the nodes, allocates
/// graph-owned resources, writes descriptor-heap slots, compiles pipelines and plans barriers.
/// `Execute()` records and submits the frame, splitting work across queues when nodes opt into async
/// compute.
class RenderGraphBuilder
{
public:
    /// @param services The device, resource allocator and descriptor allocator the graph uses. Each is
    ///                 required; a missing one is reported by name rather than dereferenced later, and
    ///                 the check throws so it survives `NDEBUG`.
    /// @param inFlightCount How many executions may be in flight at once. The default of 1 serialises
    ///                 them. Above 1, each slot owns its command buffers and root arena, but graph-owned
    ///                 resources are still shared: a caller that overlaps resource-bearing executions
    ///                 must keep their resources distinct or wait. B2b-2 makes the resource half safe.
    explicit RenderGraphBuilder(const RenderGraphServices& services, uint32_t inFlightCount = 1);
    /// Transitional: fills the remaining services from the engine's globals, for the callers that still
    /// use them. Prefer the constructor above - this one keeps the dependency that #53 removes, which is
    /// why it is documented as the transition rather than as an equal alternative.
    /// @param renderDevice Device used to create pipelines, command buffers and synchronisation.
    /// @param inFlightCount See the services constructor; the default serialises executions.
    explicit RenderGraphBuilder(VkRenderDevice* renderDevice, uint32_t inFlightCount = 1);
    /// Releases the pipelines this builder created and its command buffers. A pipeline borrowed from an
    /// injected `PipelineCache` is not released: that cache owns it and may still serve another builder.
    ~RenderGraphBuilder();

    /// @return The graph compiled by the last successful Build(); empty before the first and after a
    ///         failed Build. Exposed for diagnostics and tests that inspect the compiled pipelines.
    const CompiledRenderGraph& GetCompiledGraph() const { return m_compiledGraph; }

    /// Block until every submitted execution has completed. Does not record or submit work; use it
    /// before reading back an imported resource written by a non-blocking Execute().
    void WaitIdle();

    /// Release the descriptor slots and the pipelines this builder owns from earlier Builds. Call only
    /// when every execution that used them has completed - the app's swapchain recreation does this right
    /// after `vkDeviceWaitIdle`. The next Build recreates what it needs; graph-owned resources are
    /// untouched.
    ///
    /// A pipeline borrowed from an injected `PipelineCache` is deliberately left alone: another graph may
    /// still be using it, and the cache is the thing that knows when it is safe to release entries. The
    /// swapchain's formats are part of the pipeline key, so a recreated swapchain with a different format
    /// compiles a new pipeline on the next Build rather than reusing one built for the old format.
    void ReleaseCachedDeviceObjects();

    // ── Resource declaration (data) ──────────────────────────────────────────
    /// Declare a graph-owned resource. It is allocated at `Build()` from `desc`.
    /// @param handle Name the graph and the nodes use for it.
    /// @param desc   Allocation: buffer size, or image format/extent/usage, plus its lifetime.
    /// @return `*this` for chaining.
    /// @throws std::runtime_error if `desc` cannot describe an allocation - a zero `count`/`stride`, a
    ///         zero extent, `VK_FORMAT_UNDEFINED`, zero mips/layers, or `usage == 0` - or if it asks for
    ///         `ResourceLifetime::Imported`; use `ImportResource` for a resource the graph does not own.
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
    /// Declare a node.
    /// @throws std::runtime_error if the name is already used; a resource use names something neither
    ///         declared with `AddResource` nor imported; a use's `kind` or `usage` does not agree with the
    ///         resource it names (a sampled-only image bound as `STORAGE_IMAGE`, an image role on a
    ///         buffer); a node's shader count does not match its queue type; a non-CPU node has no
    ///         `execute` callback; or its attachments resolve to different extents.
    void AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo);
    /// Add an ordering edge: `toNode` runs after `fromNode`. Throws if either node is unknown or
    /// if the edge would create a cycle.
    void AddDependency(const std::string& fromNode, const std::string& toNode);

    // ── Build / Execute ──────────────────────────────────────────────────────
    /// Compile the graph: topological sort, resource allocation, pipeline/descriptor compilation
    /// and barrier planning. Call after all resources and nodes are declared.
    /// @throws std::runtime_error for a declaration error, naming the node and resource: a dependency
    ///         cycle; a transient read with no writer; and anything `CompileRenderGraphNode` rejects
    ///         (shader load, specialization ids, buffer range, a missing heap service).
    ///         `docs/RenderGraph.md` has the failure index.
    void Build();
    /// Run every node once in execution order, inserting barriers between nodes and synchronising
    /// cross-queue handovers.
    void Execute();
    /// Receive a trace of every execution from now on.
    ///
    /// The sink's presence *is* the switch: there is no separate enabled flag to get out of step with it. The trace
    /// is handed over by value, so a sink that keeps history owns it, and a sink that formats it can drop it.
    ///
    /// Cost when unset is one null check per node and per submit; when set, the plan half is strings the compiled
    /// graph already owns.
    /// @param sink Called after each execution completes submission, with that execution's record.
    void SetTraceSink(std::function<void(RenderGraphTrace)> sink) { m_traceSink = std::move(sink); }

    /// Record and submit without waiting for the GPU. Note this does **not** enable overlap: the
    /// builder owns a single in-flight slot, so a second call blocks in AcquireSlot until the
    /// first completes. Overlap is an executor-API concern, see issue #11.
    /// @param info External semaphores to wait on before, and signal after, this execution.
    void Execute(const RenderGraphExecuteInfo& info);

    /// @return The node names in dependency (topological) order.
    std::vector<std::string> GetExecutionOrder() const;

    /// @return The plan the last `Build()` produced: queue segments and cross-queue transfers. Derived,
    ///         read-only state, empty before the first `Build()`. It is what tells a caller whether an
    ///         `async` node actually got a cross-queue handover or was serialised on one family.
    const RenderGraphExecutionPlan& GetExecutionPlan() const { return m_compiledGraph.GetExecutionPlan(); }

    /// Render what `Build()` compiled as a Mermaid flowchart, **without executing anything**.
    ///
    /// The diagram is the plan, not a trace: segments as nodes labelled with their queue and node names, and dependency
    /// edges labelled with the resource that forces them. It answers "what did the graph compile to" - which queue each
    /// node landed on, where the segments split, what is handed across a queue-family boundary - and needs no GPU work,
    /// where a `RenderGraphTrace` describes one *execution* and therefore needs an `Execute()`.
    ///
    /// Markdown files and GitHub issues render this inline, so it is the cheapest way to put a graph in a review.
    /// Empty before `Build()`, because there is no plan to draw yet.
    /// @return A Mermaid `flowchart LR` document.
    std::string ToMermaid() const;

    /// Render what `Build()` compiled as Graphviz DOT, for `dot -Tsvg` where no browser is involved.
    /// @return A DOT document. Empty before `Build()`.
    std::string ToDot() const;

private:
    struct RenderGraphNode
    {
        std::string name;
        QueueType queueType = QueueType::GRAPHICS;
        bool async = false;
        std::vector<ResolvedResourceUse> resourceUses;
        std::array<ShaderAssetKey, MAX_SHADER_STAGES> shaders;
        // Ray tracing shader keys: [0] = raygen, [1] = miss, [2] = closest hit.
        std::array<ShaderAssetKey, 3> rtShaders;
        PSODesc psoDesc = {};
        uint32_t costHint = 1;
        std::vector<VkClearValue> attachmentClearValues;
        std::vector<SpecializationValue> specializationConstants;
        RenderGraphNodeCallback execute;
    };

    CompiledRenderGraphNode CompileRenderGraphNode(const RenderGraphNode& rgn);

    /// One in-flight execution's host-visible, device-addressable root arena.
    ///
    /// Owned per executor slot rather than per builder: two executions can be recording at once, and a
    /// shared arena would let the later execution overwrite root data the earlier one's GPU work has
    /// not read yet. A slot's cursor is reset only after AcquireSlot has waited for that slot.
    struct RootArena
    {
        VkBuffer buffer = VK_NULL_HANDLE;           ///< Host-visible root buffer.
        VmaAllocation allocation = VK_NULL_HANDLE;  ///< Its VMA allocation.
        std::byte* mapped = nullptr;                ///< Persistent mapping.
        VkDeviceAddress address = 0;                ///< Device address of offset 0.
        VkDeviceSize cursor = 0;                    ///< Next free byte in this execution.
    };
    /// Create the root arena for `slot` on first use; it is host-visible and device-addressable.
    void EnsureRootArena(uint32_t slot);
    /// @return A slice of the current execution's root arena, or throw when it does not fit.
    RenderGraphRootSlice AllocateRoot(size_t size, size_t alignment);

    // Build a ray tracing pipeline + shader binding table for a RAY_TRACING node.
    void BuildRayTracingPipeline(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                 const std::vector<VkShaderModule>& shaderModules);

    /// The compiled plan as a diagram graph: segments, and the edges that force their order.
    /// @return Segments and labelled edges, from what `Build()` compiled.
    RenderGraph::TraceGraph BuildPlanGraph() const;

    /// Device and topology, as a trace records them. Read once per traced execution, never from the graph itself.
    /// @return The scope a trace produced now should carry.
    TraceScope BuildTraceScope() const;

    /// Record a node's barriers into its segment's command buffer.
    /// @param cmdBuf       Command buffer the node's barriers are emitted into.
    /// @param resourceUses The node's uses, in declaration order.
    /// @param queueFamily  Family the node is recorded on; decides whether a handover is needed.
    /// @param pObserver    Optional observer told about each use's barriers, with the resource it belongs to.
    void RecordBarriers(VkCommandBuffer cmdBuf, const std::vector<ResolvedResourceUse>& resourceUses,
                        uint32_t queueFamily, BarrierObserver* pObserver = nullptr);

    // Stage bits the device enables. `ResourceUseResolver`'s table names a union of every stage that can
    // touch a resource; a stage whose feature the device does not enable may not appear in a barrier mask
    // at all (VUID-VkImageMemoryBarrier2-dstStageMask-07946), so the barrier masks are filtered by this.
    VkPipelineStageFlags2 SupportedStages() const;

    /// Emit the post-node transition to `PRESENT_SRC_KHR` for a node's present targets.
    ///
    /// Present is the one layout whose consumer is outside the graph, so its barrier cannot be emitted at
    /// the start of a consumer the way every other use's is. This runs after the node's render pass.
    /// @param cmdBuf       Command buffer being recorded.
    /// @param resourceUses The node's uses; the `PRESENT` ones are transitioned.
    void RecordPresentTransition(VkCommandBuffer cmdBuf, const std::vector<ResolvedResourceUse>& resourceUses);

    // Emit queue-family ownership transfer barriers for resources crossing queues.
    /// @param cmdBuf         Command buffer the release barrier is emitted into.
    /// @param srcQueueFamily Family releasing ownership.
    /// @param dstQueueFamily Family taking it.
    /// @param bAcquire       True for the acquire half, which only records pending state.
    /// @param handles        Resources being handed over.
    /// @param pObserver      Optional observer told about each release barrier, with the resource it belongs to.
    void RecordQueueTransferBarriers(VkCommandBuffer cmdBuf, uint32_t srcQueueFamily, uint32_t dstQueueFamily,
                                     bool bAcquire, const std::vector<ResourceHandle>& handles,
                                     BarrierObserver* pObserver = nullptr);

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

    // Tracked resource access state for barrier generation between nodes. The derivation lives in
    // RenderGraphBarrier.h, as a free function, so the masks can be tested without a device.
    std::unordered_map<ResourceHandle, BarrierPriorState> m_resourceAccessStates;

    std::unordered_map<std::string, RenderGraphNode> m_renderGraphNodes;
    CompiledRenderGraph m_compiledGraph;

    DependencyGraph<std::string> m_dependencyGraph;
    /// The injected services, declared **before** the members whose construction dereferences them: a
    /// member initialiser list runs in declaration order, so putting this first is what lets
    /// `RequireServices` reject a missing one by name before anything dereferences it.
    RenderGraphServices m_services;
    /// Owned fallback for callers that did not inject a service; B1b-5b lets tests/builder construction
    /// opt into heap nodes without threading a service, and the descriptor lifetime follows the graph.
    std::unique_ptr<DescriptorHeapService> m_ownedDescriptorHeap;
    ShaderAssetManager m_shaderAssetManager;
    /// Load a shader through the injected `ShaderCache` when there is one, otherwise through the
    /// per-builder manager. AddNode and CompileRenderGraphNode both go through these two helpers so the
    /// load and the resolve cannot disagree about which cache the key belongs to.
    /// @param shaderName Shader name without the `.spv` suffix.
    /// @return The loaded shader's identity, or `std::nullopt` when it could not be loaded.
    std::optional<ShaderAssetKey> LoadShaderAsset(const std::string& shaderName);
    /// @param key Key returned by the matching `LoadShaderAsset` call.
    /// @return The module, words and reflection, or nullptr when the key is unknown.
    const ShaderAsset* ResolveShaderAsset(const ShaderAssetKey& key) const;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkDeviceSize m_minUniformBufferOffsetAlignment = 1;
    VkDeviceSize m_minStorageBufferOffsetAlignment = 1;
    // Owns submission, cross-queue synchronization and waiting. The graph only records.
    RenderGraphExecutor m_executor;
    uint64_t m_nextExecution = 0;                       ///< Execution counter a trace is labelled with.
    std::function<void(RenderGraphTrace)> m_traceSink;  ///< Set by SetTraceSink; empty means no trace is recorded.
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
    /// Host-visible root arenas, one per in-flight slot (B1b-4 / B2b). The host owns each layout.
    static constexpr VkDeviceSize kRootCapacity = 64ULL * 1024;
    std::vector<RootArena> m_rootArenas;
    uint32_t m_currentRootSlot = 0;

    /// Content key for a descriptor-heap slot (B2b-2).
    ///
    /// Keyed on what the descriptor *says*, not on the resource handle: a repeated Build with the same
    /// resource hits, while a recreated resource at a new image/address misses even though its handle is
    /// unchanged. Image entries compare the view create info and layout; buffer and acceleration-structure
    /// entries compare device address and, for buffers, the range.
    struct DescriptorKey
    {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;  ///< Descriptor type.
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;     ///< Image layout, undefined for the others.
        VkDeviceAddress address = 0;                          ///< Buffer / AS device address.
        VkDeviceSize size = 0;                                ///< Buffer range in bytes.
        VkImageViewCreateInfo view{};                         ///< Image view; unused by buffers and AS.
        /// @param other Key to compare.
        /// @return True when both keys describe the same descriptor content.
        bool operator==(const DescriptorKey& other) const;
    };
    /// Hashes the fields `DescriptorKey::operator==` compares.
    struct DescriptorKeyHash
    {
        /// @param key Key to hash.
        /// @return Hash over the descriptor's content.
        std::size_t operator()(const DescriptorKey& key) const;
    };
    /// Static slots reused across builds, keyed by descriptor content.
    std::unordered_map<DescriptorKey, DescriptorSlot, DescriptorKeyHash> m_descriptorCache;
    /// Release every cached static slot; only after the GPU is known idle.
    void ReleaseCachedDescriptors();
    /// Pipelines reused across builds when no shared `PipelineCache` is injected, keyed on every input
    /// that changes the created pipeline but not on any descriptor or layout state (B2c/#17). Owned here;
    /// compiled nodes do not destroy them. With a shared cache injected this map stays empty and the
    /// cache's entries are borrowed instead.
    std::unordered_map<std::string, VkPipeline> m_pipelineCache;
    /// Destroy every pipeline in the per-builder fallback cache; only after the GPU is known idle.
    /// Entries in an injected shared cache are never touched by this.
    void DestroyCachedPipelines();
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
