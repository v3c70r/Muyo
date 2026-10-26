#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>

#include "DescriptorHeapService.h"
#include "RenderGraphNodeResource.h"
#include "RenderResourceManager.h"
#include "VkExtFuncsLoader.h"

namespace Muyo::RenderGraph
{
/// Context handed to a node's execute callback.
///
/// The graph has already recorded the barriers, opened the render pass (graphics nodes) and bound
/// the pipeline and descriptor sets, so most callbacks only issue draw/dispatch/trace commands.
struct RenderGraphNodeContext
{
    QueueType queueType = QueueType::GRAPHICS;  ///< Queue this node is running on.
    RenderResourceManager& resourceManager;     ///< Allocates and owns graph-declared resources.
    /// Resolve a handle the way the graph does: its own transients first (whose memory belongs to an
    /// aliasing block and which are therefore deliberately not registered with the manager), then ones
    /// the caller imported, then the manager's. Set by whoever executes; a context built without it falls
    /// back to the manager alone.
    ///
    /// This is not decoration. A node callback reaches its resources through `GetResource`, and aliasing
    /// moved some of them outside the manager - so asking the manager got a null and the callback
    /// dereferenced it. Imported resources had the same hole before aliasing existed, for the same reason.
    std::function<const IRenderResource*(const ResourceHandle&)> resolveResource;
    // No mesh manager here. It was a reference the graph had to fill from the renderer's singleton for
    // every node, including compute ones, and no caller ever read it - a node callback that needs mesh
    // buffers captures them itself (consumer finding F5, #53). Carrying it made the graph require a
    // renderer-shaped service it has no use for.

    // GPU-side fields (valid only for GPU nodes)
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;                      ///< Command buffer to record into.
    VkPipeline pipeline = VK_NULL_HANDLE;                                ///< Bound pipeline.
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;                    ///< Bound pipeline layout.
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  ///< Bind point for the pipeline.
    /// Non-null for a node compiled with `useDescriptorHeap`; the heaps are bound before the pipeline.
    DescriptorHeapService* descriptorHeap = nullptr;
    /// The device's `maxPushDataSize`; zero for a legacy descriptor node.
    uint32_t maxPushDataSize = 0;

    /// Record opaque root data for this node. Offsets are host-defined; the graph only bounds them by
    /// the device's push-data limit. The pipeline has no layout, so this backs the shader's push block.
    /// @throws std::runtime_error when the node is not a heap node or the range exceeds the device limit.
    void PushData(uint32_t offset, std::span<const std::byte> bytes)
    {
        if (descriptorHeap == nullptr || VkExt::vkCmdPushDataEXT == nullptr)
        {
            throw std::runtime_error("RenderGraphNodeContext::PushData: the node is not a descriptor-heap node");
        }
        if (offset > maxPushDataSize || bytes.size() > maxPushDataSize - offset)
        {
            throw std::runtime_error("RenderGraphNodeContext::PushData: offset + size exceeds maxPushDataSize");
        }
        VkPushDataInfoEXT pushInfo{VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT};
        pushInfo.offset = offset;
        pushInfo.data.address = bytes.data();
        pushInfo.data.size = bytes.size();
        VkExt::vkCmdPushDataEXT(commandBuffer, &pushInfo);
    }

    /// Resolve a graph-declared resource to its concrete pointer (allocated at Build()).
    /// @tparam T Concrete resource type (e.g. `BufferResource`, `RenderTarget`).
    /// @param handle Resource name used in the node's `resourceUses`.
    /// @return The resource, or `nullptr` if it is not of type `T`.
    template <class T>
    T* GetResource(const ResourceHandle& handle) const
    {
        if (resolveResource)
        {
            // The graph's resolver answers for everything it can see, and falls back to the manager itself.
            // `T*` rather than `const T*` because callbacks that fill a resource need the pointer they got
            // before; nothing here owns a resource through a const reference.
            if (const IRenderResource* pResource = resolveResource(handle))
            {
                return const_cast<T*>(dynamic_cast<const T*>(pResource));
            }
        }
        return resourceManager.template GetResource<T>(handle);
    }
};

/// Callback a node provides to record its GPU or host work.
using RenderGraphNodeCallback = std::function<void(RenderGraphNodeContext&)>;
}  // namespace Muyo::RenderGraph
