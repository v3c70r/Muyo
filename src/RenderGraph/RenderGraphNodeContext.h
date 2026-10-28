#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>

#include "DescriptorHeapService.h"
#include "RenderGraphNodeResource.h"
#include "RenderResourceManager.h"
#include "VkExtFuncsLoader.h"

namespace Muyo::RenderGraph
{
/// Host-writable, device-addressable root slice for one node/execution.
struct RenderGraphRootSlice
{
    std::byte* host = nullptr;          ///< Host mapping of the slice.
    VkDeviceAddress deviceAddress = 0;  ///< Device address of the same bytes.
    size_t size = 0;                    ///< Slice size in bytes.
};

/// Context handed to a node's execute callback.
///
/// The graph has already recorded the barriers, opened the render pass (graphics nodes) and bound
/// the pipeline and heaps, so most callbacks only issue draw/dispatch/trace commands.
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
    VkPipelineBindPoint bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  ///< Bind point for the pipeline.
    /// Allocator for optional root memory. B1b-4 gives each execution a host-visible, device-addressable
    /// arena; the host owns the layout and the bytes.
    std::function<RenderGraphRootSlice(size_t, size_t)> allocateRoot;
    /// Resolve a graph-declared resource to its descriptor-heap index for this node, if the service
    /// registered the use.
    std::function<std::optional<uint32_t>(const ResourceHandle&, ResourceUsage)> getHeapIndex;

    /// Non-null for every GPU node. The executor uploads and binds the heaps once per segment command
    /// buffer, before any render pass starts (B1b-5b-4).
    DescriptorHeapService* descriptorHeap = nullptr;
    /// The device's `maxPushDataSize`; zero for a CPU node.
    uint32_t maxPushDataSize = 0;
    /// Ranges pushed for the current node, for the builder's record-time coverage check.
    std::vector<std::pair<uint32_t, uint32_t>> pushedRanges;

    /// Allocate root memory for this node from the execution's arena.
    /// @throws std::runtime_error when the node has no root allocator or the arena is exhausted.
    RenderGraphRootSlice AllocateRoot(size_t size, size_t alignment)
    {
        if (!allocateRoot) throw std::runtime_error("RenderGraphNodeContext::AllocateRoot: no root arena");
        return allocateRoot(size, alignment);
    }

    /// Write a 32-bit heap index into a root slice.
    /// @throws std::runtime_error when the write would leave the slice.
    void WriteRootIndex(RenderGraphRootSlice& slice, size_t offset, uint32_t index)
    {
        if (offset > slice.size || sizeof(uint32_t) > slice.size - offset)
        {
            throw std::runtime_error("RenderGraphNodeContext::WriteRootIndex: write leaves the root slice");
        }
        std::memcpy(slice.host + offset, &index, sizeof(index));
    }

    /// Make host root writes visible to this command buffer's shader reads.
    void FlushRoot()
    {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    }

    /// @return The heap index for a use, if this node registered it.
    std::optional<uint32_t> GetHeapIndex(const ResourceHandle& handle, ResourceUsage usage) const
    {
        if (!getHeapIndex) return std::nullopt;
        return getHeapIndex(handle, usage);
    }

    /// Record opaque root data for this node. Offsets are host-defined; the graph only bounds them by
    /// the device's push-data limit. The pipeline has no layout, so this backs the shader's push block.
    /// @throws std::runtime_error when the node is not a heap node or the range exceeds the device limit.
    void PushData(uint32_t offset, std::span<const std::byte> bytes)
    {
        if (descriptorHeap == nullptr)
        {
            throw std::runtime_error("RenderGraphNodeContext::PushData: the node is not a descriptor-heap node");
        }
        if (offset > maxPushDataSize || bytes.size() > maxPushDataSize - offset)
        {
            throw std::runtime_error("RenderGraphNodeContext::PushData: offset + size exceeds maxPushDataSize");
        }
        if (VkExt::vkCmdPushDataEXT == nullptr)
        {
            throw std::runtime_error("RenderGraphNodeContext::PushData: vkCmdPushDataEXT is not loaded");
        }
        VkPushDataInfoEXT pushInfo{VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT};
        pushInfo.offset = offset;
        pushInfo.data.address = bytes.data();
        pushInfo.data.size = bytes.size();
        VkExt::vkCmdPushDataEXT(commandBuffer, &pushInfo);
        pushedRanges.emplace_back(offset, offset + static_cast<uint32_t>(bytes.size()));
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
