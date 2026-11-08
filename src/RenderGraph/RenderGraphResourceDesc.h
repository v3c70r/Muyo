#pragma once
#include <vulkan/vulkan.h>

#include <memory>
#include <variant>

#include "RenderGraphResourceHandle.h"
#include "RenderGraphResources.h"

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
    uint64_t count = 0;                                          ///< Number of elements.
    uint64_t stride = 0;                                         ///< Size of one element in bytes.
    VkBufferUsageFlags usage = 0;                                ///< Vulkan buffer usage flags.
    VmaMemoryUsage memoryProperties = VMA_MEMORY_USAGE_UNKNOWN;  ///< VMA memory usage.
    ResourceLifetime lifetime = ResourceLifetime::Transient;     ///< Lifetime relative to the graph.
};

/// Describes the allocation of an image (render target / storage image / texture).
struct ImageResourceDesc
{
    VkFormat format = VK_FORMAT_UNDEFINED;                    ///< Pixel format.
    VkExtent2D extent = {0, 0};                               ///< Width x height.
    uint32_t mips = 1;                                        ///< Mip level count.
    uint32_t layers = 1;                                      ///< Array layer count (6 = cubemap).
    VkImageUsageFlags usage = 0;                              ///< Vulkan image usage flags.
    ResourceLifetime lifetime = ResourceLifetime::Transient;  ///< Lifetime relative to the graph.
};

/// The complete set of resource types the graph knows how to allocate.
using ResourceDesc = std::variant<BufferResourceDesc, ImageResourceDesc>;

/// @param physicalDevice Physical device the format is queried against.
/// @param desc           Image declaration.
/// @return The create info an image of `desc` is created with.
///
/// One place builds it, so the graph's persistent and transient paths cannot disagree about usage flags,
/// extent, mip count or layer count - a disagreement the aliasing classes key as two different images.
inline VkImageCreateInfo MakeImageCreateInfo(VkPhysicalDevice physicalDevice, const ImageResourceDesc& desc)
{
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = desc.format;
    imageInfo.extent = {desc.extent.width, desc.extent.height, 1};
    imageInfo.mipLevels = desc.mips;
    imageInfo.arrayLayers = desc.layers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = EffectiveImageUsageFlags(physicalDevice, desc.format, desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return imageInfo;
}

/// @param physicalDevice Physical device the image format is queried against.
/// @param resource       Existing resource to test.
/// @param desc           Declaration from this Build().
/// @return True when `resource` already has the characteristics `desc` declares, so Build() may reuse it.
///
/// This is the descriptor-keyed half of the reuse rule (#209): a changed extent, format, usage or a size
/// that no longer fits is a mismatch and the resource is recreated; a repeated Build() with the same
/// declaration reuses the same object.
///
/// Images compare the full create info the allocator would use, including the attachment bits
/// `EffectiveImageUsageFlags` derives from the format. Buffers compare usage, memory usage and
/// **capacity**: a host write that grows a buffer's size within its capacity is not a declaration
/// change, so it must not force a recreation.
inline bool ResourceMatchesDesc(VkPhysicalDevice physicalDevice, const IGraphResource& resource,
                                const ResourceDesc& desc)
{
    return std::visit(
        [&](const auto& d) -> bool
        {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, BufferResourceDesc>)
            {
                if (resource.GetResourceKind() != ResourceKind::BUFFER) return false;
                const auto& graphBuffer = static_cast<const IGraphBuffer&>(resource);
                return graphBuffer.GetBufferUsage() == d.usage && graphBuffer.GetMemoryUsage() == d.memoryProperties &&
                       graphBuffer.GetCapacity() == d.count * d.stride;
            }
            else
            {
                if (resource.GetResourceKind() != ResourceKind::IMAGE) return false;
                const auto& graphImage = static_cast<const IGraphImage&>(resource);
                const VkExtent2D extent = graphImage.GetImageExtent();
                return graphImage.GetImageFormat() == d.format && extent.width == d.extent.width &&
                       extent.height == d.extent.height && graphImage.GetImageMipCount() == d.mips &&
                       graphImage.GetImageLayerCount() == d.layers &&
                       // The persistent path adds SAMPLED_BIT to every image it creates, so an unchanged
                       // declaration must expect that bit too or the matcher would recreate on every
                       // Build (masked in Debug by the allocator reusing the same address; the
                       // sanitizer's quarantine exposed it).
                       graphImage.GetImageUsage() ==
                           (EffectiveImageUsageFlags(physicalDevice, d.format, d.usage) | VK_IMAGE_USAGE_SAMPLED_BIT);
            }
        },
        desc);
}

/// Allocate a physical resource from a desc through the injected allocator.
///
/// The graph does not know whether the result is VMA-backed, and that is the point of #272: the concrete
/// type is the allocator's (`VkGraphResourceAllocator` in `muyo_device`), and the graph holds it through
/// `IGraphResource`.
///
/// @param desc           Allocation description (buffer or image).
/// @param allocator      Allocator the graph was given.
/// @param physicalDevice Physical device the image format is queried against.
/// @param handle         Name to allocate under; used as the debug name.
/// @return The allocated resource.
inline std::unique_ptr<IGraphResource> AllocateResource(const ResourceDesc& desc, IGraphResourceAllocator& allocator,
                                                        VkPhysicalDevice physicalDevice, const ResourceHandle& handle)
{
    return std::visit(
        [&](const auto& d) -> std::unique_ptr<IGraphResource>
        {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, BufferResourceDesc>)
            {
                return allocator.CreateBuffer(d.count * d.stride, d.usage, d.memoryProperties, handle);
            }
            else
            {
                VkImageCreateInfo imageInfo = MakeImageCreateInfo(physicalDevice, d);
                // Persistent graph-owned images are addressable through a sampled descriptor as well as
                // their declared roles, matching what the engine resource manager used to create.
                imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
                return allocator.CreateImage(imageInfo, VMA_MEMORY_USAGE_GPU_ONLY, handle);
            }
        },
        desc);
}
}  // namespace Muyo::RenderGraph
