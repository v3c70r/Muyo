#include "RenderGraphBuilder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Debug.h"
#include "DependencyGraph.h"
#include "PipelineCache.h"
#include "PipelineStateBuilder.h"
#include "RenderGraph/ResourceUseResolver.h"
#include "RenderGraphBarrier.h"
#include "RenderGraphExecutor.h"
#include "RenderGraphShader.h"
#include "ShaderAssetManager.h"
#include "ShaderCache.h"
#include "ShaderReflectionFetcher.h"
#include "TransientMemoryPlanner.h"
#include "TransientResources.h"
#include "VkExtFuncsLoader.h"
#include "VkGraphResourceAllocator.h"
#include "vulkan/vulkan_core.h"

namespace
{
/// One spelling for the node-local heap-index key: `handle#usage`. Kept in one place because a
/// divergent edit in either writer/reader makes GetHeapIndex silently return nullopt.
std::string HeapIndexKey(const Muyo::RenderGraph::ResourceHandle& handle, Muyo::RenderGraph::ResourceUsage usage)
{
    return handle + "#" + std::to_string(static_cast<uint32_t>(usage));
}
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

uint32_t AlignUp(uint32_t nSize, uint32_t nAlignment) { return (nSize + nAlignment - 1) / nAlignment * nAlignment; }

/// RAII Vulkan command-buffer label for one node.
///
/// The graph emits the Vulkan label itself and no longer composes the engine's capture-tool marker: that
/// marker's backend is host policy, and reaching it made `muyo_rg` depend on the engine for a label
/// (#272). A host that wants capture-tool scopes wraps its own callback or records its own ranges.
class CommandBufferLabel
{
public:
    /// @param device        Device that owns @p commandBuffer; the label is resolved from it.
    /// @param commandBuffer Command buffer to label.
    /// @param name          Label text.
    /// @param pMarker       Host capture marker, or null for none. Its scope nests outside the Vulkan label,
    ///                      matching the order the engine's `ScopedMarker` used.
    CommandBufferLabel(VkDevice device, VkCommandBuffer commandBuffer, const std::string& name,
                       Muyo::RenderGraph::IGraphCaptureMarker* pMarker)
        : m_device(device), m_commandBuffer(commandBuffer), m_marker(pMarker)
    {
        if (m_marker != nullptr) m_marker->Begin(name);
        Muyo::beginMarker(m_device, commandBuffer, std::string(name), 0);
    }
    /// End the label and then the capture marker.
    ~CommandBufferLabel()
    {
        Muyo::endMarker(m_device, m_commandBuffer);
        if (m_marker != nullptr) m_marker->End();
    }

    /// Owns a label scope, so it is not copyable.
    CommandBufferLabel(const CommandBufferLabel&) = delete;
    /// See the copy constructor.
    CommandBufferLabel& operator=(const CommandBufferLabel&) = delete;

private:
    VkDevice m_device = VK_NULL_HANDLE;                          ///< Device that owns the command buffer.
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;            ///< Labelled command buffer.
    Muyo::RenderGraph::IGraphCaptureMarker* m_marker = nullptr;  ///< Host capture marker, or null.
};
}  // namespace

namespace Muyo::RenderGraph
{
namespace
{
/// Reject a declaration that cannot describe a valid allocation (#127).
///
/// A desc is input from the author: a zero size, an undefined format or a zero usage flag set cannot be
/// turned into a valid resource, and letting it through moves the failure to the allocator or to a driver
/// VUID that names a handle. Rejected here, at the declaration line.
/// @param handle Resource being declared, so the message names it.
/// @param desc   Its declaration.
void ValidateResourceDesc(const std::string& handle, const ResourceDesc& desc)
{
    std::visit(
        [&](const auto& d)
        {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, BufferResourceDesc>)
            {
                if (d.count == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares a buffer with count 0.");
                }
                if (d.stride == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares a buffer with stride 0.");
                }
            }
            else
            {
                if (d.extent.width == 0 || d.extent.height == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares an image with a zero extent (" +
                                             std::to_string(d.extent.width) + "x" + std::to_string(d.extent.height) +
                                             ").");
                }
                if (d.format == VK_FORMAT_UNDEFINED)
                {
                    throw std::runtime_error("AddResource('" + handle +
                                             "') declares an image with VK_FORMAT_UNDEFINED.");
                }
                if (d.mips == 0 || d.layers == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares an image with " +
                                             std::to_string(d.mips) + " mips and " + std::to_string(d.layers) +
                                             " layers; both must be at least 1.");
                }
            }
            if (d.usage == 0)
            {
                throw std::runtime_error("AddResource('" + handle +
                                         "') declares usage 0, so no operation would be valid for it.");
            }
        },
        desc);
}
}  // namespace

namespace
{
/// Reject a service the graph cannot run without, **before** anything dereferences it. `m_services` is
/// declared ahead of the members whose construction dereferences it, and a member initialiser list runs
/// in declaration order, so a missing entry becomes a named error here rather than a null dereference
/// inside another member's constructor. A throw and not an assert, because a caller can falsify this and
/// the error path has to survive `NDEBUG` (AGENTS.md section 3).
const RenderGraphServices& RequireServices(const RenderGraphServices& services)
{
    if (services.device == nullptr)
    {
        throw std::runtime_error(
            "RenderGraphBuilder: services.device is null. The graph has no device to create pipelines, "
            "command buffers or synchronisation with.");
    }
    if (services.resourceAllocator == nullptr)
    {
        throw std::runtime_error(
            "RenderGraphBuilder: services.resourceAllocator is null. The graph allocates its declared "
            "resources through it.");
    }
    return services;
}
}  // namespace

/// Everything `RenderGraphBuilder` used to declare privately, out of line so its header is the public
/// surface only (#297).
///
/// The nested planning types and the data members are in the order the class had them, because both
/// are load-bearing: `m_services` is declared before the members whose construction dereferences it,
/// and `~Impl` tears the GPU objects down in an order the owning members rely on before they are
/// destroyed.
struct RenderGraphBuilder::Impl
{
    // ── Planning types ──────────────────────────────────────────────────────
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

    /// One in-flight execution's host-visible, device-addressable root arena.
    ///
    /// Owned per executor slot rather than per builder: two executions can be recording at once, and a
    /// shared arena would let the later execution overwrite root data the earlier one's GPU work has
    /// not read yet. A slot's cursor is reset only after AcquireSlot has waited for that slot.
    struct RootArena
    {
        VkBuffer buffer = VK_NULL_HANDLE;  ///< Host-visible root buffer.
        GraphMemoryBlock allocation;       ///< The block owning its memory.
        std::byte* mapped = nullptr;       ///< Persistent mapping.
        VkDeviceAddress address = 0;       ///< Device address of offset 0.
        VkDeviceSize cursor = 0;           ///< Next free byte in this execution.
    };

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

    /// One aliased transient's device-independent planning identity.
    ///
    /// Everything the memory plan depends on that the declaration and the execution order alone decide:
    /// no `VkBuffer`/`VkImage` is created to gather it. That is what lets `Build()` decide whether the
    /// arenas it already has still match before it destroys anything.
    struct TransientPlanInput
    {
        ResourceHandle handle;                ///< Declaration handle.
        const ResourceDesc* pDesc = nullptr;  ///< Its declaration; points into the registry.
        TransientLiveRange liveRange;         ///< First/last execution-order use.
        uint64_t sharingDomain = 0;           ///< Queue family, one-based; 0 means no GPU use.
        bool canShareSlot = true;             ///< False when its uses span queue families.
    };

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

    /// One in-flight slot's complete copy of the graph's transients.
    ///
    /// The plan is shared; the storage is not. Each slot owns its own unbound-then-bound buffers and
    /// images, its own views, and its own aliasing blocks, so two executions that write the same handle
    /// write different memory. The maps are keyed by handle so `ResolveResource` can find the current
    /// slot's instance, and the resources are destroyed before the blocks are freed because a block
    /// cannot be released while something is still bound to it.
    struct TransientSlotStorage
    {
        std::unordered_map<ResourceHandle, std::unique_ptr<TransientBufferResource>> buffers;  ///< Transient buffers.
        std::unordered_map<ResourceHandle, std::unique_ptr<TransientImageResource>> images;    ///< Transient images.
        std::vector<GraphMemoryBlock> blocks;  ///< This slot's aliasing blocks, freed after its resources.
        TransientSlotStorage() = default;
        /// The maps own their resources, so the struct is move-only. Saying so explicitly is load-bearing on
        /// MSVC: `std::unordered_map` declares its copy constructor unconditionally and without `noexcept` on
        /// its move constructor, so `is_copy_constructible_v` reports *true* for any struct that merely
        /// contains one. `std::vector::_Reallocate` then picks its copy branch over the move branch and
        /// instantiates the deleted copy of the mapped `unique_ptr`, failing to compile `resize()` below.
        /// Deleting the copy operations makes the trait tell the truth and the vector take the move branch.
        TransientSlotStorage(const TransientSlotStorage&) = delete;
        /// Copy assignment is deleted for the same reason; use move assignment instead.
        /// @return Nothing; this overload is deleted.
        TransientSlotStorage& operator=(const TransientSlotStorage&) = delete;
        /// @param other Storage to take over.
        TransientSlotStorage(TransientSlotStorage&& other) = default;
        /// @param other Storage to take over.
        /// @return This storage, holding `other`'s resources and blocks.
        TransientSlotStorage& operator=(TransientSlotStorage&& other) = default;
    };

    // ── Construction and teardown ───────────────────────────────────────────
    /// @param services     Device, allocator and descriptor heap the graph uses.
    /// @param inFlightCount Number of executions that may be in flight.
    Impl(const RenderGraphServices& services, uint32_t inFlightCount);
    /// @param renderDevice  Device whose VMA-backed allocator the graph creates.
    /// @param inFlightCount Number of executions that may be in flight.
    Impl(VkRenderDevice* renderDevice, uint32_t inFlightCount);
    ~Impl();

    /// Finish construction: create the owned descriptor heap when the device supports one, validate the
    /// injected caches and read the device limits. Shared by both constructors.
    void Initialise();

    // ── Public API implementations, forwarded by RenderGraphBuilder ─────────
    void WaitIdle();
    void ReleaseCachedDeviceObjects();
    /// @param handle Resource handle.
    /// @param desc   Allocation description.
    void AddResource(const ResourceHandle& handle, ResourceDesc desc);
    /// @param handle        Resource handle.
    /// @param resource      Imported resource.
    /// @param currentLayout Its layout before an execution begins.
    void ImportResource(const ResourceHandle& handle, const IGraphResource* resource, VkImageLayout currentLayout);
    /// @param handle Resource handle.
    /// @return The allocation description, if any.
    std::optional<ResourceDesc> GetResourceDesc(const ResourceHandle& handle) const;
    /// @param handle Resource handle.
    /// @return The resolved resource, or nullptr.
    const IGraphResource* ResolveResource(const ResourceHandle& handle) const;
    /// @param nodeCreateInfo Node declaration.
    void AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo);
    /// @param fromNode Node that runs first.
    /// @param toNode   Node that runs after it.
    void AddDependency(const std::string& fromNode, const std::string& toNode);
    void Build();
    void Execute();
    /// @param info External semaphores for this execution.
    void Execute(const RenderGraphExecuteInfo& info);
    /// @return The node names in dependency (topological) order.
    std::vector<std::string> GetExecutionOrder() const;
    /// @return A Mermaid `flowchart LR` document.
    std::string ToMermaid() const;
    /// @return A DOT document.
    std::string ToDot() const;

    // ── Build helpers ───────────────────────────────────────────────────────
    /// @param rgn Node to compile.
    /// @return The compiled node.
    CompiledRenderGraphNode CompileRenderGraphNode(const RenderGraphNode& rgn);
    /// Create the root arena for `slot` on first use; it is host-visible and device-addressable.
    void EnsureRootArena(uint32_t slot);
    /// @param size      Bytes wanted.
    /// @param alignment Alignment the bytes need.
    /// @return A slice of the current execution's root arena, or throw when it does not fit.
    RenderGraphRootSlice AllocateRoot(size_t size, size_t alignment);

    // Build a ray tracing pipeline + shader binding table for a RAY_TRACING node.
    /// @param rgn           Compiled node to fill in.
    /// @param logicalNode   Node declaration it was compiled from.
    /// @param shaderModules Shader modules for the RT stages.
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

    // The offset/range to write into VkDescriptorBufferInfo for a buffer binding, validated against
    // the buffer and this device's alignment limit. Whole buffer when the use declares no range.
    /// @param nodeName       Node name used in the error.
    /// @param use            Buffer use to resolve.
    /// @param buffer         Buffer being bound.
    /// @param descriptorType Descriptor type, which decides the alignment.
    /// @return The offset and range to write.
    std::pair<VkDeviceSize, VkDeviceSize> ResolveBufferRange(const ResourceHandle& nodeName,
                                                             const ResolvedResourceUse& use, const IGraphBuffer& buffer,
                                                             VkDescriptorType descriptorType) const;

    // Queue routing. `GetQueueKey` (which node runs where) lives with the execution plan in
    // RenderGraphExecutionPlan.h; these resolve a queue key against this device. Submission itself
    // - queues, handover semaphores, waiting - belongs to RenderGraphExecutor.
    /// @param type Queue type.
    /// @return The queue family index for it.
    uint32_t GetQueueFamilyForType(QueueType type) const;
    /// @return The device's queue families.
    RenderGraphQueueFamilies GetQueueFamilies() const;

    // Auto wraps a graphics node's work in vkCmdBeginRendering/vkCmdEndRendering.
    /// @param cmdBuf           Command buffer being recorded.
    /// @param rgn              Compiled node.
    /// @param ctx              Node context handed to the callback.
    /// @param alreadyWritten   Resources already written by this node.
    /// @param colorAttachments Colour attachments gathered so far.
    /// @param clearValues      Clear values gathered so far.
    /// @return True when a render pass was begun.
    bool BeginRendering(VkCommandBuffer cmdBuf, const CompiledRenderGraphNode& rgn, RenderGraphNodeContext& ctx,
                        std::unordered_set<ResourceHandle>& alreadyWritten,
                        std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                        std::vector<VkClearValue>& clearValues);

    // Derive every graph-owned transient's live range and reject a read that no write precedes.
    //
    // A transient promises content from its first write to its last read *within one execution*, so a
    // read with nothing writing it first is a declaration error. Without the check it silently reads
    // whatever the memory holds: the first reader's barrier moves an image out of
    // VK_IMAGE_LAYOUT_UNDEFINED, and a transition from UNDEFINED discards contents rather than keeping
    // them. `Persistent` and `Imported` are exempt because their content is not this execution's to
    // produce - which is what declaring either of them means.
    /// @param executionOrder Node order to derive the ranges from.
    void ValidateTransientLifetimes(const std::vector<std::string>& executionOrder);

    /// @param executionOrder Node order used to derive live ranges and queue-family domains.
    /// @return Every used aliased transient's planning identity, sorted by handle so the key and the
    ///         plan are deterministic.
    std::vector<TransientPlanInput> GatherTransientPlanInputs(const std::vector<std::string>& executionOrder) const;
    /// @param inputs Output of `GatherTransientPlanInputs`.
    /// @return A byte-string identity for the arena layout those inputs describe. Equal keys mean an
    ///         unchanged plan, so a repeated `Build()` may keep the existing arenas.
    std::string MakeTransientPlanKey(const std::vector<TransientPlanInput>& inputs) const;

    /// Create one slot's unbound resources from `inputs`, and optionally the planner requests (which
    /// need the device-queried requirements).
    /// @param inputs    Planning identity of every used aliased transient.
    /// @param pRequests When non-null, filled with one request per resource for `PlanTransientMemory`.
    /// @return The slot's resources, unbound; the caller allocates blocks and binds them.
    TransientSlotStorage CreateTransientSlotStorage(const std::vector<TransientPlanInput>& inputs,
                                                    std::vector<TransientMemoryRequest>* pRequests);
    /// Allocate `m_transientMemoryPlan`'s blocks for one slot and bind that slot's resources into them.
    /// @param storage Slot whose resources are bound; the new blocks are appended to it.
    void AllocateAndBindTransientBlocks(TransientSlotStorage& storage);

    /// Create the per-slot transient arenas for `planKey`, or keep the existing ones when it matches.
    /// The caller guarantees no execution is in flight when the key differs.
    /// @param inputs  Planning identity of every used aliased transient.
    /// @param planKey This build's identity; compared with `m_transientPlanKey`.
    void BuildAliasedTransients(const std::vector<TransientPlanInput>& inputs, const std::string& planKey);
    /// Destroy every slot's transient resources and then free its blocks. The caller must have drained
    /// every execution: a block freed under in-flight work is a use-after-free. Safe to call repeatedly.
    void DestroyAliasedTransients();

    /// Resolve a handle against one execution slot's transients, or the manager when it has none.
    /// @param slot   In-flight slot whose instance is wanted.
    /// @param handle Resource handle.
    /// @return The slot's instance, another owner's resource, or nullptr.
    const IGraphResource* ResolveResourceForSlot(uint32_t slot, const ResourceHandle& handle) const;
    /// @param handle Resource handle.
    /// @return True when the handle is bound per execution (an import, or a graph-owned transient) and
    ///         therefore cannot use a build-time static descriptor.
    bool IsPerExecutionResource(const ResourceHandle& handle) const;

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

    /// Load a shader through the injected `ShaderCache` when there is one, otherwise through the
    /// per-builder manager. AddNode and CompileRenderGraphNode both go through these two helpers so the
    /// load and the resolve cannot disagree about which cache the key belongs to.
    /// @param shaderName Shader name without the `.spv` suffix.
    /// @return The loaded shader's identity, or `std::nullopt` when it could not be loaded.
    std::optional<ShaderAssetKey> LoadShaderAsset(const std::string& shaderName);
    /// @param key Key returned by the matching `LoadShaderAsset` call.
    /// @return The module, words and reflection, or nullptr when the key is unknown.
    const ShaderAsset* ResolveShaderAsset(const ShaderAssetKey& key) const;

    /// Release every cached static slot; only after the GPU is known idle.
    void ReleaseCachedDescriptors();
    /// Destroy every pipeline in the per-builder fallback cache; only after the GPU is known idle.
    /// Entries in an injected shared cache are never touched by this.
    void DestroyCachedPipelines();

    // ── State ───────────────────────────────────────────────────────────────
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
    /// Allocator owned by the convenience constructor. Declared before `m_services` because that
    /// constructor initialises it first and points the services at it.
    std::unique_ptr<IGraphResourceAllocator> m_ownedResourceAllocator;
    /// The injected services, declared **before** the members whose construction dereferences them: a
    /// member initialiser list runs in declaration order, so putting this first is what lets
    /// `RequireServices` reject a missing one by name before anything dereferences it.
    RenderGraphServices m_services;
    /// Owned fallback for callers that did not inject a service; B1b-5b lets tests/builder construction
    /// opt into heap nodes without threading a service, and the descriptor lifetime follows the graph.
    std::unique_ptr<DescriptorHeapService> m_ownedDescriptorHeap;
    ShaderAssetManager m_shaderAssetManager;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkDeviceSize m_minUniformBufferOffsetAlignment = 1;
    VkDeviceSize m_minStorageBufferOffsetAlignment = 1;
    // Owns submission, cross-queue synchronization and waiting. The graph only records.
    RenderGraphExecutor m_executor;
    uint64_t m_nextExecution = 0;                       ///< Execution counter a trace is labelled with.
    std::function<void(RenderGraphTrace)> m_traceSink;  ///< Set by SetTraceSink; empty means no trace is recorded.
    /// One entry per in-flight slot, rebuilt only when `m_transientPlanKey` changes.
    std::vector<TransientSlotStorage> m_transientSlots;
    /// Slot whose instance `ResolveResource` and the recording paths read: the execution being recorded.
    uint32_t m_currentTransientSlot = 0;
    /// Identity of the layout `m_transientSlots` currently holds. Empty before the first successful
    /// build; a different key means the old arenas must be drained and rebuilt.
    std::string m_transientPlanKey;
    /// Host-visible root arenas, one per in-flight slot (B1b-4 / B2b). The host owns each layout.
    static constexpr VkDeviceSize kRootCapacity = 64ULL * 1024;
    std::vector<RootArena> m_rootArenas;
    uint32_t m_currentRootSlot = 0;

    /// Static slots reused across builds, keyed by descriptor content.
    std::unordered_map<DescriptorKey, DescriptorSlot, DescriptorKeyHash> m_descriptorCache;
    /// Pipelines reused across builds when no shared `PipelineCache` is injected, keyed on every input
    /// that changes the created pipeline but not on any descriptor or layout state (B2c/#17). Owned here;
    /// compiled nodes do not destroy them. With a shared cache injected this map stays empty and the
    /// cache's entries are borrowed instead.
    std::unordered_map<std::string, VkPipeline> m_pipelineCache;
    /// The last plan's numbers, so a caller can ask what aliasing bought without reaching into the build.
    /// One slot's plan: `m_transientSlots` holds `inFlightCount` copies bound at these offsets.
    TransientMemoryPlan m_transientMemoryPlan;

    ResourceDescRegistry m_resourceDescRegistry;
    /// Graph-owned persistent resources, allocated at Build() through the injected allocator. The graph
    /// owns the object; the allocator owns the memory and the concrete Vulkan class.
    std::unordered_map<ResourceHandle, std::unique_ptr<IGraphResource>> m_ownedResources;
    /// Per-node shader binding tables, allocated at Build() through the injected allocator.
    std::unordered_map<std::string, std::unique_ptr<IGraphBuffer>> m_shaderBindingTables;
    std::unordered_map<ResourceHandle, const IGraphResource*> m_importedResources;
    /// The layout each imported image is in before every execution, when the caller stated one. Seeded
    /// into the per-execution access state so the first-use barrier preserves the imported content
    /// instead of discarding it through `VK_IMAGE_LAYOUT_UNDEFINED`. Absent for `UNDEFINED`.
    std::unordered_map<ResourceHandle, VkImageLayout> m_importedImageLayouts;
};

