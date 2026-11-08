#pragma once
#include <vulkan/vulkan.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include "Debug.h"
#include "RenderGraphResourceDesc.h"
#include "VkRenderDevice.h"

namespace Muyo::RenderGraph
{
/// A transient buffer whose memory belongs to an aliasing block rather than to the buffer.
///
/// The graph creates these with `IGraphResourceAllocator::CreateUnboundBuffer` and binds them into a
/// block itself, so the buffer and its memory have different owners. The resource implements the
/// graph-owned `IGraphBuffer` interface, which is what the barrier and descriptor paths identify it by -
/// no engine class is involved, so an aliased transient cannot be silently skipped for being "not a
/// `BufferResource`" (#272).
///
/// The allocator's ownership rule makes the destructor simple: destroying an unbound buffer releases the
/// buffer and frees no memory. The block's owner frees the memory, after every resource bound into it is
/// gone.
class TransientBufferResource : public IGraphBuffer
{
public:
    /// @param nSize         Buffer size in bytes. The buffer itself is created by `Adopt`, not here.
    /// @param nUsage        Vulkan buffer usage flags.
    /// @param nMemoryUsage  The declared memory usage, which decides whether `SetData` can write through
    ///                      a host mapping or has to travel by transfer.
    /// @param allocator     Allocator that created the buffer and owns the block it is bound into.
    /// @param device        Device used for the staging copy and for debug labels.
    TransientBufferResource(size_t nSize, VkBufferUsageFlags nUsage, VmaMemoryUsage nMemoryUsage,
                            IGraphResourceAllocator& allocator, VkRenderDevice& device)
        : m_nAliasedSize(nSize), m_usage(nUsage), m_memoryUsage(nMemoryUsage), m_allocator(allocator), m_device(device)
    {
    }
    /// Destroy the unbound buffer. Its memory belongs to the block, which the graph frees afterwards.
    ~TransientBufferResource() override
    {
        // Qualified: this is the override this class defines, not a further-derived one that no longer
        // exists. The analyzer flags any virtual call in a destructor, correctly in general.
        TransientBufferResource::Unmap();
        if (m_buffer != VK_NULL_HANDLE) m_allocator.DestroyUnboundBuffer(m_buffer);
    }

    /// Take ownership of the unbound buffer the allocator created for this resource.
    /// @param buffer Buffer this resource owns, binds into a block, and destroys - without freeing its
    ///               memory, which the block owns.
    void Adopt(VkBuffer buffer) { m_buffer = buffer; }

    /// Record the block and offset this resource was bound at, so a host mapping can find its bytes.
    /// @param block  The block the buffer is bound into. Not owned, and outlived by nothing: the graph
    ///               destroys every resource before it frees any block.
    /// @param nOffset Byte offset of this buffer inside that block.
    void BindTo(GraphMemoryBlock block, VkDeviceSize nOffset)
    {
        m_block = block;
        m_nOffset = nOffset;
    }

    /// @return The Vulkan buffer handle.
    VkBuffer buffer() const override { return m_buffer; }
    /// @return The size the buffer was created with, which is what the block's plan reserved for it.
    size_t GetSize() const override { return m_nAliasedSize; }
    /// @return The size the buffer was created with; an aliased buffer cannot grow.
    size_t GetCapacity() const override { return m_nAliasedSize; }
    /// @return The Vulkan usage flags the buffer was created with.
    VkBufferUsageFlags GetBufferUsage() const override { return m_usage; }
    /// @return The declared memory usage.
    VmaMemoryUsage GetMemoryUsage() const override { return m_memoryUsage; }
    /// @return `ResourceKind::BUFFER`.
    ResourceKind GetResourceKind() const override { return ResourceKind::BUFFER; }
    /// @return `VK_OBJECT_TYPE_BUFFER`.
    VkObjectType GetVkObjectType() const override { return VK_OBJECT_TYPE_BUFFER; }
    /// Label the buffer for the validation layer and a capture tool.
    /// @param name Debug name to attach.
    void SetDebugName(const std::string& name) const override
    {
        setDebugUtilsObjectName(m_device.GetDevice(), m_device.GetInstance(), reinterpret_cast<uint64_t>(m_buffer),
                                VK_OBJECT_TYPE_BUFFER, name);
    }

