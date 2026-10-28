#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "RenderGraphExecutionPlan.h"
#include "RenderGraphNodeContext.h"
#include "RenderGraphNodeResource.h"

namespace Muyo::RenderGraph
{
/// GPU objects and execution metadata for one node, produced by `RenderGraphBuilder::Build()`.
///
/// Immutable between builds: `Execute()` only reads it. The definition carries everything a node
/// needs at record time, so it does not point back at the authoring structures.
struct CompiledRenderGraphNode
{
    /// Node name, used in diagnostics.
    std::string name;
    /// Resolved resource accesses, used to emit barriers between nodes.
    std::vector<ResolvedResourceUse> resourceUses;
    /// Clear values for the node's attachments, in attachment declaration order.
    std::vector<VkClearValue> attachmentClearValues;
    /// True when the node opted into the dedicated async compute queue.
    bool async = false;

    /// Compiled pipeline, or `VK_NULL_HANDLE` for CPU nodes. Every graph pipeline is created with
    /// `VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT` and `layout = VK_NULL_HANDLE` (B1b).
    VkPipeline pipeline = VK_NULL_HANDLE;
    /// True when this node owns `pipeline`. False for a pipeline reused from the builder's pipeline
    /// cache: Destroy() must then leave it alive, because another node or the next Build still uses it.
    bool bOwnsPipeline = true;
    /// True when this node dispatches a ray tracing pipeline.
    bool isRayTracing = false;
    /// Shader binding table regions, for ray tracing nodes.
    std::array<VkStridedDeviceAddressRegionKHR, 3> sbtRegions{};
    /// Extent the ray tracing trace call is issued over.
    VkExtent2D traceExtent = {0, 0};
    /// Resource use -> service index, for a heap node. Key is `handle#usage`.
    std::unordered_map<std::string, uint32_t> heapIndices;
    /// Push-constant ranges declared by a heap node's shaders; each must be covered by PushData.
    std::vector<VkPushConstantRange> heapPushRanges;
    /// Queue the node was compiled for.
    QueueType queueType = QueueType::GRAPHICS;
    /// Bind point of the compiled pipeline.
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    /// Destroy this node's GPU objects and clear the handles. Safe to call repeatedly.
    ///
    /// Used in two places, and the second is the one that matters: destroying a built graph, and
    /// releasing a node whose compilation threw part-way through. A compile creates its pipeline while its
    /// declaration checks are interleaved around it - so a rejected declaration can throw with the
    /// pipeline already created, and a caller that never sees it is a caller that cannot destroy it.
    /// @param device Device the objects were created on.
    void Destroy(VkDevice device);

    /// Records the node's work.
    RenderGraphNodeCallback execute;
};

/// The compiled, reusable result of `RenderGraphBuilder::Build()`.
///
/// Owns the per-node GPU objects and the metadata an executor needs. It is immutable between builds
/// and deliberately holds no per-execution state: barrier bookkeeping, command buffers and
/// in-flight slots belong to whoever executes it.
class CompiledRenderGraph
{
public:
    /// @return The compiled nodes, in execution (topological) order.
    const std::vector<CompiledRenderGraphNode>& GetNodes() const { return m_nodes; }
    /// @return Number of compiled nodes.
    std::size_t GetNodeCount() const { return m_nodes.size(); }

    /// Append a compiled node. Used by `RenderGraphBuilder::Build()` when compiling.
    /// @param node Compiled node to append.
    void AddNode(CompiledRenderGraphNode node) { m_nodes.push_back(std::move(node)); }
    /// Reserve room for `count` nodes, to avoid reallocation while compiling.
    /// @param count Number of nodes expected.
    void ReserveNodes(std::size_t count) { m_nodes.reserve(count); }

    /// Recompute the scheduling plan from the current nodes. Called by `RenderGraphBuilder::Build()`
    /// after the nodes are compiled.
    /// @param families Queue family indices used to decide which resources cross a queue family.
    void RebuildExecutionPlan(const RenderGraphQueueFamilies& families);
    /// @return The scheduling plan for the current nodes. Read-only during execution.
    const RenderGraphExecutionPlan& GetExecutionPlan() const { return m_executionPlan; }

    /// Destroy every node's GPU objects and clear the node list. Safe to call repeatedly.
    /// @param device Device the objects were created on.
    void Destroy(VkDevice device);

private:
    std::vector<CompiledRenderGraphNode> m_nodes;
    RenderGraphExecutionPlan m_executionPlan;
};
}  // namespace Muyo::RenderGraph