RenderGraphBuilder::Impl::Impl(const RenderGraphServices& services, uint32_t inFlightCount)
    : m_services(RequireServices(services))
    , m_shaderAssetManager(m_services.device->GetDevice())
    , m_vkDevice(m_services.device->GetDevice())
    , m_executor(m_services.device, inFlightCount)
{
    Initialise();
}

RenderGraphBuilder::Impl::Impl(VkRenderDevice* renderDevice, uint32_t inFlightCount)
    : m_ownedResourceAllocator(CreateVkGraphResourceAllocator(renderDevice))
    , m_services(RequireServices(
          RenderGraphServices{.device = renderDevice, .resourceAllocator = m_ownedResourceAllocator.get()}))
    , m_shaderAssetManager(m_services.device->GetDevice())
    , m_vkDevice(m_services.device->GetDevice())
    , m_executor(m_services.device, inFlightCount)
{
    Initialise();
}

void RenderGraphBuilder::Impl::Initialise()
{
    if (m_services.descriptorHeap == nullptr && m_services.device->IsDescriptorHeapEnabled())
    {
        m_ownedDescriptorHeap = std::make_unique<DescriptorHeapService>();
        m_ownedDescriptorHeap->Initialize(m_services.device);
        m_services.descriptorHeap = m_ownedDescriptorHeap.get();
    }
    if (m_services.pipelineCache != nullptr && m_services.pipelineCache->GetDevice() != m_vkDevice)
    {
        // A cache from another device holds handles this driver cannot use, and borrowing one would
        // surface as an invalid-handle VUID at bind time instead of at the call that mixed them.
        throw std::runtime_error(
            "RenderGraphBuilder: services.pipelineCache was created on a different VkDevice than "
            "services.device. A pipeline cache is device-scoped; create one per device.");
    }
    if (m_services.shaderCache != nullptr && m_services.shaderCache->GetDevice() != m_vkDevice)
    {
        throw std::runtime_error(
            "RenderGraphBuilder: services.shaderCache was created on a different VkDevice than "
            "services.device. A shader module belongs to the device that created it; create one cache per device.");
    }
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(m_services.device->GetPhysicalDevice(), &deviceProperties);
    m_minUniformBufferOffsetAlignment = deviceProperties.limits.minUniformBufferOffsetAlignment;
    m_minStorageBufferOffsetAlignment = deviceProperties.limits.minStorageBufferOffsetAlignment;
}

RenderGraphBuilder::Impl::~Impl()
{
    // Before destroying anything the GPU may still be using. `Execute(RenderGraphExecuteInfo)` submits
    // without waiting, so a builder destroyed right after a non-blocking Execute would free its pipelines
    // while its command buffers are still pending
    // (`VUID-vkDestroyPipeline-pipeline-00765`). The executor's own destructor waits, but it runs after
    // this body, which is too late.
    m_executor.WaitIdle();
    m_compiledGraph.Destroy(m_vkDevice);
    DestroyCachedPipelines();
    ReleaseCachedDescriptors();
    // After the compiled graph, because its descriptors and pipelines name these buffers, and before the
    // device goes away, because the blocks hold memory this class owns.
    DestroyAliasedTransients();
    for (RootArena& arena : m_rootArenas)
    {
        if (arena.mapped != nullptr) m_services.resourceAllocator->UnmapMemory(arena.allocation);
        if (arena.buffer != VK_NULL_HANDLE || !arena.allocation.IsNull())
        {
            m_services.resourceAllocator->FreeBuffer(arena.buffer, arena.allocation);
        }
    }
    m_rootArenas.clear();
    m_vkDevice = VK_NULL_HANDLE;
}

RenderGraphBuilder::RenderGraphBuilder(const RenderGraphServices& services, uint32_t inFlightCount)
    : m_impl(std::make_unique<Impl>(services, inFlightCount))
{
}

RenderGraphBuilder::RenderGraphBuilder(VkRenderDevice* renderDevice, uint32_t inFlightCount)
    : m_impl(std::make_unique<Impl>(renderDevice, inFlightCount))
{
}

RenderGraphBuilder::~RenderGraphBuilder() = default;

const CompiledRenderGraph& RenderGraphBuilder::GetCompiledGraph() const { return m_impl->m_compiledGraph; }

void RenderGraphBuilder::WaitIdle() { m_impl->WaitIdle(); }

void RenderGraphBuilder::ReleaseCachedDeviceObjects() { m_impl->ReleaseCachedDeviceObjects(); }

RenderGraphBuilder& RenderGraphBuilder::AddResource(const ResourceHandle& handle, ResourceDesc desc)
{
    m_impl->AddResource(handle, std::move(desc));
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::ImportResource(const ResourceHandle& handle, const IGraphResource* resource,
                                                       VkImageLayout currentLayout)
{
    m_impl->ImportResource(handle, resource, currentLayout);
    return *this;
}

std::optional<ResourceDesc> RenderGraphBuilder::GetResourceDesc(const ResourceHandle& handle) const
{
    return m_impl->GetResourceDesc(handle);
}

const IGraphResource* RenderGraphBuilder::ResolveResource(const ResourceHandle& handle) const
{
    return m_impl->ResolveResource(handle);
}

const TransientMemoryPlan& RenderGraphBuilder::GetTransientMemoryPlan() const { return m_impl->m_transientMemoryPlan; }

void RenderGraphBuilder::AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo) { m_impl->AddNode(nodeCreateInfo); }

void RenderGraphBuilder::AddDependency(const std::string& fromNode, const std::string& toNode)
{
    m_impl->AddDependency(fromNode, toNode);
}

void RenderGraphBuilder::Build() { m_impl->Build(); }

void RenderGraphBuilder::Execute() { m_impl->Execute(); }

void RenderGraphBuilder::SetTraceSink(std::function<void(RenderGraphTrace)> sink)
{
    m_impl->m_traceSink = std::move(sink);
}

void RenderGraphBuilder::Execute(const RenderGraphExecuteInfo& info) { m_impl->Execute(info); }

std::vector<std::string> RenderGraphBuilder::GetExecutionOrder() const { return m_impl->GetExecutionOrder(); }

const RenderGraphExecutionPlan& RenderGraphBuilder::GetExecutionPlan() const
{
    return m_impl->m_compiledGraph.GetExecutionPlan();
}

std::string RenderGraphBuilder::ToMermaid() const { return m_impl->ToMermaid(); }

std::string RenderGraphBuilder::ToDot() const { return m_impl->ToDot(); }

std::pair<VkDeviceSize, VkDeviceSize> RenderGraphBuilder::Impl::ResolveBufferRange(
    const ResourceHandle& nodeName, const ResolvedResourceUse& use, const IGraphBuffer& buffer,
    VkDescriptorType descriptorType) const
{
    if (!use.bufferRange.has_value())
    {
        return {0, buffer.GetSize()};
    }

    const bool bUniform = descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                          descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    const VkDeviceSize nAlignment = bUniform ? m_minUniformBufferOffsetAlignment : m_minStorageBufferOffsetAlignment;
    ValidateBufferRange(nodeName, use.handle, *use.bufferRange, buffer.GetSize(), nAlignment);
    return {static_cast<VkDeviceSize>(use.bufferRange->offset), static_cast<VkDeviceSize>(use.bufferRange->size)};
}

namespace
{
/// @param seed  Running hash.
/// @param value Field to mix in.
/// @return The combined hash (the boost::hash_combine mix).
std::size_t CombineDescriptorHash(std::size_t seed, std::size_t value)
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

/// Append a scalar field to a pipeline-cache key. The key is a byte string rather than a hash so two
/// different declarations cannot collide into the wrong pipeline.
template <typename T>
void AppendPipelineKey(std::string& key, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    key.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

/// @return The pipeline-cache key for one node. It contains every input that changes the created
///         pipeline — shader hashes, PSODesc, attachment formats and specialization values — and no
///         descriptor or layout state, which the heap path keeps out of the pipeline (#17).
std::string MakePipelineKey(QueueType queueType, const std::vector<std::uint64_t>& shaderHashes, const PSODesc& pso,
                            const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
                            const std::vector<SpecializationValue>& specializationConstants)
{
    std::string key;
    AppendPipelineKey(key, static_cast<uint32_t>(queueType));
    AppendPipelineKey(key, static_cast<uint32_t>(shaderHashes.size()));
    for (const std::uint64_t hash : shaderHashes) AppendPipelineKey(key, hash);

    AppendPipelineKey(key, static_cast<uint32_t>(pso.vertexType));
    AppendPipelineKey(key, static_cast<uint32_t>(pso.inputAssembly.topology));
    AppendPipelineKey(key, static_cast<uint8_t>(pso.inputAssembly.primitiveRestart));
    AppendPipelineKey(key, static_cast<uint32_t>(pso.rasterState.fillMode));
    AppendPipelineKey(key, static_cast<uint32_t>(pso.rasterState.cullMode));
    AppendPipelineKey(key, static_cast<uint32_t>(pso.rasterState.frontFace));
    AppendPipelineKey(key, static_cast<uint8_t>(pso.rasterState.depthClamp));
    AppendPipelineKey(key, static_cast<uint8_t>(pso.rasterState.depthBias));
    AppendPipelineKey(key, pso.rasterState.depthBiasConstant);
    AppendPipelineKey(key, pso.rasterState.depthBiasSlope);
    AppendPipelineKey(key, static_cast<uint8_t>(pso.depthStencilState.depthTestEnable));
    AppendPipelineKey(key, static_cast<uint8_t>(pso.depthStencilState.depthWriteEnable));
    AppendPipelineKey(key, static_cast<uint32_t>(pso.depthStencilState.depthCompare));
    AppendPipelineKey(key, static_cast<uint8_t>(pso.depthStencilState.stencilEnable));
    if (pso.depthStencilState.stencilEnable)
    {
        for (const StencilOpState* pStencil : {&pso.depthStencilState.front, &pso.depthStencilState.back})
        {
            AppendPipelineKey(key, pStencil->failOp);
            AppendPipelineKey(key, pStencil->passOp);
            AppendPipelineKey(key, pStencil->depthFailOp);
            AppendPipelineKey(key, static_cast<uint32_t>(pStencil->compareOp));
        }
    }
    AppendPipelineKey(key, pso.blendState.attachmentCount);
    for (uint32_t i = 0; i < pso.blendState.attachmentCount && i < MAX_COLOR_ATTACHMENTS; ++i)
    {
        const ColorBlendAttachment& attachment = pso.blendState.attachments[i];
        AppendPipelineKey(key, static_cast<uint8_t>(attachment.blendEnable));
        AppendPipelineKey(key, static_cast<uint32_t>(attachment.srcColor));
        AppendPipelineKey(key, static_cast<uint32_t>(attachment.dstColor));
        AppendPipelineKey(key, static_cast<uint32_t>(attachment.colorOp));
        AppendPipelineKey(key, static_cast<uint32_t>(attachment.srcAlpha));
        AppendPipelineKey(key, static_cast<uint32_t>(attachment.dstAlpha));
        AppendPipelineKey(key, static_cast<uint32_t>(attachment.alphaOp));
        AppendPipelineKey(key, attachment.colorWriteMask);
    }
    AppendPipelineKey(key, pso.multisampleState.sampleCount);
    AppendPipelineKey(key, static_cast<uint8_t>(pso.multisampleState.sampleShadingEnable));
    AppendPipelineKey(key, pso.multisampleState.minSampleShading);

    AppendPipelineKey(key, static_cast<uint32_t>(colorFormats.size()));
    for (const VkFormat format : colorFormats) AppendPipelineKey(key, static_cast<uint32_t>(format));
    AppendPipelineKey(key, static_cast<uint32_t>(depthFormat));

    AppendPipelineKey(key, static_cast<uint32_t>(specializationConstants.size()));
    for (const SpecializationValue& constant : specializationConstants)
    {
        AppendPipelineKey(key, constant.id);
        AppendPipelineKey(key, constant.value);
    }
    return key;
}
}  // namespace

bool RenderGraphBuilder::Impl::DescriptorKey::operator==(const DescriptorKey& other) const
{
    if (type != other.type || layout != other.layout || address != other.address || size != other.size)
    {
        return false;
    }
    const bool bImage = type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    if (!bImage) return true;
    return view.flags == other.view.flags && view.image == other.view.image && view.viewType == other.view.viewType &&
           view.format == other.view.format && view.components.r == other.view.components.r &&
           view.components.g == other.view.components.g && view.components.b == other.view.components.b &&
           view.components.a == other.view.components.a &&
           view.subresourceRange.aspectMask == other.view.subresourceRange.aspectMask &&
           view.subresourceRange.baseMipLevel == other.view.subresourceRange.baseMipLevel &&
           view.subresourceRange.levelCount == other.view.subresourceRange.levelCount &&
           view.subresourceRange.baseArrayLayer == other.view.subresourceRange.baseArrayLayer &&
           view.subresourceRange.layerCount == other.view.subresourceRange.layerCount;
}

std::size_t RenderGraphBuilder::Impl::DescriptorKeyHash::operator()(const DescriptorKey& key) const
{
    std::size_t seed = std::hash<uint32_t>{}(static_cast<uint32_t>(key.type));
    seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(key.layout)));
    seed = CombineDescriptorHash(seed, std::hash<uint64_t>{}(key.address));
    seed = CombineDescriptorHash(seed, std::hash<uint64_t>{}(key.size));
    const bool bImage = key.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || key.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    if (bImage)
    {
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.flags));
        seed = CombineDescriptorHash(seed, std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(key.view.image)));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(key.view.viewType)));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(key.view.format)));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.subresourceRange.aspectMask));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.subresourceRange.baseMipLevel));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.subresourceRange.levelCount));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.subresourceRange.baseArrayLayer));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.subresourceRange.layerCount));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.components.r));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.components.g));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.components.b));
        seed = CombineDescriptorHash(seed, std::hash<uint32_t>{}(key.view.components.a));
    }
    return seed;
}

