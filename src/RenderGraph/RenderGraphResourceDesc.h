#pragma once
#include <variant>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

#include "RenderGraphResourceHandle.h"
#include "RenderResource.h"
#include "RenderResourceManager.h"

namespace Muyo::RenderGraph
{
/// How long a resource lives relative to the graph.
enum class ResourceLifetime : uint8_t
{
    Transient,   ///< Lives only inside this graph; may be aliased / reused across nodes.
    Persistent,  ///< Survives the frame boundary (e.g. TAA history, accumulation buffers).
    Imported     ///< Externally owned (old pass system, swapchain images); graph does not allocate.
};

/// Describes the allocation of a buffer. Size = count * stride.
struct BufferResourceDesc
{
    uint64_t count = 0;                                           ///< Number of elements.
    uint64_t stride = 0;                                          ///< Size of one element in bytes.
    VkBufferUsageFlags usage = 0;                                 ///< Vulkan buffer usage flags.
    VmaMemoryUsage memoryProperties = VMA_MEMORY_USAGE_UNKNOWN;   ///< VMA memory usage.
    ResourceLifetime lifetime = ResourceLifetime::Transient;      ///< Lifetime relative to the graph.
};

/// Describes the allocation of an image (render target / storage image / texture).
struct ImageResourceDesc
{
    VkFormat format = VK_FORMAT_UNDEFINED;                       ///< Pixel format.
    VkExtent2D extent = {0, 0};                                  ///< Width x height.
    uint32_t mips = 1;                                           ///< Mip level count.
    uint32_t layers = 1;                                         ///< Array layer count (6 = cubemap).
    VkImageUsageFlags usage = 0;                                 ///< Vulkan image usage flags.
    ResourceLifetime lifetime = ResourceLifetime::Transient;     ///< Lifetime relative to the graph.
};

/// The complete set of resource types the graph knows how to allocate.
using ResourceDesc = std::variant<BufferResourceDesc, ImageResourceDesc>;

/// Allocate a physical resource from a desc. The render graph calls this at Build() time.
///
/// Uses the existing RenderResourceManager as the backend allocator and returns the existing
/// resource if the handle is already allocated, so repeated builds reuse it.
///
/// @param desc                  Allocation description (buffer or image).
/// @param renderResourceManager Backend allocator.
/// @param handle                Name to allocate under.
/// @return The allocated (or pre-existing) resource.
inline IRenderResource* AllocateResource(const ResourceDesc& desc, RenderResourceManager& renderResourceManager,
                                         const ResourceHandle& handle)
{
    if (IRenderResource* existing = renderResourceManager.GetResource<IRenderResource>(handle))
    {
        return existing;
    }

    return std::visit(
        [&](const auto& d) -> IRenderResource*
        {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, BufferResourceDesc>)
            {
                return renderResourceManager.AllocateBuffer(handle, d.count * d.stride, d.usage, d.memoryProperties);
            }
            else
            {
                return renderResourceManager.GetRenderTarget(handle, d.extent, d.format, d.mips, d.layers, d.usage);
            }
        },
        desc);
}

}  // namespace Muyo::RenderGraph
