#pragma once
#include <cstring>
#include <stdexcept>
#include <string>

#include "RenderGraphResourceDesc.h"
#include "RenderResourceManager.h"

namespace Muyo::RenderGraph
{
/// A transient buffer whose memory belongs to an aliasing block rather than to the buffer.
///
/// The graph creates these with `VkMemoryAllocator::CreateUnboundBuffer` and binds them into a block
/// itself, so the buffer and its memory have different owners. `BufferResource` already expresses that
/// shape, and it is worth spelling out why nothing is overridden: the constructor that takes no size
/// allocates nothing, the handles are protected, and the inherited destructor calls
/// `FreeBuffer(buffer, allocation)` - which with a null allocation is
/// `vmaDestroyBuffer(allocator, buffer, VK_NULL_HANDLE)`: it destroys the buffer and frees no memory. The
/// block's owner frees the memory, after every resource bound into it is gone.
///
/// It stays a `BufferResource` on purpose. The graph's barrier path and its descriptor path both
/// identify resources with `dynamic_cast<const BufferResource*>`; an aliased buffer that were merely
/// some `IRenderResource` would be silently skipped by both, which is the kind of failure that shows up
/// as a wrong barrier rather than as an error.
///
/// One consequence worth knowing before it surprises someone: this resource cannot be mapped through
/// `BufferResource::Map`, which maps the resource's own allocation and there is none. A caller that needs
/// the bytes on the host copies out of it, which is what the readback cases in the suite do. A mapping
/// accessor for graph-owned transients is a follow-up rather than an omission.
class TransientBufferResource : public BufferResource
{
public:
    /// @param nSize         Buffer size in bytes. The buffer itself is created by `Adopt`, not here.
    /// @param nUsage        Vulkan buffer usage flags.
    /// @param nMemoryUsage  The declared memory usage, which decides whether `SetData` can write through
    ///                      a host mapping or has to travel by transfer.
    TransientBufferResource(size_t nSize, VkBufferUsageFlags nUsage, VmaMemoryUsage nMemoryUsage)
        : BufferResource(nUsage, nMemoryUsage), m_nAliasedSize(nSize)
    {
        // The base's own size field is what `GetSize` reports and what `SetData` compares against; for a
        // transient it is never used to allocate, but leaving it zero would make the resource report a
        // size of zero to anything that asks.
        m_nSize = static_cast<uint32_t>(nSize);
    }

    /// Take ownership of the unbound buffer the allocator created for this resource.
    /// @param buffer Buffer this resource owns, binds into a block, and destroys - without freeing its
    ///               memory, which the block owns.
    void Adopt(VkBuffer buffer) { m_buffer = buffer; }

    /// Record the block and offset this resource was bound at, so a host mapping can find its bytes.
    /// @param block  The block the buffer is bound into. Not owned, and outlived by nothing: the graph
    ///               destroys every resource before it frees any block.
    /// @param nOffset Byte offset of this buffer inside that block.
    void BindTo(VmaAllocation block, VkDeviceSize nOffset)
    {
        m_block = block;
        m_nOffset = nOffset;
    }