void RenderGraphBuilder::Impl::WaitIdle() { m_executor.WaitIdle(); }

void RenderGraphBuilder::Impl::ReleaseCachedDeviceObjects()
{
    DestroyCachedPipelines();
    ReleaseCachedDescriptors();
}

void RenderGraphBuilder::Impl::DestroyCachedPipelines()
{
    // Only the fallback map. A shared cache's entries belong to a caller-owned object that may serve
    // another builder or graph, so this builder must never destroy them.
    for (const auto& entry : m_pipelineCache)
    {
        if (entry.second != VK_NULL_HANDLE) vkDestroyPipeline(m_vkDevice, entry.second, nullptr);
    }
    m_pipelineCache.clear();
}

void RenderGraphBuilder::Impl::ReleaseCachedDescriptors()
{
    if (m_services.descriptorHeap != nullptr)
    {
        for (const auto& entry : m_descriptorCache)
        {
            m_services.descriptorHeap->Release(entry.second);
        }
    }
    m_descriptorCache.clear();
}

void RenderGraphBuilder::Impl::EnsureRootArena(uint32_t slot)
{
    if (m_rootArenas.size() <= slot) m_rootArenas.resize(slot + 1);
    RootArena& arena = m_rootArenas[slot];
    if (arena.buffer != VK_NULL_HANDLE) return;
    m_services.resourceAllocator->AllocateBuffer(
        kRootCapacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU, arena.buffer, arena.allocation);
    arena.mapped = static_cast<std::byte*>(m_services.resourceAllocator->MapMemory(arena.allocation));
    arena.address = m_services.device->GetBufferDeviceAddress(arena.buffer);
}

RenderGraphRootSlice RenderGraphBuilder::Impl::AllocateRoot(size_t size, size_t alignment)
{
    if (size == 0 || alignment == 0) throw std::runtime_error("RenderGraphBuilder: invalid root allocation");
    EnsureRootArena(m_currentRootSlot);
    RootArena& arena = m_rootArenas[m_currentRootSlot];
    const VkDeviceSize aligned = (arena.cursor + alignment - 1) / alignment * alignment;
    if (aligned + size > kRootCapacity)
    {
        throw std::runtime_error("RenderGraphBuilder: root arena exhausted");
    }
    RenderGraphRootSlice slice{};
    slice.host = arena.mapped + aligned;
    slice.deviceAddress = arena.address + aligned;
    slice.size = size;
    arena.cursor = aligned + size;
    return slice;
}

void RenderGraphBuilder::Impl::AddResource(const ResourceHandle& handle, ResourceDesc desc)
{
    const ResourceLifetime lifetime = std::visit([](const auto& d) { return d.lifetime; }, desc);
    if (lifetime == ResourceLifetime::Imported)
    {
        // AddResource means "the graph allocates this", so a desc asking to be Imported contradicts the
        // call. Reported here rather than at Build because it is a mistake at the line that made it.
        throw std::runtime_error("AddResource('" + handle +
                                 "') declares lifetime Imported, but AddResource is the call that makes the "
                                 "graph allocate a resource. Use ImportResource for one the graph does not own.");
    }
    ValidateResourceDesc(handle, desc);
    m_resourceDescRegistry[handle] = std::move(desc);
}

void RenderGraphBuilder::Impl::ImportResource(const ResourceHandle& handle, const IGraphResource* resource,
                                              VkImageLayout currentLayout)
{
    if (currentLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
        (resource == nullptr || resource->GetResourceKind() != ResourceKind::IMAGE))
    {
        throw std::runtime_error("ImportResource('" + handle +
                                 "') states a current layout, but the imported "
                                 "resource is not an image. Layouts only describe images.");
    }
    m_importedResources[handle] = resource;
    // Re-importing resets the statement: the default clears a layout a previous import set, which is what
    // a caller re-pointing at a fresh resource means.
    if (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        m_importedImageLayouts.erase(handle);
    }
    else
    {
        m_importedImageLayouts[handle] = currentLayout;
    }
}

std::optional<ResourceDesc> RenderGraphBuilder::Impl::GetResourceDesc(const ResourceHandle& handle) const
{
    auto it = m_resourceDescRegistry.find(handle);
    return it != m_resourceDescRegistry.end() ? std::optional<ResourceDesc>{it->second} : std::nullopt;
}

const IGraphResource* RenderGraphBuilder::Impl::ResolveResource(const ResourceHandle& handle) const
{
    return ResolveResourceForSlot(m_currentTransientSlot, handle);
}

namespace
{
/// @param desc A resource declaration.
/// @return True when the declaration asks the graph to give it storage inside an aliasing block (a
///         `Transient`), instead of letting the resource manager allocate one of its own.
bool IsAliasedTransient(const ResourceDesc& desc);
}  // namespace

const IGraphResource* RenderGraphBuilder::Impl::ResolveResourceForSlot(uint32_t slot,
                                                                       const ResourceHandle& handle) const
{
    if (auto it = m_importedResources.find(handle); it != m_importedResources.end())
    {
        return it->second;
    }
    // Before the manager: an aliased buffer is not in the manager at all, and looking there first would
    // find a same-named resource from an earlier build if one ever existed. The slot is the execution's
    // own copy; a handle with no instance in this slot falls through to the manager like any other.
    if (slot < m_transientSlots.size())
    {
        const TransientSlotStorage& storage = m_transientSlots[slot];
        if (auto it = storage.buffers.find(handle); it != storage.buffers.end())
        {
            return it->second.get();
        }
        if (auto it = storage.images.find(handle); it != storage.images.end())
        {
            return it->second.get();
        }
    }
    if (auto it = m_ownedResources.find(handle); it != m_ownedResources.end())
    {
        return it->second.get();
    }
    return nullptr;
}

bool RenderGraphBuilder::Impl::IsPerExecutionResource(const ResourceHandle& handle) const
{
    if (m_importedResources.find(handle) != m_importedResources.end()) return true;
    const auto desc = m_resourceDescRegistry.find(handle);
    return desc != m_resourceDescRegistry.end() && IsAliasedTransient(desc->second);
}

namespace
{
/// @param desc A resource declaration.
/// @return True when the declaration asks the graph to give it storage inside an aliasing block (a
///         `Transient`), instead of letting the resource manager allocate one of its own.
bool IsAliasedTransient(const ResourceDesc& desc)
{
    return std::visit([](const auto& d) { return d.lifetime == ResourceLifetime::Transient; }, desc);
}

/// Destroys a half-compiled node's GPU objects if its compilation leaves by exception.
///
/// A guard rather than a destroy call beside each `throw`, because the failure path is the one nobody
/// exercises while writing a new declaration check: a check added here later is covered by construction,
/// whereas a check added beside a bare `throw` is covered only if its author remembers to add one. This is
/// #55's exception-safety half, and it is what lets a rejected declaration reach the caller as its own
/// message instead of as a leaked-object VUID raised inside `vkDestroyDevice` - which the debug callback
/// turns into an abort, so the message naming the node and resource is the one thing that never appears.
class HalfCompiledNodeGuard
{
public:
    HalfCompiledNodeGuard(CompiledRenderGraphNode& node, VkDevice device) : m_node(node), m_device(device) {}
    ~HalfCompiledNodeGuard()
    {
        if (!m_committed) m_node.Destroy(m_device);
    }

    /// Hand the node to the caller: it is no longer destroyed when this scope exits.
    void Commit() { m_committed = true; }

    HalfCompiledNodeGuard(const HalfCompiledNodeGuard&) = delete;
    HalfCompiledNodeGuard& operator=(const HalfCompiledNodeGuard&) = delete;

private:
    CompiledRenderGraphNode& m_node;
    VkDevice m_device;
    bool m_committed = false;
};
}  // namespace

std::optional<ShaderAssetKey> RenderGraphBuilder::Impl::LoadShaderAsset(const std::string& shaderName)
{
    if (m_services.shaderCache != nullptr) return m_services.shaderCache->LoadShader(shaderName);
    return m_shaderAssetManager.LoadShader(shaderName);
}

const ShaderAsset* RenderGraphBuilder::Impl::ResolveShaderAsset(const ShaderAssetKey& key) const
{
    if (m_services.shaderCache != nullptr) return m_services.shaderCache->GetShaderAsset(key);
    return m_shaderAssetManager.GetShaderAsset(key);
}