    /// Map this resource's bytes on the host.
    ///
    /// The allocation to map is the *block*, not the resource - an aliased buffer has no allocation of its
    /// own. The returned pointer is the block's mapping plus this resource's offset. Idempotent: mapping an
    /// already-mapped resource returns the same pointer rather than taking a second reference on the block,
    /// which is shared with every other member of the slot.
    /// @return A host pointer to the start of this resource's storage, or nullptr when it is not bound yet.
    void* Map() override
    {
        if (m_block.IsNull()) return nullptr;
        if (m_pMapped != nullptr) return m_pMapped;
        m_pMapped = static_cast<uint8_t*>(m_allocator.MapMemory(m_block, m_nOffset));
        return m_pMapped;
    }
    /// Release the mapping `Map` took. A resource that is not mapped is left alone, so a path that unmaps
    /// defensively cannot release the block out from under a member that still holds a pointer into it.
    void Unmap() override
    {
        if (m_pMapped == nullptr) return;
        m_allocator.UnmapMemory(m_block);
        m_pMapped = nullptr;
    }
    /// Upload content into this buffer.
    ///
    /// The storage is the block's, so there is nothing to allocate - and nothing to grow: the plan
    /// reserved this resource's size and the block is exactly as large as the plan said. A larger upload is
    /// a declaration error rather than a silent reallocation, which is why it is named here.
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
        // aliased buffer has.
        if (m_memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU || m_memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY)
        {
            std::memcpy(Map(), pData, size);
            Unmap();
            return;
        }

        // Device-local storage has to travel by transfer, and that needs the declaration to say so.
        if ((m_usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0)
        {
            throw std::runtime_error(
                "Transient buffer declares no TRANSFER_DST and is not host-visible, so "
                "nothing can write its content. Declare TRANSFER_DST, or a memory "
                "property that allows mapping.");
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        GraphMemoryBlock stagingMemory{};
        m_allocator.AllocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer,
                                   stagingMemory);
        void* pMappedMemory = m_allocator.MapMemory(stagingMemory);
        std::memcpy(pMappedMemory, pData, size);
        m_allocator.UnmapMemory(stagingMemory);

        m_device.ExecuteImmediateCommand(
            [&](VkCommandBuffer commandBuffer)
            {
                VkBufferCopy copyRegion{};
                copyRegion.size = size;
                vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_buffer, 1, &copyRegion);
#ifdef FEATURE_RAY_TRACING
                // Make sure the copy of the instance buffer is copied before triggering the acceleration
                // structure build. Assign by name: the sync2 barrier structs are not laid out the way a
                // positional initializer would assume.
                VkMemoryBarrier2 barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
                barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                VkDependencyInfo dependencyInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependencyInfo.memoryBarrierCount = 1;
                dependencyInfo.pMemoryBarriers = &barrier;
                vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
#endif
            });

        m_allocator.FreeBuffer(stagingBuffer, stagingMemory);
    }

    /// @return True while this resource holds a host mapping. The graph's contract is that a mapping does
    ///         not outlive the node whose use declared the resource, because that is what makes its slot
    ///         exclusively its own for as long as the pointer is held.
    bool IsMapped() const { return m_pMapped != nullptr; }

    /// @return The size the buffer was created with, which is what the block's plan reserved for it.
    size_t GetAliasedSize() const { return m_nAliasedSize; }

private:
    /// The block this resource was bound into, or null before `BindTo`.
    GraphMemoryBlock m_block;
    /// This resource's byte offset inside that block.
    VkDeviceSize m_nOffset = 0;
    /// The pointer `Map` returned, or null when unmapped. Kept so `Map`/`Unmap` are symmetric: the
    /// allocator counts mappings per block, and the block is shared with the other members of the slot.
    uint8_t* m_pMapped = nullptr;
    /// The size this resource was planned at.
    size_t m_nAliasedSize = 0;
    /// The Vulkan buffer handle, or null before `Adopt`.
    VkBuffer m_buffer = VK_NULL_HANDLE;
    /// The declared usage flags.
    VkBufferUsageFlags m_usage = 0;
    /// The declared memory usage.
    VmaMemoryUsage m_memoryUsage = VMA_MEMORY_USAGE_UNKNOWN;
    /// Allocator that created the buffer and owns the block.
    IGraphResourceAllocator& m_allocator;
    /// Device used for staging uploads and debug labels.
    VkRenderDevice& m_device;
};

