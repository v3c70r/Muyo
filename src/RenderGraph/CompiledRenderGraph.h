#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <string>
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

    /// Compiled pipeline, or `VK_NULL_HANDLE` for CPU nodes.
    VkPipeline pipeline = VK_NULL_HANDLE;
    /// Pipeline layout the pipeline was created with.
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    /// Descriptor set layouts used by this node's pipeline layout.
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    /// Descriptor sets bound for this node.
    std::vector<VkDescriptorSet> descriptorSets;
    /// True when the node owns (and must free) its descriptor sets.
    bool ownsDescriptorSets = false;
    /// True when the node owns its descriptor set layouts.
    bool ownsDescriptorSetLayouts = false;
    /// True when this node dispatches a ray tracing pipeline.
    bool isRayTracing = false;
    /// Shader binding table regions, for ray tracing nodes.
    std::array<VkStridedDeviceAddressRegionKHR, 3> sbtRegions{};
    /// Extent the ray tracing trace call is issued over.
    VkExtent2D traceExtent = {0, 0};
    /// Queue the node was compiled for.
    QueueType queueType = QueueType::GRAPHICS;
    /// Bind point of the compiled pipeline.
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
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
    /// @return Mutable access to the compiled nodes, used by the compile step in `Build()`.
    std::vector<CompiledRenderGraphNode>& GetNodes() { return m_nodes; }
    /// @return Number of compiled nodes.
    std::size_t GetNodeCount() const { return m_nodes.size(); }

    /// Recompute the scheduling plan from the current nodes. Called by `RenderGraphBuilder::Build()`
    /// after the nodes are compiled.
    /// @param families Queue family indices used to decide which resources cross a queue family.
    void RebuildExecutionPlan(const RenderGraphQueueFamilies& families);
    /// @return The scheduling plan for the current nodes. Read-only during execution.
    const RenderGraphExecutionPlan& GetExecutionPlan() const { return m_executionPlan; }

    /// Destroy every node's GPU objects and clear the node list. Safe to call repeatedly.
    /// @param device Device the objects were created on.
    /// @param descriptorPool Pool the per-node descriptor sets were allocated from.
    void Destroy(VkDevice device, VkDescriptorPool descriptorPool);

private:
    std::vector<CompiledRenderGraphNode> m_nodes;
    RenderGraphExecutionPlan m_executionPlan;
};
}  // namespace Muyo::RenderGraph