CompiledRenderGraphNode RenderGraphBuilder::Impl::CompileRenderGraphNode(const RenderGraphNode& rgn)
{
    // Compile the pipeline and its descriptor-heap slots from the node
    CompiledRenderGraphNode result;
    // From here on the node is owed to this scope: if any declaration check below rejects it, the objects
    // already created for it are destroyed before the exception leaves.
    HalfCompiledNodeGuard releaseOnThrow(result, m_vkDevice);
    // Copy the node's metadata: the compiled node carries everything needed at record time, so it
    // does not point back at the authoring structures.
    result.name = rgn.name;
    result.resourceUses = rgn.resourceUses;
    result.attachmentClearValues = rgn.attachmentClearValues;
    result.async = rgn.async;
    result.queueType = rgn.queueType;
    result.isRayTracing = (rgn.queueType == QueueType::RAY_TRACING);
    result.bindingPoint = (rgn.queueType == QueueType::COMPUTE)       ? VK_PIPELINE_BIND_POINT_COMPUTE
                          : (rgn.queueType == QueueType::RAY_TRACING) ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
                                                                      : VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Retrive and merge shader reflections
    std::vector<ShaderReflection> shaderReflections;
    std::vector<VkShaderModule> shaderModules;
    std::vector<std::uint64_t> shaderHashes;
    for (const auto& shaderKey : rgn.shaders)
    {
        if (shaderKey.IsValid())
        {
            const auto* shaderAsset = ResolveShaderAsset(shaderKey);
            if (shaderAsset)
            {
                shaderReflections.push_back(shaderAsset->shaderReflection);
                shaderModules.push_back(shaderAsset->shaderModule);
                shaderHashes.push_back(HashSpirv(shaderAsset->spirv));
            }
        }
    }
    // Ray tracing pipeline stages, in the order the RT builder expects (raygen, miss, hit).
    std::vector<VkShaderModule> rtShaderModules;
    for (const auto& shaderKey : rgn.rtShaders)
    {
        if (shaderKey.IsValid())
        {
            const auto* shaderAsset = ResolveShaderAsset(shaderKey);
            if (shaderAsset)
            {
                shaderReflections.push_back(shaderAsset->shaderReflection);
                shaderModules.push_back(shaderAsset->shaderModule);
                rtShaderModules.push_back(shaderAsset->shaderModule);
            }
        }
    }
    result.execute = rgn.execute;
    if (shaderReflections.size() > 0)
    {
        ShaderReflection mergedReflection = MergeShaderReflections(shaderReflections);

        if (m_services.descriptorHeap == nullptr)
        {
            throw std::runtime_error("Node '" + rgn.name +
                                     "' needs the descriptor heap, but RenderGraphServices::descriptorHeap "
                                     "is null (the graph path is heap-only since B1b).");
        }
        if (rgn.queueType == QueueType::RAY_TRACING && !m_services.descriptorHeap->CanUseDirectAccelerationStructure())
        {
            // Slang lowers RaytracingAccelerationStructure.Handle to an 8-byte value; the heap
            // descriptor is sized by the implementation's AS descriptor size. A mismatch needs the
            // mapping path, which B1b does not implement, so reject by name instead of feeding the
            // shader a descriptor representation it cannot read (#223).
            throw std::runtime_error(
                "Node '" + rgn.name +
                "' requests the descriptor heap, but the device's acceleration-structure descriptor is " +
                std::to_string(m_services.device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)) +
                " bytes while Slang's direct RaytracingAccelerationStructure handle is 8; the mapping path "
                "is not implemented (B1b, #223).");
        }
        if (!mergedReflection.descriptorBindings.empty())
        {
            throw std::runtime_error("Node '" + rgn.name +
                                     "' declares descriptor bindings; the graph path is heap-only since B1b. "
                                     "Convert the shaders to a .heap.slang variant.");
        }

        for (const ResolvedResourceUse& use : result.resourceUses)
        {
            // Per-execution resources are bound from the slot's ring at Execute (see Execute): an import
            // may be re-pointed between executions without a Build (#83), and a graph-owned transient has
            // one instance per slot, so a static descriptor written here would name slot 0's memory and
            // hand it to every other execution. Persistent graph-owned resources keep the static cache
            // because they are genuinely shared.
            if (IsPerExecutionResource(use.handle)) continue;
            const IGraphResource* pResource = ResolveResource(use.handle);
            if (pResource == nullptr) continue;
            if (use.usage == ResourceUsage::SAMPLED || use.usage == ResourceUsage::STORAGE_IMAGE)
            {
                if (pResource->GetResourceKind() != ResourceKind::IMAGE) continue;
                const auto* pImage = static_cast<const IGraphImage*>(pResource);
                const VkImageLayout layout = use.usage == ResourceUsage::SAMPLED
                                                 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_GENERAL;
                const VkDescriptorType type = use.usage == ResourceUsage::SAMPLED ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                                                  : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                DescriptorKey key{};
                key.type = type;
                key.layout = layout;
                key.view = pImage->GetImageViewCreateInfo();
                const auto [it, inserted] = m_descriptorCache.try_emplace(key);
                if (inserted)
                {
                    it->second = m_services.descriptorHeap->AcquireImage(key.view, layout, type);
                }
                result.heapIndices[HeapIndexKey(use.handle, use.usage)] = it->second.index;
            }
            else if (use.usage == ResourceUsage::STORAGE_BUFFER || use.usage == ResourceUsage::UNIFORM_BUFFER ||
                     use.usage == ResourceUsage::DRAW_COMMAND_BUFFER)
            {
                if (pResource->GetResourceKind() != ResourceKind::BUFFER) continue;
                const auto* pBuffer = static_cast<const IGraphBuffer*>(pResource);
                VkDeviceSize offset = 0;
                VkDeviceSize size = pBuffer->GetSize();
                if (use.bufferRange.has_value())
                {
                    offset = use.bufferRange->offset;
                    size = use.bufferRange->size;
                }
                const VkDescriptorType type = use.usage == ResourceUsage::UNIFORM_BUFFER
                                                  ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                DescriptorKey key{};
                key.type = type;
                key.address = m_services.device->GetBufferDeviceAddress(pBuffer->buffer()) + offset;
                key.size = size;
                const auto [it, inserted] = m_descriptorCache.try_emplace(key);
                if (inserted)
                {
                    it->second = m_services.descriptorHeap->AcquireBuffer(key.address, key.size, type);
                }
                result.heapIndices[HeapIndexKey(use.handle, use.usage)] = it->second.index;
            }
            else if (use.usage == ResourceUsage::ACCEL_STRUCTURE)
            {
                if (pResource->GetResourceKind() != ResourceKind::ACCELERATION_STRUCTURE) continue;
                const auto* pAccelerationStructure = static_cast<const IGraphAccelerationStructure*>(pResource);
                DescriptorKey key{};
                key.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                key.address = pAccelerationStructure->GetDeviceAddress();
                const auto [it, inserted] = m_descriptorCache.try_emplace(key);
                if (inserted)
                {
                    it->second = m_services.descriptorHeap->AcquireAccelerationStructure(key.address);
                }
                result.heapIndices[HeapIndexKey(use.handle, use.usage)] = it->second.index;
            }
        }

        std::vector<VkPushConstantRange> pushConstantRanges;
        for (const auto& pcRange : mergedReflection.pushConstantRanges)
        {
            pushConstantRanges.push_back(
                {.stageFlags = pcRange.stageFlags, .offset = pcRange.offset, .size = pcRange.size});
        }
        result.heapPushRanges = pushConstantRanges;

        // Inspect the node's resolved resource uses to determine attachment formats.
        std::vector<VkFormat> colorAttachmentFormats;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        for (const auto& resourceUse : rgn.resourceUses)
        {
            if (resourceUse.usage == ResourceUsage::COLOR_ATTACHMENT || resourceUse.usage == ResourceUsage::PRESENT)
            {
                // An imported image has no ResourceDesc, so its format comes from the resource. Without
                // this the render pass would be created for the default format and disagree with the
                // image it renders to (the swapchain's format for a present target).
                VkFormat format = resourceUse.format;
                if (format == VK_FORMAT_UNDEFINED)
                {
                    const IGraphResource* pResource = ResolveResource(resourceUse.handle);
                    if (pResource != nullptr && pResource->GetResourceKind() == ResourceKind::IMAGE)
                    {
                        format = static_cast<const IGraphImage*>(pResource)->GetImageFormat();
                    }
                }
                colorAttachmentFormats.push_back(format != VK_FORMAT_UNDEFINED ? format
                                                                               : VK_FORMAT_R16G16B16A16_SFLOAT);
            }
            else if (resourceUse.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
            {
                depthAttachmentFormat =
                    resourceUse.format != VK_FORMAT_UNDEFINED ? resourceUse.format : VK_FORMAT_D32_SFLOAT;
            }
        }

        // Specialization constants. The ids are validated against the node's merged reflection first:
        // the driver ignores map entries for constants a shader does not declare, so a typo would leave
        // that constant on its baked default and surface as a wrong number far from the cause. The width
        // comes from reflection rather than from the caller for the same reason.
        std::vector<VkSpecializationMapEntry> specializationEntries;
        std::vector<uint64_t> specializationValues;
        specializationEntries.reserve(rgn.specializationConstants.size());
        specializationValues.reserve(rgn.specializationConstants.size());
        for (const SpecializationValue& supplied : rgn.specializationConstants)
        {
            const auto& reflected = mergedReflection.specializationConstants;
            const auto it = std::find_if(reflected.begin(), reflected.end(),
                                         [&supplied](const auto& constant) { return constant.id == supplied.id; });
            if (it == reflected.end())
            {
                throw std::runtime_error("Node '" + rgn.name + "' supplies specialization constant " +
                                         std::to_string(supplied.id) + ", which none of its shaders declares.");
            }
            if (it->size == 0 || it->size > sizeof(uint64_t))
            {
                throw std::runtime_error("Node '" + rgn.name + "' supplies specialization constant " +
                                         std::to_string(supplied.id) + " whose reflected size (" +
                                         std::to_string(it->size) + " bytes) is not 1-8 bytes.");
            }
            VkSpecializationMapEntry entry{};
            entry.constantID = supplied.id;
            entry.offset = static_cast<uint32_t>(specializationValues.size() * sizeof(uint64_t));
            entry.size = it->size;
            specializationEntries.push_back(entry);
            specializationValues.push_back(supplied.value);
        }
        // The entries address the values as a fixed-stride blob, of which Vulkan reads `size` bytes per
        // entry. It has to outlive the pipeline-creation calls below, which is why it lives here.
        VkSpecializationInfo specializationInfo{};
        specializationInfo.mapEntryCount = static_cast<uint32_t>(specializationEntries.size());
        specializationInfo.pMapEntries = specializationEntries.empty() ? nullptr : specializationEntries.data();
        specializationInfo.dataSize = specializationValues.size() * sizeof(uint64_t);
        specializationInfo.pData = specializationValues.empty() ? nullptr : specializationValues.data();
        const VkSpecializationInfo* pSpecializationInfo = specializationEntries.empty() ? nullptr : &specializationInfo;

        VkPipelineRenderingCreateInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.size()),
            .pColorAttachmentFormats = colorAttachmentFormats.empty() ? nullptr : colorAttachmentFormats.data(),
            .depthAttachmentFormat = depthAttachmentFormat,
        };

        // Graphics and compute pipelines are cached on every input that changes their creation and on
        // nothing that is descriptor or layout state (#17). A hit is borrowed either from the injected
        // shared cache or from the per-builder fallback map, so the compiled node must not destroy it.
        const bool bCacheable = rgn.queueType == QueueType::GRAPHICS || rgn.queueType == QueueType::COMPUTE;
        PipelineCache* pSharedPipelineCache = m_services.pipelineCache;
        const VkPipelineCache vkPipelineCache =
            pSharedPipelineCache != nullptr ? pSharedPipelineCache->GetVkPipelineCache() : VK_NULL_HANDLE;
        std::string pipelineKey;
        if (bCacheable)
        {
            pipelineKey = MakePipelineKey(rgn.queueType, shaderHashes, rgn.psoDesc, colorAttachmentFormats,
                                          depthAttachmentFormat, rgn.specializationConstants);
            VkPipeline cached = VK_NULL_HANDLE;
            if (pSharedPipelineCache != nullptr)
            {
                cached = pSharedPipelineCache->Find(pipelineKey);
            }
            else if (const auto it = m_pipelineCache.find(pipelineKey); it != m_pipelineCache.end())
            {
                cached = it->second;
            }
            if (cached != VK_NULL_HANDLE)
            {
                result.pipeline = cached;
                result.bOwnsPipeline = false;
            }
        }

        if (result.pipeline == VK_NULL_HANDLE && rgn.queueType == QueueType::GRAPHICS)
        {
            result.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            result.pipeline = CreatePipelineFromPSODesc(rgn.psoDesc, m_vkDevice, shaderModules, renderingInfo,
                                                        pSpecializationInfo, vkPipelineCache);
        }
        else if (result.pipeline == VK_NULL_HANDLE && rgn.queueType == QueueType::COMPUTE)
        {
            result.bindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = shaderModules.front();
            stageInfo.pName = "main";
            stageInfo.pSpecializationInfo = pSpecializationInfo;
            VkPipelineCreateFlags2CreateInfo heapFlags{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
            heapFlags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.pNext = &heapFlags;
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = VK_NULL_HANDLE;
            VK_ASSERT(
                vkCreateComputePipelines(m_vkDevice, vkPipelineCache, 1, &pipelineInfo, nullptr, &result.pipeline));
        }
        if (bCacheable && result.pipeline != VK_NULL_HANDLE && result.bOwnsPipeline)
        {
            if (pSharedPipelineCache != nullptr)
            {
                // The cache owns the entry from here. A duplicate can only come from a concurrent Build
                // on another thread (the builder and the cache are single-threaded contracts), and the
                // duplicate we just created is ours to release.
                if (!pSharedPipelineCache->Insert(pipelineKey, result.pipeline))
                {
                    vkDestroyPipeline(m_vkDevice, result.pipeline, nullptr);
                    result.pipeline = pSharedPipelineCache->Find(pipelineKey);
                    if (result.pipeline == VK_NULL_HANDLE)
                    {
                        throw std::runtime_error("Node '" + rgn.name +
                                                 "' lost a pipeline-cache race for a key that then had no entry.");
                    }
                }
            }
            else
            {
                m_pipelineCache.emplace(std::move(pipelineKey), result.pipeline);
            }
            result.bOwnsPipeline = false;
        }
        if (result.pipeline == VK_NULL_HANDLE && rgn.queueType == QueueType::RAY_TRACING)
        {
            // Refused rather than ignored: the ray tracing pipeline path does not apply specialization
            // constants yet, and silently keeping the baked defaults is the failure this whole change
            // exists to remove. Tracked by #62.
            if (!rgn.specializationConstants.empty())
            {
                throw std::runtime_error("Ray tracing node '" + rgn.name +
                                         "' supplies specialization constants, which the ray tracing pipeline path "
                                         "does not apply yet (#62).");
            }
            result.bindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
            BuildRayTracingPipeline(result, rgn, rtShaderModules);

            // Trace extent: use the first storage image the node writes to.
            for (const auto& resourceUse : rgn.resourceUses)
            {
                if (resourceUse.usage == ResourceUsage::STORAGE_IMAGE && resourceUse.extent.width > 0)
                {
                    result.traceExtent = {resourceUse.extent.width, resourceUse.extent.height};
                    break;
                }
            }
        }
    }

    releaseOnThrow.Commit();
    return result;
}

void RenderGraphBuilder::Impl::BuildRayTracingPipeline(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                                       const std::vector<VkShaderModule>& shaderModules)
{
    if (shaderModules.size() < 3)
    {
        throw std::runtime_error("Ray tracing node '" + logicalNode.name +
                                 "' needs raygen, miss and closest-hit shaders");
    }

    RayTracingPipelineBuilder builder;
    builder.AddShaderModule(shaderModules[0], VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .AddShaderModule(shaderModules[1], VK_SHADER_STAGE_MISS_BIT_KHR)
        .AddShaderModule(shaderModules[2], VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    builder.SetPipelineLayout(VK_NULL_HANDLE).SetMaxRecursionDepth(1);
    VkRayTracingPipelineCreateInfoKHR createInfo = builder.Build();
    // D11: an RT heap pipeline takes the heap flag and a null layout, like the graphics/compute ones.
    VkPipelineCreateFlags2CreateInfo heapFlags{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
    heapFlags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
    createInfo.pNext = &heapFlags;
    createInfo.layout = VK_NULL_HANDLE;
    const VkPipelineCache vkPipelineCache =
        m_services.pipelineCache != nullptr ? m_services.pipelineCache->GetVkPipelineCache() : VK_NULL_HANDLE;
    VK_ASSERT(VkExt::vkCreateRayTracingPipelinesKHR(m_vkDevice, VK_NULL_HANDLE, vkPipelineCache, 1, &createInfo,
                                                    nullptr, &rgn.pipeline));

    // ── Shader binding table ──────────────────────────────────────────────
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    m_services.device->GetPhysicalDeviceProperties(rtProps);

    const uint32_t nHandleSize = rtProps.shaderGroupHandleSize;
    const uint32_t nHandleSizeAligned = AlignUp(nHandleSize, rtProps.shaderGroupHandleAlignment);

    // 1 raygen, 1 miss, 1 hit group.
    VkStridedDeviceAddressRegionKHR& rgenRegion = rgn.sbtRegions[0];
    VkStridedDeviceAddressRegionKHR& missRegion = rgn.sbtRegions[1];
    VkStridedDeviceAddressRegionKHR& hitRegion = rgn.sbtRegions[2];

    rgenRegion.stride = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);
    rgenRegion.size = rgenRegion.stride;
    missRegion.stride = nHandleSizeAligned;
    missRegion.size = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);
    hitRegion.stride = nHandleSizeAligned;
    hitRegion.size = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);

    const uint32_t sbtSize = rgenRegion.size + missRegion.size + hitRegion.size;
    const uint32_t nHandleCount = 3;
    // The handle count and the handle size are 32-bit; multiply in size_t so the allocation and the pointer
    // offsets below cannot wrap before they widen (bugprone-implicit-widening-of-multiplication-result,
    // the #124 class).
    std::vector<uint8_t> handles(static_cast<size_t>(nHandleCount) * nHandleSize);
    VK_ASSERT(VkExt::vkGetRayTracingShaderGroupHandlesKHR(m_vkDevice, rgn.pipeline, 0, nHandleCount, handles.size(),
                                                          handles.data()));

    std::unique_ptr<IGraphBuffer>& sbt = m_shaderBindingTables["SBT_" + logicalNode.name];
    if (!sbt)
    {
        sbt = m_services.resourceAllocator->CreateBuffer(
            sbtSize, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU, "SBT_" + logicalNode.name, GraphBufferPool::ShaderBindingTable);
    }
    IGraphBuffer* pSBT = sbt.get();
    const VkDeviceAddress sbtAddress = m_services.device->GetBufferDeviceAddress(pSBT->buffer());
    rgenRegion.deviceAddress = sbtAddress;
    missRegion.deviceAddress = sbtAddress + rgenRegion.size;
    hitRegion.deviceAddress = sbtAddress + rgenRegion.size + missRegion.size;

    uint8_t* pMapped = static_cast<uint8_t*>(pSBT->Map());
    memcpy(pMapped, handles.data(), nHandleSize);
    memcpy(pMapped + rgenRegion.size, handles.data() + static_cast<size_t>(nHandleSize), nHandleSize);
    memcpy(pMapped + rgenRegion.size + missRegion.size, handles.data() + static_cast<size_t>(2) * nHandleSize,
           nHandleSize);
    pSBT->Unmap();
}

namespace
{
/// @param usage A use's semantic role.
/// @return True when the role makes the image an attachment of the node's framebuffer.
bool IsAttachmentUsage(ResourceUsage usage)
{
    return usage == ResourceUsage::COLOR_ATTACHMENT || usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT ||
           usage == ResourceUsage::RESOLVE_ATTACHMENT || usage == ResourceUsage::INPUT_ATTACHMENT;
}

/// Check a node's shader/execute shape against its queue type (#132).
///
/// A wrong count here compiles a pipeline with a stage missing, which fails much later at draw with a
/// descriptor or push-constant mismatch far from the cause.
/// @param nodeName Node name, for the message.
/// @param info     Its declaration.
void ValidateNodeShape(const std::string& nodeName, const RenderGraphNodeCreateInfo& info)
{
    if (info.queueType != QueueType::CPU && !info.execute)
    {
        throw std::runtime_error("Node '" + nodeName +
                                 "' is a GPU node with no execute callback, so it would record nothing.");
    }
    switch (info.queueType)
    {
        case QueueType::GRAPHICS:
            // A node with no shader is a no-op used for mapping or dependency tests; when it declares a
            // graphics pipeline it has to have exactly the vertex and fragment stages.
            if (!info.shaderNames.empty() && info.shaderNames.size() != 2)
            {
                throw std::runtime_error(
                    "Graphics node '" + nodeName + "' declares " + std::to_string(info.shaderNames.size()) +
                    " shader(s); a graphics pipeline needs exactly a vertex and a fragment stage.");
            }
            break;
        case QueueType::COMPUTE:
            if (!info.shaderNames.empty() && info.shaderNames.size() != 1)
            {
                throw std::runtime_error("Compute node '" + nodeName + "' declares " +
                                         std::to_string(info.shaderNames.size()) +
                                         " shader(s); a compute pipeline takes exactly one.");
            }
            break;
        case QueueType::RAY_TRACING:
        case QueueType::COPY:
        case QueueType::CPU:
        case QueueType::COUNT:
            break;
    }
}

/// The allocation usage a semantic role requires, and the kinds that admit it.
///
/// A role selects a barrier policy, but it also promises something about the allocation: `STORAGE_IMAGE`
/// needs the image created with `STORAGE_BIT`, `UNIFORM_BUFFER` needs `UNIFORM_BUFFER_BIT`. Declaring the
/// role without the bit reaches the driver as an invalid usage for the resource, so the pair is checked
/// against the declaration here (#128).
struct UsageAllocationRequirement
{
    bool bBuffer = false;               ///< The role is valid for a buffer.
    bool bImage = false;                ///< The role is valid for an image.
    const char* bufferFlagName = "";    ///< Symbolic name of the buffer bit, for messages.
    VkBufferUsageFlags bufferFlag = 0;  ///< Bit a buffer must carry.
    const char* imageFlagName = "";     ///< Symbolic name of the image bit, for messages.
    VkImageUsageFlags imageFlag = 0;    ///< Bit an image must carry.
};

/// @param usage The role a node declares.
/// @return What it requires of an allocation, or nothing when the role carries no allocation flag
///         (payloads, push constants) and therefore has nothing to check.
std::optional<UsageAllocationRequirement> GetUsageAllocationRequirement(ResourceUsage usage)
{
    switch (usage)
    {
        case ResourceUsage::SAMPLED:
            return UsageAllocationRequirement{
                .bImage = true, .imageFlagName = "VK_IMAGE_USAGE_SAMPLED_BIT", .imageFlag = VK_IMAGE_USAGE_SAMPLED_BIT};
        case ResourceUsage::STORAGE_IMAGE:
            return UsageAllocationRequirement{
                .bImage = true, .imageFlagName = "VK_IMAGE_USAGE_STORAGE_BIT", .imageFlag = VK_IMAGE_USAGE_STORAGE_BIT};
        case ResourceUsage::COLOR_ATTACHMENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
        case ResourceUsage::DEPTH_STENCIL_ATTACHMENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT};
        case ResourceUsage::INPUT_ATTACHMENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT};
        case ResourceUsage::RESOLVE_ATTACHMENT:
        case ResourceUsage::PRESENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
        case ResourceUsage::UNIFORM_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
        case ResourceUsage::STORAGE_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_STORAGE_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
        case ResourceUsage::VERTEX_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_VERTEX_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
        case ResourceUsage::INDEX_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_INDEX_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
        case ResourceUsage::INDIRECT_BUFFER:
        case ResourceUsage::DRAW_COMMAND_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT};
        case ResourceUsage::TRANSFER_SOURCE:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bImage = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_TRANSFER_SRC_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              .imageFlagName = "VK_IMAGE_USAGE_TRANSFER_SRC_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        case ResourceUsage::TRANSFER_DEST:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bImage = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_TRANSFER_DST_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              .imageFlagName = "VK_IMAGE_USAGE_TRANSFER_DST_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        case ResourceUsage::ACCEL_STRUCTURE:
        case ResourceUsage::TASK_PAYLOAD:
        case ResourceUsage::MESH_PAYLOAD:
        case ResourceUsage::WORKGRAPH_NODE:
        case ResourceUsage::PUSH_CONSTANTS:
            return std::nullopt;
    }
    return std::nullopt;
}

