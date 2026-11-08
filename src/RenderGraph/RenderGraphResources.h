#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <memory>
#include <string>

#include "RenderGraphNodeResource.h"

namespace Muyo::RenderGraph
{
/// @param physicalDevice Physical device to ask.
/// @param format         Image format.
/// @return True when the format supports an optimal-tiling depth/stencil attachment.
///
/// Device-level format facts used to be reachable only through the engine's resource manager, which is
/// one of the things that kept the graph on that header. They take the physical device as an argument so
/// the graph's own image paths and the consumer's agree on one implementation.
inline bool FormatSupportsOptimalTilingDepthAttachment(VkPhysicalDevice physicalDevice, VkFormat format)
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

/// @param physicalDevice Physical device to ask.
/// @param format         Image format.
/// @return True when the format supports an optimal-tiling colour attachment.
inline bool FormatSupportsOptimalTilingColorAttachment(VkPhysicalDevice physicalDevice, VkFormat format)
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0U;
}

/// The usage flags an image of this format is created with, before the caller's own are added.
///
/// A shared function rather than a line inside one allocator because two paths create images: the engine's
/// resource manager and the graph allocating a transient unbound so it can be bound into a shared block.
/// If those two disagreed about the usage flags, the same declaration would produce two different create
/// infos - which is exactly what the graph's aliasing classes assume cannot happen, since they key an
/// image on its create info.
///
/// @param physicalDevice        Physical device the format is queried against.
/// @param format                Image format.
/// @param additionalUsageFlags  Usage flags the caller asked for.
/// @return The flags to create the image with.
inline VkImageUsageFlags EffectiveImageUsageFlags(VkPhysicalDevice physicalDevice, VkFormat format,
                                                  VkImageUsageFlags additionalUsageFlags)
{
    const bool isColorAttachment = FormatSupportsOptimalTilingColorAttachment(physicalDevice, format) &&
                                   !FormatSupportsOptimalTilingDepthAttachment(physicalDevice, format);
    return additionalUsageFlags |
           (isColorAttachment ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
                              : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

/// An opaque handle to memory the injected allocator owns.
///
/// The graph plans aliasing - which resources share a block and at what offsets - but it does not own the
/// memory. A block therefore crosses the boundary as this handle rather than as a VMA type, and the concrete
/// allocator is the only thing that interprets `pImpl`. The rest of `IGraphResourceAllocator` still speaks
/// `VmaMemoryUsage`, so an implementation does include `vk_mem_alloc.h` today; the block handle keeps the
/// *allocation* opaque while that VMA vocabulary remains part of the stated surface.
struct GraphMemoryBlock
{
    void* pImpl = nullptr;  ///< Allocator-private identity; null means "no block".
    /// @return True when this handle names no block.
    bool IsNull() const { return pImpl == nullptr; }
};

/// The one thing every resource answers: what kind of Vulkan object it is.
///
/// The graph's barrier, descriptor and binding paths switch on `GetResourceKind()` instead of casting to
/// a concrete engine class. The kind is a property of the resource rather than of its C++ type, which is
/// what lets an imported engine resource and a graph-owned transient be handled by the same code.
class IGraphResource
{
public:
    /// Destroy the resource. A consumer-owned resource is destroyed by its owner, not by the graph.
    virtual ~IGraphResource() = default;
    /// @return The concrete Vulkan object kind behind this resource.
    virtual ResourceKind GetResourceKind() const = 0;
    /// @return The Vulkan object type, for debug labels and validation messages.
    virtual VkObjectType GetVkObjectType() const = 0;
    /// @param name Debug name to attach to the Vulkan object.
    virtual void SetDebugName(const std::string& name) const = 0;
};

/// A buffer the graph can address, look at and map without knowing whose class it is.
///
/// Implemented by the graph's own transients, by the device's VMA-backed resources
/// (`VkGraphResourceAllocator`) and by the engine's `BufferResource`, so an imported buffer and a
/// graph-allocated one are the same thing to the barrier, descriptor and binding paths. This is what
/// replaces the graph's old cast to the engine's concrete buffer class: the graph asks a resource what
/// it is instead of asking whether it is one particular engine class.
class IGraphBuffer : public IGraphResource
{
public:
    /// @return The Vulkan buffer handle, or `VK_NULL_HANDLE` before one has been adopted.
    virtual VkBuffer buffer() const = 0;
    /// @return The declared size in bytes.
    virtual size_t GetSize() const = 0;
    /// @return The allocated capacity in bytes, which is at least `GetSize()`.
    virtual size_t GetCapacity() const = 0;
    /// @return The Vulkan usage flags the buffer was created with.
    virtual VkBufferUsageFlags GetBufferUsage() const = 0;
    /// @return The memory usage the buffer was created with.
    virtual VmaMemoryUsage GetMemoryUsage() const = 0;
    /// Map this resource's memory on the host.
    /// @return A host pointer to the start of its storage, or nullptr when it has none.
    virtual void* Map() = 0;
    /// Release a mapping `Map` took.
    virtual void Unmap() = 0;
    /// Upload content into this buffer.
    /// @param pData Source bytes, or `nullptr` to size the buffer without uploading.
    /// @param size  Byte count, at most the storage the resource has.
    virtual void SetData(const void* pData, size_t size) = 0;
};

/// An image the graph can barrier, describe and bind without knowing whose class it is.
///
/// The image counterpart of `IGraphBuffer`, and the reason the graph no longer casts to the engine's
/// `ImageResource`: an imported swapchain image, an engine render target and a graph transient all answer
/// these questions.
class IGraphImage : public IGraphResource
{
public:
    /// @return The Vulkan image handle.
    virtual VkImage getImage() const = 0;
    /// @return The image's view.
    virtual VkImageView getView() const = 0;
    /// @return The create-info used for the view, for descriptor-heap writes that need its description.
    virtual const VkImageViewCreateInfo& GetImageViewCreateInfo() const = 0;
    /// @return The image's format.
    virtual VkFormat GetImageFormat() const = 0;
    /// @return The image's extent.
    virtual VkExtent2D GetImageExtent() const = 0;
    /// @return The usage flags the image was created with.
    virtual VkImageUsageFlags GetImageUsage() const = 0;
    /// @return The created mip level count.
    virtual uint32_t GetImageMipCount() const = 0;
    /// @return The created array layer count.
    virtual uint32_t GetImageLayerCount() const = 0;
};

/// An acceleration structure the graph can bind and barrier.
///
/// A `vkCmdPipelineBarrier2` has no barrier type for an acceleration structure, so a queue-family handover
/// is expressed as a buffer barrier on `GetBackingBuffer()`. That is why the interface exposes the backing
/// buffer and not only the structure handle.
class IGraphAccelerationStructure : public IGraphResource
{
public:
    /// @return The structure handle.
    virtual const VkAccelerationStructureKHR& GetAccelerationStructure() const = 0;
    /// @return The device address used to write the structure into a descriptor heap.
    virtual VkDeviceAddress GetDeviceAddress() const = 0;
    /// @return The buffer the structure's memory lives in.
    virtual VkBuffer GetBackingBuffer() const = 0;
};

/// Which allocation pool a buffer belongs to.
///
/// A graph-owned enum rather than the device allocator's `PoolType`, so the graph can ask for the
/// shader-binding-table alignment without including a device header. VMA's SBT pool exists because a
/// shader binding table's device address must meet `shaderGroupBaseAlignment`; the caller states the
/// intent and the implementation maps it.
enum class GraphBufferPool : uint8_t
{
    Default,            ///< VMA's default allocation path.
    ShaderBindingTable  ///< Aligned for ray-tracing shader groups.
};

/// What the graph needs from a resource allocator, and nothing more.
///
/// The graph owns the **policy** - transient lifetimes, aliasing, which resource is reused - and receives
/// the **mechanism** as this interface. `muyo_device`'s `VkGraphResourceAllocator` is the VMA-backed
/// implementation; a consumer with its own memory manager implements the same interface instead
/// (`docs/RenderGraph-Library-Design.md` section 4).
///
/// Every method is on the allocator rather than on the resource because the graph's transients create a
/// resource *unbound*, query its requirements, decide the aliasing offsets and only then bind it. A helper
/// that allocated resource and memory together could not do that.
class IGraphResourceAllocator
{
public:
    /// Destroy the allocator. Resources it created must already be destroyed.
    virtual ~IGraphResourceAllocator() = default;

    // ── Owned resources ──────────────────────────────────────────────────────
    /// Create a buffer and the memory it owns.
    /// @param size        Buffer size in bytes.
    /// @param usage       Vulkan buffer usage flags.
    /// @param memoryUsage Intended memory usage.
    /// @param debugName   Name used for the Vulkan debug label; may be empty.
    /// @param pool        Allocation pool the buffer needs.
    /// @return The buffer, owned by the caller.
    virtual std::unique_ptr<IGraphBuffer> CreateBuffer(size_t size, VkBufferUsageFlags usage,
                                                       VmaMemoryUsage memoryUsage, const std::string& debugName,
                                                       GraphBufferPool pool = GraphBufferPool::Default) = 0;
    /// Create an image and the memory it owns.
    /// @param imageInfo   Image create info, without a memory binding.
    /// @param memoryUsage Intended memory usage.
    /// @param debugName   Name used for the Vulkan debug label; may be empty.
    /// @return The image, owned by the caller.
    virtual std::unique_ptr<IGraphImage> CreateImage(const VkImageCreateInfo& imageInfo, VmaMemoryUsage memoryUsage,
                                                     const std::string& debugName) = 0;

    // ── Aliasing ─────────────────────────────────────────────────────────────
    /// Create a buffer with no memory bound to it.
    /// @param size  Buffer size in bytes.
    /// @param usage Vulkan buffer usage flags.
    /// @return The unbound buffer. Destroy it with `DestroyUnboundBuffer`.
    virtual VkBuffer CreateUnboundBuffer(VkDeviceSize size, VkBufferUsageFlags usage) = 0;
    /// Destroy a buffer created by `CreateUnboundBuffer`; its memory belongs to a block.
    /// @param buffer Buffer to destroy.
    virtual void DestroyUnboundBuffer(VkBuffer buffer) = 0;
    /// @param buffer A buffer created by `CreateUnboundBuffer`.
    /// @return Its memory requirements.
    virtual VkMemoryRequirements QueryBufferRequirements(VkBuffer buffer) const = 0;
    /// Create an image with no memory bound to it.
    /// @param imageInfo Image create info.
    /// @return The unbound image. Destroy it with `DestroyUnboundImage`.
    virtual VkImage CreateUnboundImage(const VkImageCreateInfo& imageInfo) = 0;
    /// Destroy an image created by `CreateUnboundImage`; its memory belongs to a block.
    /// @param image Image to destroy.
    virtual void DestroyUnboundImage(VkImage image) = 0;
    /// @param image An image created by `CreateUnboundImage`.
    /// @return Its memory requirements.
    virtual VkMemoryRequirements QueryImageRequirements(VkImage image) const = 0;
    /// Allocate memory with no resource bound to it.
    /// @param requirements Combined requirements of everything that will alias this block.
    /// @param memoryUsage  Intended usage of the block.
    /// @return The block. Free it with `FreeAliasingBlock` once everything bound to it is destroyed.
    virtual GraphMemoryBlock AllocateAliasingBlock(const VkMemoryRequirements& requirements,
                                                   VmaMemoryUsage memoryUsage) = 0;
    /// Bind an already-created buffer to a byte offset of an aliasing block.
    /// @param buffer Buffer to bind.
    /// @param block  The block.
    /// @param offset Byte offset inside the block.
    virtual void BindBufferToBlock(VkBuffer buffer, GraphMemoryBlock block, VkDeviceSize offset) = 0;
    /// Bind an already-created image to a byte offset of an aliasing block.
    /// @param image  Image to bind.
    /// @param block  The block.
    /// @param offset Byte offset inside the block.
    virtual void BindImageToBlock(VkImage image, GraphMemoryBlock block, VkDeviceSize offset) = 0;
    /// Free an aliasing block. Every resource bound to it must already be destroyed.
    /// @param block The block to free.
    virtual void FreeAliasingBlock(GraphMemoryBlock block) = 0;

    // ── Owned memory for the graph's root arenas ─────────────────────────────
    /// Allocate a buffer and the memory it owns, through out-parameters.
    /// @param size        Size in bytes.
    /// @param usage       Vulkan buffer usage flags.
    /// @param memoryUsage Intended memory usage.
    /// @param[out] buffer Receives the buffer.
    /// @param[out] memory Receives the block owning its memory.
    virtual void AllocateBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkBuffer& buffer,
                                GraphMemoryBlock& memory) = 0;
    /// Free a buffer and its memory.
    /// @param[in,out] buffer Buffer to destroy; left as a destroyed handle on return.
    /// @param[in,out] memory Memory to free; left null on return.
    virtual void FreeBuffer(VkBuffer& buffer, GraphMemoryBlock& memory) = 0;
    /// @param block  Block to map.
    /// @param offset Byte offset inside the block; zero maps its start.
    /// @return A host pointer to the requested bytes.
    virtual void* MapMemory(GraphMemoryBlock block, VkDeviceSize offset = 0) = 0;
    /// Release a mapping taken by `MapMemory`.
    /// @param block The mapped block.
    virtual void UnmapMemory(GraphMemoryBlock block) = 0;

    // ── Views ────────────────────────────────────────────────────────────────
    /// Create an image view.
    /// @param createInfo View create info.
    /// @return The view.
    virtual VkImageView CreateImageView(const VkImageViewCreateInfo& createInfo) = 0;
    /// Destroy a view created by `CreateImageView`.
    /// @param view View to destroy.
    virtual void DestroyImageView(VkImageView view) = 0;
};
}  // namespace Muyo::RenderGraph