/// A transient image whose memory belongs to an aliasing block rather than to the image.
///
/// The image counterpart of `TransientBufferResource`: it implements the graph-owned `IGraphImage`
/// interface, so the barrier and descriptor paths need no engine class to recognise it.
///
/// The allocator's ownership rule makes the destructor simple again: destroying an unbound image releases
/// the image and frees no memory, which is right because the block owns the memory and the graph frees the
/// block after every image bound into it is gone. The **view** is not aliased: a view belongs to one
/// resource and is created and destroyed as usual.
class TransientImageResource : public IGraphImage
{
public:
    /// @param imageInfo The create info the allocator created the image with; also the aliasing identity.
    /// @param allocator Allocator that created the image and owns the block it is bound into.
    /// @param device    Device used to create the view and for debug labels.
    TransientImageResource(const VkImageCreateInfo& imageInfo, IGraphResourceAllocator& allocator,
                           VkRenderDevice& device)
        : m_imageInfo(imageInfo), m_allocator(allocator), m_device(device)
    {
        // The view's subresource range, which the inherited creator does not fill in. A depth format needs
        // the depth aspect; everything else is colour, which is the same split the usage flags use.
        m_imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        m_imageViewInfo.subresourceRange.aspectMask =
            FormatSupportsOptimalTilingDepthAttachment(m_device.GetPhysicalDevice(), imageInfo.format)
                ? VK_IMAGE_ASPECT_DEPTH_BIT
                : VK_IMAGE_ASPECT_COLOR_BIT;
        m_imageViewInfo.subresourceRange.baseMipLevel = 0;
        m_imageViewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
        m_imageViewInfo.subresourceRange.baseArrayLayer = 0;
        m_imageViewInfo.subresourceRange.layerCount = imageInfo.arrayLayers;
    }
    /// Destroy the view and then the unbound image; its memory belongs to the block.
    ~TransientImageResource() override
    {
        if (m_view != VK_NULL_HANDLE) m_allocator.DestroyImageView(m_view);
        if (m_image != VK_NULL_HANDLE) m_allocator.DestroyUnboundImage(m_image);
    }

    /// @return The create info this image must be created with, which the caller needs because the
    ///         allocator creates it before the resource can own it.
    const VkImageCreateInfo& ImageInfoForCreate() const { return m_imageInfo; }

    /// Take ownership of the unbound image the allocator created.
    /// @param image Image this resource owns, binds into a block, and destroys - without freeing its
    ///              memory, which the block owns.
    void Adopt(VkImage image) { m_image = image; }

    /// Create this image's view.
    ///
    /// Called after the image is bound into its block, not when it is adopted, because a view cannot be
    /// created for an image with no memory bound - the validation layer says so in as many words
    /// (`VUID-VkImageViewCreateInfo-image-01020`: "used with no memory bound"). The order is therefore
    /// create unbound, query, plan, bind, view.
    void CreateView()
    {
        m_imageViewInfo.image = m_image;
        m_imageViewInfo.format = m_imageInfo.format;
        m_view = m_allocator.CreateImageView(m_imageViewInfo);
    }

    /// @return The Vulkan image handle.
    VkImage getImage() const override { return m_image; }
    /// @return The image's view.
    VkImageView getView() const override { return m_view; }
    /// @return The view create info.
    const VkImageViewCreateInfo& GetImageViewCreateInfo() const override { return m_imageViewInfo; }
    /// @return The image's format.
    VkFormat GetImageFormat() const override { return m_imageInfo.format; }
    /// @return The image's extent.
    VkExtent2D GetImageExtent() const override { return {m_imageInfo.extent.width, m_imageInfo.extent.height}; }
    /// @return The usage flags the image was created with.
    VkImageUsageFlags GetImageUsage() const override { return m_imageInfo.usage; }
    /// @return The created mip level count.
    uint32_t GetImageMipCount() const override { return m_imageInfo.mipLevels; }
    /// @return The created array layer count.
    uint32_t GetImageLayerCount() const override { return m_imageInfo.arrayLayers; }
    /// @return `ResourceKind::IMAGE`.
    ResourceKind GetResourceKind() const override { return ResourceKind::IMAGE; }
    /// @return `VK_OBJECT_TYPE_IMAGE`.
    VkObjectType GetVkObjectType() const override { return VK_OBJECT_TYPE_IMAGE; }
    /// Label the image for the validation layer and a capture tool.
    /// @param name Debug name to attach.
    void SetDebugName(const std::string& name) const override
    {
        setDebugUtilsObjectName(m_device.GetDevice(), m_device.GetInstance(), reinterpret_cast<uint64_t>(m_image),
                                VK_OBJECT_TYPE_IMAGE, name);
    }

private:
    /// The create info the allocator created the image with.
    VkImageCreateInfo m_imageInfo{};
    /// The view create info, filled in by `CreateView`.
    VkImageViewCreateInfo m_imageViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    /// The Vulkan image handle.
    VkImage m_image = VK_NULL_HANDLE;
    /// The image's view.
    VkImageView m_view = VK_NULL_HANDLE;
    /// Allocator that created the image and owns the block.
    IGraphResourceAllocator& m_allocator;
    /// Device used to create the view.
    VkRenderDevice& m_device;
};
}  // namespace Muyo::RenderGraph