/// @param kind A resource kind.
/// @return Its lower-case name, for messages.
const char* NameOfKind(ResourceKind kind)
{
    switch (kind)
    {
        case ResourceKind::BUFFER:
            return "buffer";
        case ResourceKind::IMAGE:
            return "image";
        case ResourceKind::ACCELERATION_STRUCTURE:
            return "acceleration structure";
        case ResourceKind::SAMPLER:
            return "sampler";
    }
    return "(unknown kind)";
}

/// @param kind A resource kind.
/// @return The indefinite article that reads correctly before `NameOfKind(kind)`.
const char* ArticleForKind(ResourceKind kind)
{
    return (kind == ResourceKind::IMAGE || kind == ResourceKind::ACCELERATION_STRUCTURE) ? "an " : "a ";
}

/// @param resource An imported resource.
/// @return The kind a use has to declare for it.
ResourceKind KindOfResource(const IGraphResource* resource)
{
    switch (resource->GetResourceKind())
    {
        case ResourceKind::BUFFER:
        case ResourceKind::IMAGE:
        case ResourceKind::ACCELERATION_STRUCTURE:
            return resource->GetResourceKind();
        case ResourceKind::SAMPLER:
            break;
    }
    throw std::runtime_error("The imported resource is neither a buffer, an image nor an acceleration structure.");
}

/// Check a use against the resource it names (#128).
///
/// @param nodeName Node declaring the use, for the message.
/// @param use      The use.
/// @param desc     The declared resource's description, or nothing when the resource is imported.
/// @param imported The imported resource, or null when the resource is declared.
void ValidateUseAgainstResource(const std::string& nodeName, const ResourceUse& use,
                                const std::optional<ResourceDesc>& desc, const IGraphResource* imported,
                                QueueType queueType)
{
    ResourceKind resourceKind = ResourceKind::BUFFER;
    if (desc.has_value())
    {
        resourceKind = std::holds_alternative<BufferResourceDesc>(*desc) ? ResourceKind::BUFFER : ResourceKind::IMAGE;
    }
    else if (imported != nullptr)
    {
        resourceKind = KindOfResource(imported);
    }

    // U1: the kind a use declares must be the kind the resource actually is.
    if (use.kind != resourceKind)
    {
        throw std::runtime_error("Node '" + nodeName + "' uses resource '" + use.handle + "' as " +
                                 ArticleForKind(use.kind) + NameOfKind(use.kind) + ", but the resource is " +
                                 ArticleForKind(resourceKind) + NameOfKind(resourceKind) + ".");
    }

    // The declaration has to say *which* write it means (#152): a shader store and a host write need different
    // barrier scopes, and the graph cannot tell them apart from the node's queue type - a GPU node may map a
    // buffer. These three checks are what makes that distinction reliable rather than a convention.
    if (use.io == ResourceIOType::HOST_WRITE)
    {
        if (resourceKind == ResourceKind::IMAGE)
        {
            throw std::runtime_error("Node '" + nodeName + "' declares HOST_WRITE of image '" + use.handle +
                                     "'. This engine has no host write to an image: write a staging buffer and "
                                     "copy from it with TRANSFER_DEST.");
        }
        if (desc.has_value())
        {
            // A two-value allowlist on purpose. The VMA `AUTO*` modes resolve at allocation time, and this check
            // runs before anything is allocated - so it cannot know whether VMA would pick host-visible memory.
            // A buffer whose host visibility the barrier will rely on should say `CPU_TO_GPU`, which puts the
            // property in the declaration rather than in the allocator's choice.
            const auto& bufferDesc = std::get<BufferResourceDesc>(*desc);
            if (bufferDesc.memoryProperties != VMA_MEMORY_USAGE_CPU_TO_GPU &&
                bufferDesc.memoryProperties != VMA_MEMORY_USAGE_CPU_ONLY)
            {
                throw std::runtime_error("Node '" + nodeName + "' declares HOST_WRITE of buffer '" + use.handle +
                                         "', which was declared with device-only memory. A host write needs "
                                         "VMA_MEMORY_USAGE_CPU_TO_GPU or CPU_ONLY.");
            }
        }
    }
    else if (queueType == QueueType::CPU && use.io == ResourceIOType::WRITE)
    {
        // Both kinds, because the argument is the same for both and stronger for an image: a CPU node has no
        // command buffer, and this engine has no host image write at all - so a CPU node's `WRITE` of an image
        // can only mean nothing, while it still marks the resource written for the lifetime check (#155).
        if (resourceKind == ResourceKind::IMAGE)
        {
            throw std::runtime_error("Node '" + nodeName + "' is a CPU node and declares WRITE of image '" +
                                     use.handle +
                                     "'. This engine has no host image write: write a staging buffer and copy "
                                     "from it with TRANSFER_DEST.");
        }
        throw std::runtime_error("Node '" + nodeName + "' is a CPU node and declares WRITE of buffer '" + use.handle +
                                 "'. A CPU node writes through the host, so declare HOST_WRITE: WRITE means a "
                                 "shader store, and the host write would be in no barrier's source scope.");
    }

    // U2: the role must admit that kind, and a declared resource must carry the allocation bit it implies.
    const std::optional<UsageAllocationRequirement> requirement = GetUsageAllocationRequirement(use.usage);
    if (!requirement.has_value()) return;

    const bool bAdmitsKind = resourceKind == ResourceKind::BUFFER ? requirement->bBuffer : requirement->bImage;
    if (!bAdmitsKind)
    {
        throw std::runtime_error("Node '" + nodeName + "' uses " + NameOfKind(resourceKind) + " '" + use.handle +
                                 "' with usage " + NameOf(use.usage) + ", which is not a " + NameOfKind(resourceKind) +
                                 " usage.");
    }

    // An imported resource carries no desc, so its usage flags are the caller's promise; the kind checks
    // above are what the graph can verify for it.
    if (!desc.has_value()) return;

    if (resourceKind == ResourceKind::BUFFER)
    {
        const auto& bufferDesc = std::get<BufferResourceDesc>(*desc);
        if ((bufferDesc.usage & requirement->bufferFlag) == 0)
        {
            throw std::runtime_error("Node '" + nodeName + "' uses buffer '" + use.handle + "' as " +
                                     NameOf(use.usage) + ", but it was declared without " +
                                     requirement->bufferFlagName + ".");
        }
    }
    else
    {
        const auto& imageDesc = std::get<ImageResourceDesc>(*desc);
        if ((imageDesc.usage & requirement->imageFlag) == 0)
        {
            throw std::runtime_error("Node '" + nodeName + "' uses image '" + use.handle + "' as " + NameOf(use.usage) +
                                     ", but it was declared without " + requirement->imageFlagName + ".");
        }
    }
}
}  // namespace

/// Check that an image role fits the image's format (#143).
///
/// Extents are checked separately; this is the format half. A depth role on a colour format (and the
/// reverse) is a declaration error the driver would otherwise report, and a storage image on a format the
/// device cannot store to is the same class.
/// @param nodeName       Node declaring the use, for the message.
/// @param use            The use.
/// @param physicalDevice Device whose format support decides the storage check.
void ValidateImageRoleFormat(const std::string& nodeName, const ResolvedResourceUse& use,
                             VkPhysicalDevice physicalDevice)
{
    if (use.kind != ResourceKind::IMAGE) return;

    const bool bDepthFormat = IsDepthFormat(use.format);
    const std::string sFormat = std::to_string(static_cast<int>(use.format));
    switch (use.usage)
    {
        case ResourceUsage::DEPTH_STENCIL_ATTACHMENT:
            if (!bDepthFormat)
            {
                throw std::runtime_error("Node '" + nodeName + "' uses image '" + use.handle +
                                         "' as a depth/stencil attachment, but format " + sFormat +
                                         " is a colour format.");
            }
            break;
        case ResourceUsage::COLOR_ATTACHMENT:
        case ResourceUsage::RESOLVE_ATTACHMENT:
            if (bDepthFormat)
            {
                throw std::runtime_error("Node '" + nodeName + "' uses image '" + use.handle +
                                         "' as a colour attachment, but format " + sFormat + " is a depth format.");
            }
            break;
        case ResourceUsage::STORAGE_IMAGE:
        {
            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, use.format, &formatProperties);
            if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
            {
                throw std::runtime_error("Node '" + nodeName + "' uses image '" + use.handle +
                                         "' as a storage image, but format " + sFormat +
                                         " has no storage support on this device.");
            }
            break;
        }
        default:
            break;
    }
}

void RenderGraphBuilder::Impl::AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo)
{
    const std::string& nodeName = nodeCreateInfo.nodeName;

    if (m_renderGraphNodes.find(nodeName) != m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node with name '" + nodeCreateInfo.nodeName +
                                 "' already exists in the render graph.");
    }

    ValidateNodeShape(nodeName, nodeCreateInfo);

    // `async` is accepted on every device, but only a dedicated compute family makes it concurrent. On a
    // single family the node still gets its own compute segment, on the graphics family, so the timeline
    // serialises it. Warn once per process - a frame loop rebuilds every frame, so per-build would be
    // noise - and let an author choose from the same query this message names.
    if (nodeCreateInfo.async && !m_services.device->IsComputeQueueDedicated())
    {
        static std::once_flag s_asyncDegradedWarning;
        std::call_once(s_asyncDegradedWarning,
                       [&nodeName, this]
                       {
                           std::cerr << "[WARNING]: node '" << nodeName
                                     << "' asks for async compute, but this device has no dedicated compute "
                                        "family (graphics family "
                                     << m_services.device->GetGraphicsQueueFamily()
                                     << " is also the compute family). The node still gets its own compute "
                                        "segment, but on that family, so it is serialised rather than "
                                        "concurrent. Query RenderDevice::IsComputeQueueDedicated() to choose "
                                        "async per device."
                                     << std::endl;
                       });
    }

    m_renderGraphNodes[nodeName] = {.name = nodeName};
    m_dependencyGraph.AddNode(nodeName);

    auto& rgn = m_renderGraphNodes.at(nodeName);
    rgn.queueType = nodeCreateInfo.queueType;
    rgn.async = nodeCreateInfo.async;
    rgn.costHint = nodeCreateInfo.costHint;
    rgn.attachmentClearValues = nodeCreateInfo.attachmentClearValues;
    rgn.specializationConstants = nodeCreateInfo.specializationConstants;

    // Load shaders
    if (nodeCreateInfo.shaderNames.size() > static_cast<size_t>(MAX_SHADER_STAGES))
    {
        throw std::runtime_error("Node '" + nodeName + "' declares more shaders than MAX_SHADER_STAGES.");
    }
    int shaderIdx = 0;
    for (const auto& shaderName : nodeCreateInfo.shaderNames)
    {
        auto key = LoadShaderAsset(shaderName);
        if (!key)
        {
            // Skipping the stage here produced a pipeline compiled without it, which failed much later
            // at draw time with a descriptor or push-constant mismatch far from the cause. A shader the
            // node asked for and the build did not produce is a hard error.
            throw std::runtime_error("Node '" + nodeName + "' declares shader '" + shaderName +
                                     "', which could not be loaded.");
        }
        rgn.shaders[shaderIdx++] = key.value();
    }

    // Load ray tracing shaders (raygen / miss / closest hit).
    if (nodeCreateInfo.rtShaderNames.size() > rgn.rtShaders.size())
    {
        throw std::runtime_error("Node '" + nodeName + "' declares more ray tracing shaders than the " +
                                 std::to_string(rgn.rtShaders.size()) + " supported (raygen / miss / closest hit).");
    }
    for (size_t i = 0; i < nodeCreateInfo.rtShaderNames.size(); ++i)
    {
        auto key = LoadShaderAsset(nodeCreateInfo.rtShaderNames[i]);
        if (!key)
        {
            throw std::runtime_error("Ray tracing node '" + nodeName + "' declares shader '" +
                                     nodeCreateInfo.rtShaderNames[i] + "', which could not be loaded.");
        }
        rgn.rtShaders[i] = key.value();
    }

    // Resolve resource uses
    for (const auto& resourceUse : nodeCreateInfo.resourceUses)
    {
        if (m_resourceDescRegistry.find(resourceUse.handle) == m_resourceDescRegistry.end() &&
            m_importedResources.find(resourceUse.handle) == m_importedResources.end())
        {
            throw std::runtime_error("Node '" + nodeName + "' uses resource '" + resourceUse.handle +
                                     "' which is neither declared with AddResource nor imported.");
        }
        const std::optional<ResourceDesc> resourceDesc = GetResourceDesc(resourceUse.handle);
        const IGraphResource* pImportedResource =
            resourceDesc.has_value() ? nullptr : m_importedResources.at(resourceUse.handle);
        ValidateUseAgainstResource(nodeName, resourceUse, resourceDesc, pImportedResource, nodeCreateInfo.queueType);
        ResolvedResourceUse resolved = ResolveResourceUse(resourceUse);
        // A range on a buffer bound by the heap descriptor is validated at Build(), which knows the
        // descriptor type from the use, so a bad range fails there rather than mid-record in Execute.
        // Fill in allocation-time properties (format / extent) so pipeline + barrier generation can use them.
        if (const auto desc = GetResourceDesc(resourceUse.handle))
        {
            if (const auto* imageDesc = std::get_if<ImageResourceDesc>(&*desc))
            {
                resolved.format = imageDesc->format;
                resolved.extent = {imageDesc->extent.width, imageDesc->extent.height, 1};
            }
        }
        else if (resourceUse.kind == ResourceKind::IMAGE)
        {
            // An imported image has no ResourceDesc, so its format and extent come from the resource
            // itself. Without them the render pass and the render area would be built from zeros.
            const IGraphResource* pResource = ResolveResource(resourceUse.handle);
            if (pResource != nullptr && pResource->GetResourceKind() == ResourceKind::IMAGE)
            {
                const auto* pImage = static_cast<const IGraphImage*>(pResource);
                resolved.format = pImage->GetImageFormat();
                const VkExtent2D extent = pImage->GetImageExtent();
                resolved.extent = {extent.width, extent.height, 1};
            }
        }
        rgn.resourceUses.push_back(std::move(resolved));
    }

    // One framebuffer per node, so every attachment has to resolve to the same extent (#132). Two
    // different sizes are a declaration error; the render pass would be built from one of them and the
    // other attachment would be the wrong size for it.
    const ResolvedResourceUse* pFirstAttachment = nullptr;
    for (const ResolvedResourceUse& use : rgn.resourceUses)
    {
        if (!IsAttachmentUsage(use.usage)) continue;
        if (pFirstAttachment == nullptr)
        {
            pFirstAttachment = &use;
            continue;
        }
        if (use.extent.width != pFirstAttachment->extent.width || use.extent.height != pFirstAttachment->extent.height)
        {
            throw std::runtime_error(
                "Node '" + nodeName + "' attaches '" + use.handle + "' at " + std::to_string(use.extent.width) + "x" +
                std::to_string(use.extent.height) + " and '" + pFirstAttachment->handle + "' at " +
                std::to_string(pFirstAttachment->extent.width) + "x" + std::to_string(pFirstAttachment->extent.height) +
                "; one framebuffer needs one extent.");
        }
    }

    for (const ResolvedResourceUse& use : rgn.resourceUses)
    {
        ValidateImageRoleFormat(nodeName, use, m_services.device->GetPhysicalDevice());
    }

    rgn.psoDesc = nodeCreateInfo.psoDesc;
    rgn.execute = nodeCreateInfo.execute;
}

void RenderGraphBuilder::Impl::AddDependency(const std::string& fromNode, const std::string& toNode)
{
    if (m_renderGraphNodes.find(fromNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + fromNode + "' does not exist in the render graph.");
    }

    if (m_renderGraphNodes.find(toNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + toNode + "' does not exist in the render graph.");
    }

    // CPU nodes are executed host-side before any GPU segment is submitted, so they cannot
    // consume GPU results. Reject the ordering rather than silently running it too early.
    if (m_renderGraphNodes.at(toNode).queueType == QueueType::CPU &&
        m_renderGraphNodes.at(fromNode).queueType != QueueType::CPU)
    {
        throw std::runtime_error("Dependency '" + fromNode + "' -> '" + toNode +
                                 "': CPU nodes run before GPU work is submitted and cannot depend on a GPU node.");
    }

    if (!m_dependencyGraph.AddEdge(fromNode, toNode))
    {
        throw std::runtime_error("Adding dependency from '" + fromNode + "' to '" + toNode +
                                 "' creates a cycle in the render graph.");
    }
}

