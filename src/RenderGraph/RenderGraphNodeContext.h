#pragma once
#include <vulkan/vulkan.h>

#include <functional>

#include "MeshResourceManager.h"
#include "RenderGraphNodeResource.h"
#include "RenderResourceManager.h"

namespace Muyo::RenderGraph
{
/// Context handed to a node's execute callback.
///
/// The graph has already recorded the barriers, opened the render pass (graphics nodes) and bound
/// the pipeline and descriptor sets, so most callbacks only issue draw/dispatch/trace commands.
struct RenderGraphNodeContext
{
    QueueType queueType = QueueType::GRAPHICS;  ///< Queue this node is running on.
    RenderResourceManager& resourceManager;     ///< Global resource manager (graph-owned resources).
    MeshResourceManager& meshManager;           ///< Mesh manager (shared vertex/index buffers).

    // GPU-side fields (valid only for GPU nodes)
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;                      ///< Command buffer to record into.
    VkPipeline pipeline = VK_NULL_HANDLE;                                ///< Bound pipeline.
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;                    ///< Bound pipeline layout.
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  ///< Bind point for the pipeline.

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
}  // namespace Muyo::RenderGraph
