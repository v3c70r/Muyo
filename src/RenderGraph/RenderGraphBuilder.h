#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// The public headers below are the builder's *entire* interface. Its private state lives in an
// out-of-line `Impl` (defined in RenderGraphBuilder.cpp), so this header no longer pulls in the
// implementation headers that describe that state - the package can install the public surface only
// (#297). Do not include an internal header here: the out-of-tree consumer probe compiles against an
// installed prefix with no `include/Muyo/detail/RenderGraph/`.
#include "CompiledRenderGraph.h"
#include "DescriptorHeapService.h"
#include "PSODesc.h"
#include "RenderGraphExecuteInfo.h"
#include "RenderGraphExecutionPlan.h"
#include "RenderGraphNodeContext.h"
#include "RenderGraphNodeResource.h"
#include "RenderGraphResourceDesc.h"
#include "RenderGraphResourceHandle.h"
#include "RenderGraphResources.h"
#include "RenderGraphTrace.h"
#include "TransientMemoryPlan.h"

namespace Muyo
{
class VkRenderDevice;
}  // namespace Muyo

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

/// The host's capture-tool marker for a node scope.
///
/// A capture backend is host policy, and the graph must not reach one: `src/Debug/ScopedMarker.h` composes
/// this behaviour for the engine, and the graph used to depend on it just to emit a node marker (#272).
/// A host that wants marker scopes implements this interface around its backend and passes it in
/// `RenderGraphServices::captureMarker`; without one the graph emits only the Vulkan debug label.
class IGraphCaptureMarker
{
public:
    /// Destroy the marker. The host owns it and must outlive the builder that refers to it.
    virtual ~IGraphCaptureMarker() = default;
    /// Open a marker for a node.
    /// @param name Marker name, which is the node's name.
    virtual void Begin(const std::string& name) = 0;
    /// Close the marker opened by the matching `Begin`.
    virtual void End() = 0;
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
    /// Allocates the graph's declared resources. Required.
    ///
    /// The graph owns the policy (what to allocate, aliasing, reuse) and receives the mechanism as this
    /// interface. In this repository `muyo_device`'s `VkGraphResourceAllocator` implements it; a consumer
    /// with its own memory manager implements it instead. No engine resource type appears here (#272).
    IGraphResourceAllocator* resourceAllocator = nullptr;
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
    /// construction. See `PipelineCache`. The cache also owns a `VkPipelineCache`, and every pipeline a
    /// builder creates is recorded in it, so its serialized data survives a process restart when the
    /// caller loads it back.
    PipelineCache* pipelineCache = nullptr;
    /// Optional host capture-tool marker, opened and closed around each node's recorded scope.
    ///
    /// Null means no capture markers; the Vulkan debug label is emitted either way. See
    /// `IGraphCaptureMarker` for why this is injected rather than reached through the engine.
    IGraphCaptureMarker* captureMarker = nullptr;
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
/// RenderGraphBuilder builder(RenderGraphServices{.device = device,
///                                               .resourceAllocator = &allocator,
///                                               .descriptorHeap = &heap});
/// builder.AddResource("Color", ImageResourceDesc{ .format = ..., .extent = ..., .usage = ... });
/// builder.ImportResource("Vertices", meshManager.m_pVertexBuffer);
/// builder.AddNode({ .nodeName = "Opaque", .queueType = QueueType::GRAPHICS, ... });
/// builder.Build();
/// builder.Execute();
/// @endcode
///
/// `Build()` is idempotent for a fixed declaration: it topologically sorts the nodes, allocates
/// graph-owned resources, writes descriptor-heap slots, compiles pipelines and plans barriers. A repeated
/// build reuses the allocations, the compiled pipelines, the descriptor slots and - crucially - the
/// per-slot transient arenas, so a per-frame `Build()` does not reallocate a graph-owned transient. A
/// changed transient declaration or plan drains every in-flight execution before the old arenas are
/// destroyed; an unchanged plan never drains. `Execute()` records and submits the frame, splitting work
/// across queues when nodes opt into async compute.
class RenderGraphBuilder
{
public:
    /// @param services The device, resource allocator and descriptor allocator the graph uses. Each is
    ///                 required; a missing one is reported by name rather than dereferenced later, and
    ///                 the check throws so it survives `NDEBUG`.
    /// @param inFlightCount How many executions may be in flight at once. The default of 1 serialises
    ///                 them. Above 1, each slot owns its command buffers, root arena and a complete copy
    ///                 of the graph's transients (see `GetTransientMemoryPlan()`), and imported resources
    ///                 are bound per execution from that slot's descriptor ring. A graph-owned
    ///                 `Persistent` resource is shared by every execution and is therefore rejected
    ///                 above one slot; a caller that needs per-execution content imports it and
    ///                 re-points the handle between executions (#83).
    explicit RenderGraphBuilder(const RenderGraphServices& services, uint32_t inFlightCount = 1);
    /// Convenience: creates the device's VMA-backed resource allocator for `renderDevice`.
    ///
    /// This is what the in-tree callers that used to fall back to the engine resource manager now get.
    /// It reads no global: the allocator is constructed from the device and owned by the builder, so the
    /// graph still receives its allocator by construction (#272). Prefer the services constructor when
    /// you already own an allocator, because a second builder over the same device cannot share one
    /// created here.
    /// @param renderDevice Device used to create pipelines, command buffers and synchronisation.
    /// @param inFlightCount See the services constructor; the default serialises executions.
    explicit RenderGraphBuilder(VkRenderDevice* renderDevice, uint32_t inFlightCount = 1);
    /// Releases the pipelines this builder created and its command buffers. A pipeline borrowed from an
    /// injected `PipelineCache` is not released: that cache owns it and may still serve another builder.
    ~RenderGraphBuilder();