void RenderGraphBuilder::Impl::ValidateTransientLifetimes(const std::vector<std::string>& executionOrder)
{
    m_transientLiveRanges.clear();
    std::unordered_set<ResourceHandle> written;

    for (size_t nNode = 0; nNode < executionOrder.size(); ++nNode)
    {
        const RenderGraphNode& node = m_renderGraphNodes.at(executionOrder[nNode]);
        for (const ResolvedResourceUse& use : node.resourceUses)
        {
            // Imported resources belong to the caller, so their content is not this execution's to
            // produce. That is the whole difference between an import and a graph-owned resource.
            if (m_importedResources.find(use.handle) != m_importedResources.end()) continue;

            const std::optional<ResourceDesc> desc = GetResourceDesc(use.handle);
            if (!desc.has_value()) continue;
            if (std::visit([](const auto& d) { return d.lifetime; }, *desc) != ResourceLifetime::Transient)
            {
                continue;
            }

            // TRANSFER_WRITE counts: a copy or an upload is a write like any other, and leaving it out
            // rejected a graph in which one node's transfer fills a transient and the next reads it -
            // which is what transfer roles are for. Found by the transfer round trip test, and it was
            // reachable before that through DRAW_COMMAND_BUFFER with TRANSFER_WRITE, which no test used.
            const bool bWrites = IsWrite(use.io);
            if (!bWrites && written.find(use.handle) == written.end())
            {
                // Named, because the whole value of the check is telling the author which line is wrong.
                // The message also names the escape, since the mistake is usually a missing Persistent.
                throw std::runtime_error(
                    "Node '" + executionOrder[nNode] + "' reads transient resource '" + use.handle +
                    "' with nothing writing it first. A transient's content is valid only from its first "
                    "write within one execution, so this read would take whatever the memory happens to "
                    "hold. Declare it Persistent if it is meant to carry content across executions.");
            }
            if (bWrites)
            {
                written.insert(use.handle);
            }

            // The live interval. A read cannot come before the first write in a graph that passed the
            // check above, so the first use is a write and the two definitions coincide here.
            auto it = m_transientLiveRanges.find(use.handle);
            if (it == m_transientLiveRanges.end())
            {
                m_transientLiveRanges.emplace(use.handle, TransientLiveRange{nNode, nNode});
            }
            else
            {
                it->second.lastUse = nNode;
            }
        }
    }
}

void RenderGraphBuilder::Impl::DestroyAliasedTransients()
{
    // The caller must have drained the executor: Build() waits when the plan changes, and the destructor
    // waits before calling this. An execution in flight may still read these blocks through descriptors
    // it already recorded. Resources first, then memory, per slot - a block cannot be released while a
    // resource is still bound to it, which is the order VMA's own implementation uses.
    for (TransientSlotStorage& storage : m_transientSlots)
    {
        storage.buffers.clear();
        storage.images.clear();
        for (GraphMemoryBlock& block : storage.blocks) m_services.resourceAllocator->FreeAliasingBlock(block);
        storage.blocks.clear();
    }
    m_transientSlots.clear();
    m_transientMemoryPlan = TransientMemoryPlan{};
    m_transientPlanKey.clear();
}

std::vector<RenderGraphBuilder::Impl::TransientPlanInput> RenderGraphBuilder::Impl::GatherTransientPlanInputs(
    const std::vector<std::string>& executionOrder) const
{
    // A resource may share a slot only with one whose uses run on the same queue family. Within a family
    // the graph's own barriers order the hand-over; across families nothing does, because the executor
    // inserts a wait only for a *shared handle* and an aliased pair is two handles. So each resource is
    // given the family its GPU uses run on, or none for a host-written one, and one used on both families
    // is marked as sharing with nothing.
    std::unordered_map<ResourceHandle, uint32_t> transientFamily;
    std::unordered_set<ResourceHandle> spansFamilies;
    for (const std::string& nodeName : executionOrder)
    {
        const RenderGraphNode& node = m_renderGraphNodes.at(nodeName);
        if (node.queueType == QueueType::CPU) continue;
        const uint32_t family = GetQueueFamilyForType(GetQueueKey(node.queueType, node.async));
        for (const ResolvedResourceUse& use : node.resourceUses)
        {
            const auto [it, inserted] = transientFamily.emplace(use.handle, family);
            if (!inserted && it->second != family) spansFamilies.insert(use.handle);
        }
    }

    std::vector<TransientPlanInput> inputs;
    inputs.reserve(m_resourceDescRegistry.size());
    for (const auto& [handle, desc] : m_resourceDescRegistry)
    {
        if (!IsAliasedTransient(desc)) continue;
        if (m_importedResources.find(handle) != m_importedResources.end()) continue;

        const auto liveRange = m_transientLiveRanges.find(handle);
        if (liveRange == m_transientLiveRanges.end()) continue;

        TransientPlanInput input;
        input.handle = handle;
        input.pDesc = &desc;
        input.liveRange = liveRange->second;
        // Domain 0 is "no GPU use", so a single family is stored one-based to keep the two apart.
        const auto familyIt = transientFamily.find(handle);
        input.sharingDomain = (familyIt == transientFamily.end()) ? 0u : static_cast<uint64_t>(1u) + familyIt->second;
        input.canShareSlot = spansFamilies.find(handle) == spansFamilies.end();
        inputs.push_back(input);
    }

    // Sorted by handle, not left in registry order: an unordered_map's iteration order is not part of the
    // declaration, and the key must not change because a rehash happened.
    std::sort(inputs.begin(), inputs.end(),
              [](const TransientPlanInput& a, const TransientPlanInput& b) { return a.handle < b.handle; });
    return inputs;
}

std::string RenderGraphBuilder::Impl::MakeTransientPlanKey(const std::vector<TransientPlanInput>& inputs) const
{
    // A byte string rather than a hash, like the pipeline key: two different declarations must not be able
    // to collide into reusing the wrong arena.
    std::string key;
    AppendPipelineKey(key, m_executor.GetInFlightCount());
    AppendPipelineKey(key, static_cast<uint64_t>(inputs.size()));
    for (const TransientPlanInput& input : inputs)
    {
        AppendPipelineKey(key, static_cast<uint64_t>(input.handle.size()));
        key.append(input.handle);
        if (const auto* pBufferDesc = std::get_if<BufferResourceDesc>(input.pDesc))
        {
            AppendPipelineKey(key, uint8_t{0});
            AppendPipelineKey(key, pBufferDesc->count);
            AppendPipelineKey(key, pBufferDesc->stride);
            AppendPipelineKey(key, static_cast<uint32_t>(pBufferDesc->usage));
            AppendPipelineKey(key, static_cast<uint32_t>(pBufferDesc->memoryProperties));
        }
        else
        {
            const auto& imageDesc = std::get<ImageResourceDesc>(*input.pDesc);
            AppendPipelineKey(key, uint8_t{1});
            AppendPipelineKey(key, static_cast<uint32_t>(imageDesc.format));
            AppendPipelineKey(key, imageDesc.extent.width);
            AppendPipelineKey(key, imageDesc.extent.height);
            AppendPipelineKey(key, imageDesc.mips);
            AppendPipelineKey(key, imageDesc.layers);
            AppendPipelineKey(key, static_cast<uint32_t>(imageDesc.usage));
        }
        AppendPipelineKey(key, static_cast<uint64_t>(input.liveRange.firstUse));
        AppendPipelineKey(key, static_cast<uint64_t>(input.liveRange.lastUse));
        AppendPipelineKey(key, input.sharingDomain);
        AppendPipelineKey(key, static_cast<uint8_t>(input.canShareSlot));
    }
    return key;
}

RenderGraphBuilder::Impl::TransientSlotStorage RenderGraphBuilder::Impl::CreateTransientSlotStorage(
    const std::vector<TransientPlanInput>& inputs, std::vector<TransientMemoryRequest>* pRequests)
{
    TransientSlotStorage storage;
    for (const TransientPlanInput& input : inputs)
    {
        VkMemoryRequirements requirements = {};
        TransientMemoryClass classKey;

        if (const auto* pBufferDesc = std::get_if<BufferResourceDesc>(input.pDesc))
        {
            const VkDeviceSize nSize = pBufferDesc->count * pBufferDesc->stride;
            auto resource =
                std::make_unique<TransientBufferResource>(nSize, pBufferDesc->usage, pBufferDesc->memoryProperties,
                                                          *m_services.resourceAllocator, *m_services.device);
            VkBuffer buffer = m_services.resourceAllocator->CreateUnboundBuffer(nSize, pBufferDesc->usage);
            resource->Adopt(buffer);
            requirements = m_services.resourceAllocator->QueryBufferRequirements(buffer);
            classKey = ClassOf(*input.pDesc, requirements.memoryTypeBits);
            storage.buffers.emplace(input.handle, std::move(resource));
        }
        else
        {
            const auto& imageDesc = std::get<ImageResourceDesc>(*input.pDesc);
            // The same create-info computation the persistent path uses, from the same function: an
            // image's aliasing class is its create info, so the two paths must not be able to disagree.
            const VkImageCreateInfo imageInfo = MakeImageCreateInfo(m_services.device->GetPhysicalDevice(), imageDesc);
            auto resource =
                std::make_unique<TransientImageResource>(imageInfo, *m_services.resourceAllocator, *m_services.device);
            VkImage image = m_services.resourceAllocator->CreateUnboundImage(resource->ImageInfoForCreate());
            resource->Adopt(image);
            requirements = m_services.resourceAllocator->QueryImageRequirements(image);
            classKey = ClassOf(*input.pDesc, requirements.memoryTypeBits);
            // Images have no memory usage in their declaration, and a render target's storage is
            // device-local by definition, so the block's usage is not something the caller chose here.
            classKey.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
            storage.images.emplace(input.handle, std::move(resource));
        }

        if (pRequests == nullptr) continue;
        TransientMemoryRequest request;
        request.handle = input.handle;
        request.classKey = classKey;
        request.size = requirements.size;
        request.alignment = requirements.alignment;
        request.memoryTypeBits = requirements.memoryTypeBits;
        request.firstUse = input.liveRange.firstUse;
        request.lastUse = input.liveRange.lastUse;
        request.sharingDomain = input.sharingDomain;
        request.canShareSlot = input.canShareSlot;
        pRequests->push_back(std::move(request));
    }
    return storage;
}

void RenderGraphBuilder::Impl::AllocateAndBindTransientBlocks(TransientSlotStorage& storage)
{
    for (const TransientMemoryBlock& block : m_transientMemoryPlan.blocks)
    {
        VkMemoryRequirements blockRequirements = {};
        blockRequirements.size = block.size;
        blockRequirements.alignment = block.alignment;
        blockRequirements.memoryTypeBits = block.memoryTypeBits;

        const GraphMemoryBlock allocation =
            m_services.resourceAllocator->AllocateAliasingBlock(blockRequirements, block.classKey.memoryUsage);
        storage.blocks.push_back(allocation);

        for (const TransientMemorySlot& slot : block.slots)
        {
            for (const ResourceHandle& member : slot.members)
            {
                if (block.classKey.isImage)
                {
                    TransientImageResource* pResource = storage.images.at(member).get();
                    m_services.resourceAllocator->BindImageToBlock(pResource->getImage(), allocation, slot.offset);
                    // After the bind, never before: a view of an image with no memory bound is a validation
                    // error, and it is the one order this path has to get right.
                    pResource->CreateView();
                    continue;
                }
                TransientBufferResource* pResource = storage.buffers.at(member).get();
                m_services.resourceAllocator->BindBufferToBlock(pResource->buffer(), allocation, slot.offset);
                // Told now rather than at creation: the offset is what the plan just decided, and it is
                // what `Map` needs to find this resource's bytes inside the block.
                pResource->BindTo(allocation, slot.offset);
            }
        }
    }
}

void RenderGraphBuilder::Impl::BuildAliasedTransients(const std::vector<TransientPlanInput>& inputs,
                                                      const std::string& planKey)
{
    const uint32_t slotCount = m_executor.GetInFlightCount();
    if (planKey == m_transientPlanKey && m_transientSlots.size() == slotCount)
    {
        // The arenas already match this declaration. A per-frame Build() must reach here: recreating them
        // would free memory a previous execution may still be reading, and would defeat the point of a
        // build cache.
        return;
    }

    // A changed plan invalidates storage that in-flight executions may still reference through recorded
    // descriptors and barriers. Build() drains before it destroys the compiled graph; this is the same
    // contract at the point the storage actually goes away.
    DestroyAliasedTransients();

    // Slot 0 is created first because the plan needs the resources' device-queried requirements. Every
    // other slot then binds its own resources at the same planned offsets: the plan is shared, storage is
    // not.
    std::vector<TransientMemoryRequest> requests;
    requests.reserve(inputs.size());
    m_transientSlots.resize(slotCount);
    m_transientSlots[0] = CreateTransientSlotStorage(inputs, &requests);
    m_transientMemoryPlan = PlanTransientMemory(requests);
    AllocateAndBindTransientBlocks(m_transientSlots[0]);
    for (uint32_t slot = 1; slot < slotCount; ++slot)
    {
        m_transientSlots[slot] = CreateTransientSlotStorage(inputs, nullptr);
        AllocateAndBindTransientBlocks(m_transientSlots[slot]);
    }
    // Set only after every slot is complete: a build that throws part-way leaves this empty, so the next
    // Build cannot mistake a partial set of arenas for a match and reuse it.
    m_transientPlanKey = planKey;
}

void RenderGraphBuilder::Impl::Build()
{
    if (m_dependencyGraph.HasCycle())
    {
        throw std::runtime_error("Render graph contains a cycle!");
    }

    std::vector<std::string> executionOrder = m_dependencyGraph.TopologicalSort();
    if (executionOrder.size() != m_renderGraphNodes.size())
    {
        throw std::runtime_error("Render graph contains a cycle; topological sort is incomplete.");
    }

    // Before allocating anything: a declaration error should not cost an allocation, and this needs only
    // the order and the declarations.
    ValidateTransientLifetimes(executionOrder);

    // Decide whether the transient arenas still match this declaration *before* tearing anything down.
    // Both passes are pure: no resource is created to answer the question. An unchanged key is what lets
    // a per-frame Build() keep the arenas a previous execution may still be reading; a changed key
    // invalidates them, so every execution has to finish before the compiled graph or the storage is
    // destroyed. The wait is explicit here rather than accidental inside the teardown.
    const std::vector<TransientPlanInput> transientInputs = GatherTransientPlanInputs(executionOrder);
    const std::string transientPlanKey = MakeTransientPlanKey(transientInputs);
    if (transientPlanKey != m_transientPlanKey || m_transientSlots.size() != m_executor.GetInFlightCount())
    {
        m_executor.WaitIdle();
    }

    // TODO(#253): a compiled node's own pipeline (ray tracing today) is destroyed here even when an
    // execution that bound it is still in flight; graphics and compute pipelines are cache-borrowed and
    // survive. The drain above is about the transient storage, not about an owned pipeline.
    m_compiledGraph.Destroy(m_vkDevice);
    m_compiledGraph.ReserveNodes(m_renderGraphNodes.size());

    // The per-execution rings live in the heap service, whose slot count was fixed at Initialize;
    // a builder that runs further ahead than that would call BeginExecution on a slot the rings do not
    // have. Report the mismatch at Build rather than from a node's Execute.
    if (m_services.descriptorHeap != nullptr &&
        m_executor.GetInFlightCount() > m_services.descriptorHeap->GetInFlightCount())
    {
        throw std::runtime_error("RenderGraphBuilder: inFlightCount " + std::to_string(m_executor.GetInFlightCount()) +
                                 " exceeds the descriptor heap's " +
                                 std::to_string(m_services.descriptorHeap->GetInFlightCount()) +
                                 " execution slots; initialize the heap with at least that many.");
    }

    // Transients are per execution now: each in-flight slot owns a complete copy of the plan. A graph-owned
    // *Persistent* resource is different - it survives the frame boundary by definition, so there is no
    // per-slot copy to make and two overlapping executions would race a shared write. Refuse it by name
    // instead of letting the hazard reach the GPU; the per-execution alternative is to import the
    // resource and re-point the handle between executions (#83).
    if (m_executor.GetInFlightCount() > 1)
    {
        for (const auto& [handle, desc] : m_resourceDescRegistry)
        {
            if (m_importedResources.find(handle) != m_importedResources.end()) continue;
            if (IsAliasedTransient(desc)) continue;
            throw std::runtime_error(
                "Graph declares persistent graph-owned resource '" + handle +
                "' while inFlightCount > 1; a persistent resource is shared by every execution, so "
                "overlapping executions would write it concurrently. Import a per-execution resource and "
                "re-point it between executions (#83), or use inFlightCount = 1.");
        }
    }

    // Allocate all graph-owned resources before compiling nodes. A buffer transient is skipped here and
    // created unbound below instead: its memory belongs to a block, and the manager's path would give it
    // memory of its own.
    //
    // Reuse is keyed on the declaration, not only the handle (#209): the manager returns an existing
    // resource by name, so a repeated Build() with an unchanged desc keeps it, while a changed extent,
    // format, usage or a size that differs is recreated. The manager remains the owner of a
    // graph-owned resource (the builder destroys only aliased transients); the descriptor cache keys on
    // the resolved resource, not on this name.
    for (const auto& [handle, desc] : m_resourceDescRegistry)
    {
        if (m_importedResources.find(handle) != m_importedResources.end()) continue;
        if (IsAliasedTransient(desc)) continue;
        auto existing = m_ownedResources.find(handle);
        if (existing != m_ownedResources.end())
        {
            if (ResourceMatchesDesc(m_services.device->GetPhysicalDevice(), *existing->second, desc)) continue;
            // The old resource may still be read by the previous execution: Build() can follow a
            // non-blocking Execute(). Wait until the GPU is done before freeing it, then drop the
            // descriptor cache so the next compile cannot hand out a slot that still names the old
            // resource. #40 replaces the wait with a retire behind the execution timeline.
            m_executor.WaitIdle();
            DestroyCachedPipelines();
            ReleaseCachedDescriptors();
            m_ownedResources.erase(existing);
        }
        m_ownedResources.emplace(handle, AllocateResource(desc, *m_services.resourceAllocator,
                                                          m_services.device->GetPhysicalDevice(), handle));
    }
    BuildAliasedTransients(transientInputs, transientPlanKey);

    try
    {
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
                }
                else
                {
                    resource.version = resourceCurrentVersions[handle];
                    if (IsWrite(resource.io))
                    {
                        resourceCurrentVersions[handle]++;
                    }
                }
            }

            // Compile RenderGraphNode
            m_compiledGraph.AddNode(CompileRenderGraphNode(node));
        }

        // Derive the scheduling plan (queue segments + cross-queue transfers) once, so Execute() reads
        // it rather than recomputing it every frame.
        //
        // The plan freezes the device's queue family indices, so it must be rebuilt if the device
        // changes. Today Build() always reruns after a device change (a builder is recreated), but a
        // caller that reused a compiled graph across devices would have to rebuild it.
        m_compiledGraph.RebuildExecutionPlan(GetQueueFamilies());
    }
    catch (...)
    {
        // A build that fails leaves an *empty* graph, not a partial one. The nodes compiled before the
        // failing one are destroyed here rather than whenever the builder happens to be destroyed, and the
        // failing node's own objects were released by its compile guard. Two reasons this matters: a graph
        // holding half a pass list is one Execute() would run with a plan that was never derived, and the
        // objects in it would otherwise still be alive when the device is destroyed, which is exactly the
        // leaked-object error that erases the diagnostic this issue exists to deliver.
        m_compiledGraph.Destroy(m_vkDevice);
        throw;
    }
}