    /// Map this resource's bytes on the host.
    ///
    /// The allocation to map is the *block*, not the resource - an aliased buffer has no allocation of its
    /// own, which is why `BufferResource::Map` cannot serve it. The returned pointer is the block's mapping
    /// plus this resource's offset. Idempotent: mapping an already-mapped resource returns the same pointer
    /// rather than taking a second reference on the block, which is shared with every other member of the
    /// slot.
    /// @return A host pointer to the start of this resource's storage, or nullptr when it is not bound yet.
    void* Map() override
    {
        if (m_block == VK_NULL_HANDLE) return nullptr;
        if (m_pMapped != nullptr) return m_pMapped;
        void* pBlockMemory = nullptr;
        GetMemoryAllocator()->MapBuffer(m_block, &pBlockMemory);
        m_pMapped = static_cast<uint8_t*>(pBlockMemory) + m_nOffset;
        return m_pMapped;
    }
    /// Release the mapping `Map` took. A resource that is not mapped is left alone, so a path that unmaps
    /// defensively cannot release the block out from under a member that still holds a pointer into it.
    void Unmap() override
    {
        if (m_pMapped == nullptr) return;
        GetMemoryAllocator()->UnmapBuffer(m_block);
        m_pMapped = nullptr;
    }
    /// Upload content into this buffer.
    ///
    /// The storage is the block's, so there is nothing to allocate - and nothing to grow: the plan
    /// reserved this resource's size and the block is exactly as large as the plan said. A larger upload is
    /// a declaration error rather than a silent reallocation, which is why it is named here.
    ///
    /// With the size it already has and a non-null buffer, the base implementation's first two branches
    /// are both no-ops and only the staging copy runs, which is the part that belongs to a resource. That
    /// is the whole override.
    /// @param pData Source bytes.
    /// @param size  Byte count, at most the size this resource was planned at.
    void SetData(const void* pData, size_t size) override
    {
        if (size > m_nAliasedSize)
        {
            throw std::runtime_error("Transient buffer of " + std::to_string(m_nAliasedSize) + " bytes cannot take a " +
                                     std::to_string(size) +
                                     " byte upload: its storage is a shared block sized by the build plan, "
                                     "and it is not grown in place.");
        }
        if (pData == nullptr || size == 0) return;

        // Host-visible storage is written through the block's mapping, which is the only mapping an
        // aliased buffer has. The base class's own path for a buffer without TRANSFER_DST maps the
        // resource's allocation, and there is none - which is what a `PreViewData`-shaped transient hits.
        if (MEMORY_USAGE == VMA_MEMORY_USAGE_CPU_TO_GPU || MEMORY_USAGE == VMA_MEMORY_USAGE_CPU_ONLY)
        {
            std::memcpy(Map(), pData, size);
            Unmap();
            return;
        }

        // Device-local storage has to travel by transfer, and that needs the declaration to say so.
        if ((BUFFER_USAGE & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0)
        {
            throw std::runtime_error(
                "Transient buffer declares no TRANSFER_DST and is not host-visible, so "
                "nothing can write its content. Declare TRANSFER_DST, or a memory "
                "property that allows mapping.");
        }
        BufferResource::SetData(pData, size);
    }

    /// @return True while this resource holds a host mapping. The graph's contract is that a mapping does
    ///         not outlive the node whose use declared the resource, because that is what makes its slot
    ///         exclusively its own for as long as the pointer is held.
    bool IsMapped() const { return m_pMapped != nullptr; }

    /// @return The size the buffer was created with, which is what the block's plan reserved for it.
    size_t GetAliasedSize() const { return m_nAliasedSize; }

    /// Unmap before the base destructor destroys the buffer. The block is freed after every resource
    /// bound into it is gone, so this runs while the mapping is still valid - which is the whole reason the
    /// graph's teardown order is resources first, blocks second.
    // Qualified: this is the override this class defines, and naming it makes the dispatch explicit. The
    // analyzer flags any virtual call in a destructor, correctly in general - here the call is this class's
    // own Unmap, not a further-derived override that no longer exists.
    ~TransientBufferResource() override { TransientBufferResource::Unmap(); }

private:
    /// The block this resource was bound into, or null before `BindTo`.
    VmaAllocation m_block = VK_NULL_HANDLE;
    /// This resource's byte offset inside that block.
    VkDeviceSize m_nOffset = 0;
    /// The pointer `Map` returned, or null when unmapped. Kept so `Map`/`Unmap` are symmetric: VMA counts
    /// mappings per allocation, and the allocation here is shared with the other members of the slot.
    uint8_t* m_pMapped = nullptr;
    /// The size this resource was planned at. Kept separately from `m_nSize` because that one is the
    /// base class's, and a reader asking how large this buffer is should not have to know which path
    /// created it.
    size_t m_nAliasedSize = 0;
};

/// A transient image whose memory belongs to an aliasing block rather than to the image.
///
/// Derives from `ImageResource` rather than from `TextureResource`: the graph's barrier, descriptor and
/// binding paths all identify images with `dynamic_cast<const ImageResource*>`, so that is the whole
/// requirement, and `TextureResource` adds a sampler and pixel loading that a render-graph target has no
/// use for.
///
/// The memory half is the same trick as the buffer's. `m_allocation` stays null, so the inherited
/// destructor's `FreeImage(image, VK_NULL_HANDLE)` is `vmaDestroyImage(allocator, image,
/// VK_NULL_HANDLE)` - it destroys the image and frees no memory, which is right because the block owns
/// the memory and the graph frees the block after every image bound into it is gone. The **view** is not
/// aliased: a view belongs to one resource and is created and destroyed as usual.
class TransientImageResource : public ImageResource
{
public:
    /// @param desc  The image declaration, which is also its aliasing class.
    /// @param usage The usage flags to create it with - `EffectiveImageUsageFlags` of the declaration, so
    ///              that this path and the resource manager's cannot disagree about what an image is.
    TransientImageResource(const ImageResourceDesc& desc, VkImageUsageFlags usage)
    {
        m_imageInfo.format = desc.format;
        m_imageInfo.extent = {desc.extent.width, desc.extent.height, 1};
        m_imageInfo.mipLevels = desc.mips;
        m_imageInfo.arrayLayers = desc.layers;
        m_imageInfo.usage = usage;

        // The view's subresource range, which the inherited creator does not fill in. A depth format needs
        // the depth aspect; everything else is colour, which is the same split the usage flags use.
        m_imageViewInfo.subresourceRange.aspectMask = FormatSupportsOptimalTilingDepthAttachment(desc.format)
                                                          ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                          : VK_IMAGE_ASPECT_COLOR_BIT;
        m_imageViewInfo.subresourceRange.baseMipLevel = 0;
        m_imageViewInfo.subresourceRange.levelCount = desc.mips;
        m_imageViewInfo.subresourceRange.baseArrayLayer = 0;
        m_imageViewInfo.subresourceRange.layerCount = desc.layers;
    }

    /// @return The create info this image must be created with, which the caller needs because the
    ///         allocator creates it before the resource can own it.
    const VkImageCreateInfo& ImageInfoForCreate() const { return m_imageInfo; }

    /// Take ownership of the unbound image the allocator created, and create its view.
    /// @param image Image this resource owns, binds into a block, and destroys - without freeing its
    ///              memory, which the block owns. The view is created from it here because a view cannot
    ///              exist before its image does.
    void Adopt(VkImage image) { m_image = image; }

    /// Create this image's view.
    ///
    /// Called after the image is bound into its block, not when it is adopted, because a view cannot be
    /// created for an image with no memory bound - the validation layer says so in as many words
    /// (`VUID-VkImageViewCreateInfo-image-01020`: "used with no memory bound"). The order is therefore
    /// create unbound, query, plan, bind, view.
    void CreateView() { CreateImageViewInternal(); }
};
}  // namespace Muyo::RenderGraph