    /// @return The graph compiled by the last successful Build(); empty before the first and after a
    ///         failed Build. Exposed for diagnostics and tests that inspect the compiled pipelines.
    const CompiledRenderGraph& GetCompiledGraph() const;

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
    ///
    /// @param handle Name the graph and the nodes use for it.
    /// @param resource The resource; the graph never frees it.
    /// @param currentLayout For an imported **image**, the layout it is in when an execution begins.
    ///        The default, `VK_IMAGE_LAYOUT_UNDEFINED`, promises nothing about its contents: the first
    ///        node to use it transitions from `UNDEFINED`, which discards them. A caller importing
    ///        content produced outside the graph - a scene pass that has not been ported yet, or a
    ///        capture - passes the layout that content is in, and the graph's first-use barrier
    ///        transitions from there instead. The graph cannot see the caller's outside writes, so the
    ///        statement has to stay true for every execution; re-import when the layout changes. A
    ///        non-`UNDEFINED` layout on a non-image is rejected.
    /// @return `*this` for chaining.
    RenderGraphBuilder& ImportResource(const ResourceHandle& handle, const IGraphResource* resource,
                                       VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED);

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
    ///
    /// **A transient is per in-flight slot.** Each execution owns a complete copy of the graph's
    /// transients, so the answer is the copy belonging to the most recent execution's slot; before the
    /// first execution it is slot 0's. A caller reading back after `WaitIdle()` therefore sees what the
    /// last execution used, while a node callback during `Execute()` sees the execution's own copy. A
    /// caller that needs a *specific* past execution's content must copy it into an imported resource,
    /// or keep at most one execution in flight.
    /// @param handle Resource handle.
    /// @return The resource, or nullptr when the handle names nothing the graph can see.
    const IGraphResource* ResolveResource(const ResourceHandle& handle) const;

    /// @return The last build's transient memory plan: the blocks, their slots, and the two byte totals
    ///         for **one in-flight slot**. The builder allocates one copy of this plan per slot, so the
    ///         graph's total transient memory is `reservedBytes * inFlightCount`; the plan is shared
    ///         because every slot binds the same resources at the same offsets. `reservedBytes <
    ///         individualBytes` is what sharing memory looks like from outside, and `reservedBytes ==
    ///         individualBytes` is a legitimate answer for a graph whose transients all overlap in time.
    ///         Empty before the first build.
    const TransientMemoryPlan& GetTransientMemoryPlan() const;

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
    void SetTraceSink(std::function<void(RenderGraphTrace)> sink);

    /// Record and submit without waiting for the GPU. With `inFlightCount > 1` a second call may record
    /// and submit while the first is still in flight; its transients come from its own arena, and its
    /// imported resources from its own descriptor ring. The builder is single-threaded: one `Execute()`
    /// must return before the next begins, and `Build()` must not run concurrently with one.
    ///
    /// Every GPU node's use of an imported or transient resource acquires one slot per execution from
    /// the injected heap's ring, so the heap's `Config::ringSlots` must cover the graph's per-execution
    /// use count; `DescriptorHeapService` throws when a region's ring is exhausted.
    /// @param info External semaphores to wait on before, and signal after, this execution.
    void Execute(const RenderGraphExecuteInfo& info);

    /// @return The node names in dependency (topological) order.
    std::vector<std::string> GetExecutionOrder() const;

    /// @return The plan the last `Build()` produced: queue segments and cross-queue transfers. Derived,
    ///         read-only state, empty before the first `Build()`. It is what tells a caller whether an
    ///         `async` node actually got a cross-queue handover or was serialised on one family.
    const RenderGraphExecutionPlan& GetExecutionPlan() const;

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
    /// Complete definition of the builder's state, in RenderGraphBuilder.cpp.
    ///
    /// Out of line so this header names no implementation type (#297): a consumer compiles the public
    /// surface against an installed prefix with no internal graph headers. The builder's copy and move
    /// operations are deleted, which is the contract it already had when its members were declared
    /// here (a `unique_ptr` member plus a user-declared destructor), now stated rather than implied.
    struct Impl;

    /// Owned implementation. Non-null for the whole lifetime of a constructed builder.
    std::unique_ptr<Impl> m_impl;

public:
    /// The builder owns GPU resources, so copying it is not allowed.
    RenderGraphBuilder(const RenderGraphBuilder&) = delete;
    /// The builder owns GPU resources, so copy-assigning it is not allowed.
    RenderGraphBuilder& operator=(const RenderGraphBuilder&) = delete;
    /// The builder owns GPU resources, so moving it is not allowed.
    RenderGraphBuilder(RenderGraphBuilder&&) = delete;
    /// The builder owns GPU resources, so move-assigning it is not allowed.
    RenderGraphBuilder& operator=(RenderGraphBuilder&&) = delete;
};
}  // namespace Muyo::RenderGraph