bool RenderGraphBuilder::Impl::BeginRendering(VkCommandBuffer cmdBuf, const CompiledRenderGraphNode& rgn,
                                              RenderGraphNodeContext& /*ctx*/,
                                              std::unordered_set<ResourceHandle>& alreadyWritten,
                                              std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                                              std::vector<VkClearValue>& clearValues)
{
    colorAttachments.clear();
    clearValues.clear();

    VkRenderingAttachmentInfo depthAttachment{};
    bool hasDepth = false;
    VkExtent2D renderArea{0, 0};

    uint32_t clearIndex = 0;
    for (const auto& use : rgn.resourceUses)
    {
        if (use.usage != ResourceUsage::COLOR_ATTACHMENT && use.usage != ResourceUsage::DEPTH_STENCIL_ATTACHMENT &&
            use.usage != ResourceUsage::PRESENT)
        {
            continue;
        }

        const IGraphResource* pResource = ResolveResource(use.handle);
        if (pResource == nullptr || pResource->GetResourceKind() != ResourceKind::IMAGE) continue;
        const auto* image = static_cast<const IGraphImage*>(pResource);

        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = image->getView();
        attachment.imageLayout = use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT
                                     ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const bool firstWrite = alreadyWritten.find(use.handle) == alreadyWritten.end();
        // Clear only a pure first write. A READ_WRITE (or READ) attachment means "keep what is
        // already there", e.g. blending on top of a previous pass, so it must LOAD.
        const bool bClearAttachment = firstWrite && use.io == ResourceIOType::WRITE;
        attachment.loadOp = bClearAttachment ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (bClearAttachment)
        {
            VkClearValue clearValue{};
            if (use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
            {
                clearValue.depthStencil = {1.0f, 0};
            }
            else if (clearIndex < rgn.attachmentClearValues.size())
            {
                clearValue = rgn.attachmentClearValues[clearIndex++];
            }
            attachment.clearValue = clearValue;
            alreadyWritten.insert(use.handle);
        }
        else if (use.usage == ResourceUsage::COLOR_ATTACHMENT || use.usage == ResourceUsage::PRESENT)
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
            // The attachment image is authoritative: an imported image (a swapchain target) can change
            // extent between builds while a declaration-time `use.extent` stays at the old size, and a
            // render area larger than the image view is a validation error (VUID-VkRenderingInfo-pNext-06079).
            const VkExtent2D imageExtent = image->GetImageExtent();
            renderArea = imageExtent.width > 0 && imageExtent.height > 0
                             ? imageExtent
                             : VkExtent2D{use.extent.width, use.extent.height};
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
    VkViewport viewport = {0.0F, 0.0F, static_cast<float>(renderArea.width), static_cast<float>(renderArea.height),
                           0.0F, 1.0F};
    VkRect2D scissor = {{0, 0}, renderArea};
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    return true;
}

void RenderGraphBuilder::Impl::RecordPresentTransition(VkCommandBuffer cmdBuf,
                                                       const std::vector<ResolvedResourceUse>& resourceUses)
{
    std::vector<VkImageMemoryBarrier2> barriers;
    for (const auto& use : resourceUses)
    {
        if (use.usage != ResourceUsage::PRESENT || use.kind != ResourceKind::IMAGE) continue;
        const IGraphResource* pResource = ResolveResource(use.handle);
        if (pResource == nullptr || pResource->GetResourceKind() != ResourceKind::IMAGE) continue;
        const auto* image = static_cast<const IGraphImage*>(pResource);

        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        // Present does not read through a pipeline, so it has no stage or access of its own.
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image->getImage();
        barrier.subresourceRange = {AspectForFormat(image->GetImageFormat()), 0, 1, 0, 1};
        barriers.push_back(barrier);
    }
    if (barriers.empty()) return;

    VkDependencyInfo dependencyInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependencyInfo.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);
}

void RenderGraphBuilder::Impl::RecordBarriers(VkCommandBuffer cmdBuf,
                                              const std::vector<ResolvedResourceUse>& resourceUses,
                                              uint32_t queueFamily, BarrierObserver* pObserver)
{
    // Everything that decides *which* barriers this node needs lives in `DeriveBarrierBatch`, which is device-free
    // and therefore assertable - the assembly used to be inline here, where dropping an append left the suite green
    // (#160). What remains here is resolving the handles and handing the result to Vulkan.
    const BarrierBatch batch = DeriveBarrierBatch(
        resourceUses, m_resourceAccessStates, queueFamily, SupportedStages(),
        [this](const ResourceHandle& handle)
        {
            BarrierTarget target;
            const IGraphResource* pResource = ResolveResource(handle);
            if (pResource == nullptr)
            {
                // Master dropped the barrier here, which leaves a race; recording one with a
                // null handle would be an invalid barrier, which is UB. Neither is a barrier,
                // so a handle that does not resolve is reported - unreachable for a graph that
                // built, since every use was checked at `AddNode`.
                throw std::runtime_error(
                    "Barrier for '" + handle +
                    "' is needed but the resource does not resolve, so no barrier can address it.");
            }
            switch (pResource->GetResourceKind())
            {
                case ResourceKind::IMAGE:
                {
                    const auto* pImage = static_cast<const IGraphImage*>(pResource);
                    target.image = pImage->getImage();
                    target.aspect = AspectForFormat(pImage->GetImageFormat());
                    break;
                }
                case ResourceKind::ACCELERATION_STRUCTURE:
                    target.buffer = static_cast<const IGraphAccelerationStructure*>(pResource)->GetBackingBuffer();
                    break;
                case ResourceKind::BUFFER:
                    target.buffer = static_cast<const IGraphBuffer*>(pResource)->buffer();
                    break;
                case ResourceKind::SAMPLER:
                    break;
            }
            return target;
        },
        pObserver);

    if (batch.images.empty() && batch.buffers.empty() && !batch.memory.has_value()) return;

    VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.memoryBarrierCount = batch.memory.has_value() ? 1 : 0;
    dependencyInfo.pMemoryBarriers = batch.memory.has_value() ? &batch.memory.value() : nullptr;
    dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(batch.buffers.size());
    dependencyInfo.pBufferMemoryBarriers = batch.buffers.empty() ? nullptr : batch.buffers.data();
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(batch.images.size());
    dependencyInfo.pImageMemoryBarriers = batch.images.empty() ? nullptr : batch.images.data();

    vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);
}

void RenderGraphBuilder::Impl::RecordQueueTransferBarriers(VkCommandBuffer cmdBuf, uint32_t srcQueueFamily,
                                                           uint32_t dstQueueFamily, bool bAcquire,
                                                           const std::vector<ResourceHandle>& handles,
                                                           BarrierObserver* pObserver)
{
    if (bAcquire)
    {
        // The actual barrier is emitted by RecordBarriers on the first use in this queue.
        for (const auto& handle : handles)
        {
            m_resourceAccessStates[handle].pendingAcquireFamily = static_cast<int32_t>(srcQueueFamily);
        }
        return;
    }

    // Release: hand ownership of the resource from the producing queue family to the consumer. The barrier is
    // built by `AppendReleaseTransfer`, which is device-free, so what it records can be asserted without a
    // device - the part that regressed in #153 is the emitted barrier, not the decision to emit one.
    QueueTransferBarriers transferBarriers;
    for (const auto& handle : handles)
    {
        const IGraphResource* pResource = ResolveResource(handle);
        if (pResource == nullptr)
        {
            // The handle had to resolve to be scheduled at `Build()`, so a null here is a bug rather than
            // something to skip - and skipping is how #153 stayed invisible.
            throw std::runtime_error("Queue handover of '" + handle + "' is scheduled but has no resource to address.");
        }

        std::optional<ResourceKind> kind;
        VkImage image = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        switch (pResource->GetResourceKind())
        {
            case ResourceKind::IMAGE:
            {
                const auto* pImage = static_cast<const IGraphImage*>(pResource);
                kind = ResourceKind::IMAGE;
                image = pImage->getImage();
                aspect = AspectForFormat(pImage->GetImageFormat());
                break;
            }
            case ResourceKind::ACCELERATION_STRUCTURE:
                // Its memory is its backing buffer's, so the handover is a buffer barrier on that buffer.
                kind = ResourceKind::ACCELERATION_STRUCTURE;
                buffer = static_cast<const IGraphAccelerationStructure*>(pResource)->GetBackingBuffer();
                break;
            case ResourceKind::BUFFER:
                kind = ResourceKind::BUFFER;
                buffer = static_cast<const IGraphBuffer*>(pResource)->buffer();
                break;
            case ResourceKind::SAMPLER:
                break;
        }
        if (!kind.has_value())
        {
            // Not "a sampler": the resource is something the barrier machinery does not recognise, and naming
            // the wrong kind in the message is how a reader chases the wrong thing.
            throw std::runtime_error("Queue handover of '" + handle +
                                     "' is scheduled, but the resource is neither an image, a buffer nor an "
                                     "acceleration structure.");
        }

        QueueTransferBarriers single;
        AppendReleaseTransfer(single, handle, *kind, image, buffer, aspect, m_resourceAccessStates[handle],
                              srcQueueFamily, dstQueueFamily);
        // The release half of a handover is as much a part of "what ran" as the acquire, and it is derived on a
        // different path (`AppendReleaseTransfer` here, `DeriveBarrierBatch` for the acquire), so the observer has
        // to be told on this one too or a trace shows half of every handover (#166 review A1).
        if (pObserver != nullptr)
        {
            pObserver->OnUseBarriers(handle, UseBarriers{.barriers = single, .memory = std::nullopt});
        }
        transferBarriers.images.insert(transferBarriers.images.end(), single.images.begin(), single.images.end());
        // Appended one at a time rather than with a range `insert`: GCC 13's inlined range-insert copy trips
        // `-Warray-bounds` on this call site in Release (offset 80 out of [0, 80]) - a false positive, since the
        // source range is at most one barrier and the destination is a growing vector - and the single-element
        // append is also what the loop means (#192). The range is 0 or 1 elements, so this is not a bulk copy.
        for (const VkBufferMemoryBarrier2& barrier : single.buffers)
        {
            transferBarriers.buffers.push_back(barrier);
        }
    }

    if (transferBarriers.images.empty() && transferBarriers.buffers.empty()) return;

    VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(transferBarriers.buffers.size());
    dependencyInfo.pBufferMemoryBarriers = transferBarriers.buffers.empty() ? nullptr : transferBarriers.buffers.data();
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(transferBarriers.images.size());
    dependencyInfo.pImageMemoryBarriers = transferBarriers.images.empty() ? nullptr : transferBarriers.images.data();

    vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);
}

VkPipelineStageFlags2 RenderGraphBuilder::Impl::SupportedStages() const
{
    return m_services.device != nullptr ? m_services.device->EnabledPipelineStages() : ~VkPipelineStageFlags2{0};
}

uint32_t RenderGraphBuilder::Impl::GetQueueFamilyForType(QueueType type) const
{
    return GetQueueFamilyForQueueType(type, GetQueueFamilies());
}

RenderGraphQueueFamilies RenderGraphBuilder::Impl::GetQueueFamilies() const
{
    return RenderGraphQueueFamilies{m_services.device->GetGraphicsQueueFamily(),
                                    m_services.device->GetComputeQueueFamily()};
}

void RenderGraphBuilder::Impl::EnsureMappingsReleased(const std::string& nodeName) const
{
    // Only aliased buffers can be host-mapped, and they are the reason the rule exists: the pointer
    // points into a block shared with other transients, so it must be released before the next node can
    // be bound to the same bytes. A Persistent or imported resource maps its own storage and is not this
    // rule's subject.
    //
    // Only the executing slot's copy can alias this execution's bytes; another slot's buffer belongs to
    // a different execution and cannot be overwritten by a later node of this one. Scanning the current
    // slot is also what keeps the node named in the error accurate.
    if (m_currentTransientSlot >= m_transientSlots.size()) return;
    for (const auto& [handle, pBuffer] : m_transientSlots[m_currentTransientSlot].buffers)
    {
        if (!pBuffer->IsMapped()) continue;
        throw std::runtime_error(
            "Node '" + nodeName + "' left graph-owned transient buffer '" + handle +
            "' mapped after its callback returned. A mapping is valid only for the node whose use declared the "
            "resource: the buffer's bytes are shared with other transients, and a later node may be bound to "
            "them. Unmap the resource before the callback returns.");
    }
}

/// The blocking form: records, submits, and waits for the GPU before returning. Kept as the
/// default so every existing caller keeps the contract it had. The overload below does not wait,
/// but the builder still runs one execution in flight, so calls serialise rather than overlap.
void RenderGraphBuilder::Impl::Execute()
{
    Execute(RenderGraphExecuteInfo{});
    m_executor.WaitIdle();
}

namespace
{
/// A compiled graph rendered as the views the trace needs.
///
/// Takes the compiled nodes rather than including `CompiledRenderGraph.h` from the trace header: the record is about
/// names, queues and uses, and this is the one place that knows how to get them. Owns the use lists because
/// `TraceNodeView` holds spans into them, and reserves up front so no span is invalidated while building.
struct TraceViews
{
    /// Uses per compiled node, in compiled order.
    std::vector<std::vector<TraceUseView>> uses;
    /// One view per compiled node.
    std::vector<TraceNodeView> nodes;

    /// @param compiled The graph's compiled nodes.
    void Build(const std::vector<CompiledRenderGraphNode>& compiled)
    {
        uses.clear();
        nodes.clear();
        uses.resize(compiled.size());
        nodes.reserve(compiled.size());
        for (std::size_t i = 0; i < compiled.size(); ++i)
        {
            uses[i].reserve(compiled[i].resourceUses.size());
            for (const ResolvedResourceUse& use : compiled[i].resourceUses)
            {
                uses[i].push_back(TraceUseView{.handle = use.handle, .io = use.io});
            }
            nodes.push_back(TraceNodeView{.name = compiled[i].name,
                                          .queueType = compiled[i].queueType,
                                          .bAsync = compiled[i].async,
                                          .uses = std::span<const TraceUseView>(uses[i])});
        }
    }
};

/// @return True when `MUYO_RG_TRACE` is set to something other than empty or `0`.
///
/// Read once per process: this is a diagnostic switch, not a per-frame decision, and a `getenv` per execution would
/// be a measurable part of a quiet frame's cost.
bool TraceRequestedByEnvironment()
{
    static const bool s_requested = []()
    {
        const char* pValue = std::getenv("MUYO_RG_TRACE");
        return pValue != nullptr && pValue[0] != '\0' && !(pValue[0] == '0' && pValue[1] == '\0');
    }();
    return s_requested;
}

/// Emit a trace as the `MUYO_RG_TRACE` variable asked for: `1` prints it, anything else is a file appended to.
///
/// NDJSON to a file rather than an array, so a long capture stays streamable and `tail -1` is a valid document. The
/// file is opened per execution and closed again: tracing is a diagnostic, and holding a handle open for the
/// process lifetime would make "did this run trace?" unanswerable from outside.
/// @param trace Trace to emit.
void EmitTraceToEnvironment(const RenderGraphTrace& trace)
{
    const char* pValue = std::getenv("MUYO_RG_TRACE");
    if (pValue == nullptr || pValue[0] == '\0')
    {
        return;
    }
    if (pValue[0] == '1' && pValue[1] == '\0')
    {
        std::fputs(ToText(trace).c_str(), stdout);
        return;
    }

    std::ofstream out(pValue, std::ios::app);
    if (!out)
    {
        std::cerr << "[WARNING]: MUYO_RG_TRACE=" << pValue << " could not be opened; trace dropped" << std::endl;
        return;
    }
    out << ToJson(trace) << '\n';
}
}  // namespace

RenderGraph::TraceGraph RenderGraphBuilder::Impl::BuildPlanGraph() const
{
    // The same three pieces a trace starts from, and the same views helper: the difference between this and a trace is
    // that nothing has executed, so there are no barriers, no timeline values and no timings to record - only the plan.
    TraceViews views;
    views.Build(m_compiledGraph.GetNodes());
    return RenderGraph::ToGraph(
        RenderGraph::TracePlan(m_compiledGraph.GetExecutionPlan(), views.nodes, BuildTraceScope()));
}

std::string RenderGraphBuilder::Impl::ToMermaid() const
{
    const RenderGraph::TraceGraph graph = BuildPlanGraph();
    // Nothing compiled yet: an empty document rather than a valid-but-contentless one, so a caller writing this to a
    // file gets an empty file instead of a diagram that says nothing.
    if (graph.nodes.empty())
    {
        return {};
    }
    return RenderGraph::ToMermaid(graph);
}

std::string RenderGraphBuilder::Impl::ToDot() const
{
    const RenderGraph::TraceGraph graph = BuildPlanGraph();
    if (graph.nodes.empty())
    {
        return {};
    }
    return RenderGraph::ToDot(graph);
}

TraceScope RenderGraphBuilder::Impl::BuildTraceScope() const
{
    TraceScope scope;
    if (m_services.device == nullptr)
    {
        return scope;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_services.device->GetPhysicalDevice(), &properties);
    scope.deviceName = properties.deviceName;
    scope.graphicsFamily = m_services.device->GetGraphicsQueueFamily();
    scope.computeFamily = m_services.device->GetComputeQueueFamily();
    scope.bDedicatedCompute = m_services.device->IsComputeQueueDedicated();
    scope.inFlightCount = m_executor.GetInFlightCount();
    // Timestamps are phase 2 (#56): the fields exist so the shape does not change, and `bKnown` stays false so a
    // reader cannot mistake "not measured" for "measured as zero".
    scope.timestamps = TraceTimestamps{};
    return scope;
}

void RenderGraphBuilder::Impl::Execute(const RenderGraphExecuteInfo& info)
{
    m_resourceAccessStates.clear();
    // An imported image whose caller stated its layout is already in that layout; without this seed the
    // first use would derive an `UNDEFINED` source and discard the content being imported.
    for (const auto& [handle, layout] : m_importedImageLayouts)
    {
        SeedImportedImageLayout(m_resourceAccessStates[handle], layout);
    }

    // A trace is recorded when a sink is installed or `MUYO_RG_TRACE` asks for one. The environment variable is
    // read once per process: it is a diagnostic switch, not a per-frame decision, and re-reading it per execution
    // would cost more than the trace itself on a quiet frame.
    const bool bTraceToEnv = TraceRequestedByEnvironment();
    TraceRecorder recorder;
    TraceViews views;
    TraceRecorder* pRecorder = (m_traceSink || bTraceToEnv) ? &recorder : nullptr;
    // Advanced for every execution, traced or not: the counter describes the builder's lifetime, not the trace's, so
    // that a consumer comparing two traces from one process can see that executions happened in between.
    const uint64_t execution = m_nextExecution++;
    if (pRecorder != nullptr)
    {
        views.Build(m_compiledGraph.GetNodes());
        recorder.Begin(m_compiledGraph.GetExecutionPlan(), views.nodes, BuildTraceScope(), execution);
    }

    RenderGraphNodeContext context{.queueType = QueueType::GRAPHICS};
    // Nodes reach resources through the context, so it has to resolve the way the graph does - which
    // includes transients the graph owns and persistent resources it allocated itself.
    context.resolveResource = [this](const ResourceHandle& handle) { return ResolveResource(handle); };
    context.allocateRoot = [this](size_t size, size_t alignment) { return AllocateRoot(size, alignment); };

    std::unordered_set<ResourceHandle> alreadyWritten;
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    std::vector<VkClearValue> clearValues;

    const std::vector<CompiledRenderGraphNode>& nodes = m_compiledGraph.GetNodes();
    const RenderGraphExecutionPlan& plan = m_compiledGraph.GetExecutionPlan();

    // Acquire the slot before any callback runs: a CPU node can allocate a root slice too, and the slice
    // must live in the arena of the execution that will read it. AcquireSlot waits for this slot's
    // previous execution, which is what makes resetting its cursor safe.
    RenderGraphExecutor::ExecutionSlot slot = m_executor.AcquireSlot(plan);
    m_currentRootSlot = slot.index;
    // The execution's transient instances are the ones every callback, barrier and descriptor below must
    // see. Set before the first CPU node: a host-side callback can reach a transient too.
    m_currentTransientSlot = slot.index;
    if (m_rootArenas.size() <= slot.index) m_rootArenas.resize(slot.index + 1);
    m_rootArenas[slot.index].cursor = 0;
    const std::vector<VkCommandBuffer>& segmentCmdBuffers = slot.segmentCommandBuffers;

    // ── CPU nodes run host-side, before any GPU segment is recorded. ──────────────────────────
    for (const std::size_t i : plan.cpuNodes)
    {
        const CompiledRenderGraphNode& rgn = nodes[i];
        context.queueType = rgn.queueType;
        context.commandBuffer = VK_NULL_HANDLE;
        context.pipeline = VK_NULL_HANDLE;
        rgn.execute(context);
        EnsureMappingsReleased(rgn.name);
        // The transition itself lives in `ApplyHostWrites`, which is device-free and asserted. What remains here
        // is the one call, which nothing observes: pinning it needs an execution trace (#56), not another
        // extraction.
        ApplyHostWrites(rgn.resourceUses, m_resourceAccessStates);
    }

    if (plan.IsEmpty()) return;

    // Imported resources and graph-owned transients are bound per execution, from this slot's descriptor
    // rings. AcquireSlot has waited for the slot, so ResetRing can recycle its rings; the acquisitions
    // write the ring regions of the host staging buffer, which the segment-start Upload then copies. Two
    // in-flight executions have disjoint ring slots, so neither overwrites the other's descriptors, a
    // caller can re-point an imported handle between executions without a Build (#83), and a transient's
    // descriptor names this execution's instance rather than slot 0's.
    std::vector<std::unordered_map<std::string, uint32_t>> executionHeapIndices(nodes.size());
    if (m_services.descriptorHeap != nullptr)
    {
        m_services.descriptorHeap->BeginExecution(slot.index);
        m_services.descriptorHeap->ResetRing(slot.index);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            // A node with no pipeline has no shader to read a descriptor index; acquiring one for it
            // would consume a ring slot for nothing.
            if (nodes[i].queueType == QueueType::CPU || nodes[i].pipeline == VK_NULL_HANDLE) continue;
            for (const ResolvedResourceUse& use : nodes[i].resourceUses)
            {
                if (!IsPerExecutionResource(use.handle)) continue;
                const IGraphResource* pResource = ResolveResource(use.handle);
                if (pResource == nullptr) continue;
                std::optional<uint32_t> index;
                if (use.usage == ResourceUsage::SAMPLED || use.usage == ResourceUsage::STORAGE_IMAGE)
                {
                    if (pResource->GetResourceKind() != ResourceKind::IMAGE) continue;
                    const auto* pImage = static_cast<const IGraphImage*>(pResource);
                    const VkImageLayout layout = use.usage == ResourceUsage::SAMPLED
                                                     ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_GENERAL;
                    const VkDescriptorType type = use.usage == ResourceUsage::SAMPLED
                                                      ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                      : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    index =
                        m_services.descriptorHeap->AcquireImage(pImage->GetImageViewCreateInfo(), layout, type, true)
                            .index;
                }
                else if (use.usage == ResourceUsage::STORAGE_BUFFER || use.usage == ResourceUsage::UNIFORM_BUFFER ||
                         use.usage == ResourceUsage::DRAW_COMMAND_BUFFER)
                {
                    if (pResource->GetResourceKind() != ResourceKind::BUFFER) continue;
                    const auto* pBuffer = static_cast<const IGraphBuffer*>(pResource);
                    VkDeviceSize offset = 0;
                    VkDeviceSize size = pBuffer->GetSize();
                    if (use.bufferRange.has_value())
                    {
                        offset = use.bufferRange->offset;
                        size = use.bufferRange->size;
                    }
                    const VkDescriptorType type = use.usage == ResourceUsage::UNIFORM_BUFFER
                                                      ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                      : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    index = m_services.descriptorHeap
                                ->AcquireBuffer(m_services.device->GetBufferDeviceAddress(pBuffer->buffer()) + offset,
                                                size, type, true)
                                .index;
                }
                else if (use.usage == ResourceUsage::ACCEL_STRUCTURE)
                {
                    if (pResource->GetResourceKind() != ResourceKind::ACCELERATION_STRUCTURE) continue;
                    const auto* pAccelerationStructure = static_cast<const IGraphAccelerationStructure*>(pResource);
                    index = m_services.descriptorHeap
                                ->AcquireAccelerationStructure(pAccelerationStructure->GetDeviceAddress(), true)
                                .index;
                }
                if (index.has_value())
                {
                    executionHeapIndices[i][HeapIndexKey(use.handle, use.usage)] = *index;
                }
            }
        }
    }

    // Queue segments and cross-queue transfers were derived from the compiled graph at Build().
    const std::vector<RenderGraphQueueSegment>& segments = plan.segments;
    const std::vector<RenderGraphQueueTransfer>& transfers = plan.transfers;

    // ── Record each segment into its own command buffer. ───────────────────────────────────────
    // The executor owns the command buffers: it can only recycle one once that slot's fence has
    // signalled, which is what makes recording into a slot safe while earlier executions are still
    // in flight. Acquiring blocks while every slot is busy, bounding how far the CPU runs ahead.
    for (size_t s = 0; s < segments.size(); ++s)
    {
        const uint32_t queueFamily = GetQueueFamilyForType(segments[s].queueType);
        VkCommandBuffer cmdBuf = segmentCmdBuffers[s];

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_ASSERT(vkBeginCommandBuffer(cmdBuf, &beginInfo));

        // Heap descriptors were written into the service's host staging buffer at Build(). Copy them into
        // the device heaps and bind both heaps once, before this segment records any node. The upload is
        // a vkCmdCopyBuffer and is therefore illegal inside a render pass; a graphics node's callback
        // runs inside BeginRendering, so the graph cannot leave this to the callback. Binding once per
        // command buffer is also what the design requires (DescriptorHeap-Bindless-Design.md §9).
        bool bSegmentUsesHeap = false;
        for (size_t i = segments[s].begin; i < segments[s].end && !bSegmentUsesHeap; ++i)
        {
            bSegmentUsesHeap = nodes[i].queueType != QueueType::CPU;
        }
        // A graph whose GPU nodes have no shaders (a present-only or copy-only frame) needs no heap:
        // the service is null on a heap-less device and there is nothing acquired to upload. A node
        // with shaders would have been rejected at Build on such a device.
        if (bSegmentUsesHeap && m_services.descriptorHeap != nullptr)
        {
            m_services.descriptorHeap->Upload(cmdBuf);
        }

        // Before anything is recorded for this segment: a release barrier is emitted here, before the first node,
        // and it belongs to this segment just as much as the per-node barriers do.
        if (pRecorder != nullptr)
        {
            pRecorder->BeginSegment(s);
        }

        // Mark resources handed over from another queue; RecordBarriers emits the acquire barrier.
        for (const auto& transfer : transfers)
        {
            if (transfer.consumer == s)
            {
                RecordQueueTransferBarriers(cmdBuf, transfer.producerFamily, transfer.consumerFamily, true,
                                            {transfer.handle});
            }
        }

        for (size_t i = segments[s].begin; i < segments[s].end; ++i)
        {
            const auto& rgn = nodes[i];
            // Segments contain GPU nodes only (see RebuildExecutionPlan); guard anyway so a CPU node
            // can never be recorded as GPU work.
            if (rgn.queueType == QueueType::CPU) continue;
            context.queueType = rgn.queueType;
            context.commandBuffer = cmdBuf;
            context.pipeline = rgn.pipeline;
            context.descriptorHeap = m_services.descriptorHeap;
            context.getHeapIndex = [&rgn, &executionHeapIndices, i](const ResourceHandle& handle,
                                                                    ResourceUsage usage) -> std::optional<uint32_t>
            {
                const std::string key = HeapIndexKey(handle, usage);
                if (const auto it = rgn.heapIndices.find(key); it != rgn.heapIndices.end()) return it->second;
                const auto it = executionHeapIndices[i].find(key);
                if (it == executionHeapIndices[i].end()) return std::nullopt;
                return it->second;
            };
            context.maxPushDataSize =
                m_services.descriptorHeap != nullptr ? m_services.descriptorHeap->GetProperties().maxPushDataSize : 0;
            context.bindingPoint = rgn.bindingPoint;

            // Bracket the node so a validation tool attributes the commands and anything it reports to this node
            // rather than to the segment's command buffer, which several nodes share (#134). The graph emits the
            // Vulkan label only; a capture-tool marker is host policy and is not the graph's to emit (#272).
            CommandBufferLabel nodeMarker(m_vkDevice, cmdBuf, rgn.name, m_services.captureMarker);

            // 1. Barrier batch: transition all resources used by this node. The observer is how the trace learns
            // which resource each barrier belongs to, since the batch itself carries only handles.
            if (pRecorder != nullptr)
            {
                pRecorder->BeginSegment(s);
            }
            RecordBarriers(cmdBuf, rgn.resourceUses, queueFamily, pRecorder);

            // 2. Auto render pass wrap for graphics nodes.
            bool rendering = false;
            if (rgn.queueType == QueueType::GRAPHICS)
            {
                rendering = BeginRendering(cmdBuf, rgn, context, alreadyWritten, colorAttachments, clearValues);
            }

            // 3. Bind the pipeline. The heaps were bound at the start of the segment.
            if (rgn.pipeline != VK_NULL_HANDLE)
            {
                vkCmdBindPipeline(cmdBuf, rgn.bindingPoint, rgn.pipeline);
            }

            // 4. User GPU work.
            context.pushedRanges.clear();
            rgn.execute(context);
            for (const VkPushConstantRange& range : rgn.heapPushRanges)
            {
                const uint32_t end = range.offset + range.size;
                const bool covered =
                    std::any_of(context.pushedRanges.begin(), context.pushedRanges.end(), [&](const auto& pushed)
                                { return pushed.first <= range.offset && pushed.second >= end; });
                if (!covered)
                {
                    throw std::runtime_error("Node '" + rgn.name + "' has a heap push-constant range [" +
                                             std::to_string(range.offset) + ", " + std::to_string(end) +
                                             ") not covered by PushData");
                }
            }

            // 4b. Ray tracing nodes issue the trace automatically; the SBT is graph-managed.
            if (rgn.isRayTracing && rgn.pipeline != VK_NULL_HANDLE && rgn.traceExtent.width > 0)
            {
                const VkStridedDeviceAddressRegionKHR callable{0, 0, 0};
                VkExt::vkCmdTraceRaysKHR(cmdBuf, &rgn.sbtRegions[0], &rgn.sbtRegions[1], &rgn.sbtRegions[2], &callable,
                                         rgn.traceExtent.width, rgn.traceExtent.height, 1);
            }

            // 5. End auto render pass.
            if (rendering)
            {
                vkCmdEndRendering(cmdBuf);
            }

            // 5b. A present target leaves the node in COLOR_ATTACHMENT_OPTIMAL; the transition to
            // PRESENT_SRC is after the render pass, because the presentation engine - its consumer - is
            // not a node, so no consumer's barrier can do it.
            RecordPresentTransition(cmdBuf, rgn.resourceUses);

            // 6. Mapping lifetime: the callback's host mapping must not outlive the node. Checked after
            // the render pass is closed, so the throw (a programmer error) cannot leave one open.
            EnsureMappingsReleased(rgn.name);
        }

        // Release ownership of resources consumed by a later queue.
        for (const auto& transfer : transfers)
        {
            if (transfer.producer == s)
            {
                RecordQueueTransferBarriers(cmdBuf, transfer.producerFamily, transfer.consumerFamily, false,
                                            {transfer.handle}, pRecorder);
            }
        }

        VK_ASSERT(vkEndCommandBuffer(cmdBuf));
    }

    // ── Submit through the executor, which owns the handover semaphores and the waiting. ───────
    m_executor.Submit(plan, slot, info, pRecorder);

    if (pRecorder != nullptr)
    {
        if (m_traceSink)
        {
            m_traceSink(recorder.GetTrace());
        }
        if (bTraceToEnv)
        {
            EmitTraceToEnvironment(recorder.GetTrace());
        }
    }
}

std::vector<std::string> RenderGraphBuilder::Impl::GetExecutionOrder() const
{
    return m_dependencyGraph.TopologicalSort();
}

}  // namespace Muyo::RenderGraph
