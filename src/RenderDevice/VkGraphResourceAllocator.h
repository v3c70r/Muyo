#pragma once
#include <memory>
#include <string>

#include "RenderGraphResources.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{
/// A VMA-backed buffer owned by the graph but implemented by the device.
///
/// The graph holds it through `RenderGraph::IGraphBuffer`; the concrete class lives here because the
/// memory is the device's (`docs/RenderGraph-Library-Design.md` section 4, #272). `muyo_device` includes
/// the graph-owned interface header, not the other way round: the interface is pure virtual and
/// header-only, so this is an implementation edge with no link edge back to `muyo_rg`.
class VkGraphBuffer final : public RenderGraph::IGraphBuffer
{
public:
    /// Allocate the buffer and its memory.
    /// @param device    Device to allocate on and label.
    /// @param allocator Allocator that owns the memory.
    /// @param size      Size in bytes.
    /// @param usage     Vulkan buffer usage flags.
    /// @param memoryUsage Intended memory usage.
    /// @param name      Debug name; may be empty.
    /// @param pool      Allocation pool.
    VkGraphBuffer(VkRenderDevice& device, VkMemoryAllocator& allocator, size_t size, VkBufferUsageFlags usage,
                  VmaMemoryUsage memoryUsage, const std::string& name, PoolType pool = PoolType::Default);
    /// Destroy the buffer and its memory.
    ~VkGraphBuffer() override;

    /// Owns Vulkan handles, so copying would double-destroy the buffer.
    VkGraphBuffer(const VkGraphBuffer&) = delete;
    /// See the copy constructor.
    VkGraphBuffer& operator=(const VkGraphBuffer&) = delete;

    /// @return The Vulkan buffer handle.
    VkBuffer buffer() const override { return m_buffer; }
    /// @return The declared size in bytes.
    size_t GetSize() const override { return m_nSize; }
    /// @return The allocated capacity in bytes.
    size_t GetCapacity() const override { return m_nCapacity; }
    /// @return The Vulkan usage flags.
    VkBufferUsageFlags GetBufferUsage() const override { return m_usage; }
    /// @return The memory usage.
    VmaMemoryUsage GetMemoryUsage() const override { return m_memoryUsage; }
    /// Map the buffer's memory.
    /// @return A host pointer to its storage.
    void* Map() override;
    /// Release a mapping `Map` took.
    void Unmap() override;
    /// Upload content, growing the allocation when the request no longer fits.
    /// @param pData Source bytes, or `nullptr` to size the buffer without uploading.
    /// @param size  Byte count.
    void SetData(const void* pData, size_t size) override;
    /// @return `ResourceKind::BUFFER`.
    RenderGraph::ResourceKind GetResourceKind() const override { return RenderGraph::ResourceKind::BUFFER; }
    /// @return `VK_OBJECT_TYPE_BUFFER`.
    VkObjectType GetVkObjectType() const override { return VK_OBJECT_TYPE_BUFFER; }
    /// Label the buffer for the validation layer and a capture tool.
    /// @param name Debug name to attach.
    void SetDebugName(const std::string& name) const override;

private:
    /// Allocate (or reallocate) this buffer's memory.
    /// @param size Bytes to allocate.
    void Allocate(size_t size);

    VkRenderDevice& m_device;                                 ///< Device the buffer belongs to.
    VkMemoryAllocator& m_allocator;                           ///< Allocator that owns the memory.
    VkBuffer m_buffer = VK_NULL_HANDLE;                       ///< The buffer.
    VmaAllocation m_allocation = VK_NULL_HANDLE;              ///< Its memory.
    size_t m_nSize = 0;                                       ///< Declared size in bytes.
    size_t m_nCapacity = 0;                                   ///< Allocated capacity in bytes.
    VkBufferUsageFlags m_usage = 0;                           ///< Vulkan usage flags.
    VmaMemoryUsage m_memoryUsage = VMA_MEMORY_USAGE_UNKNOWN;  ///< Memory usage.
    PoolType m_pool = PoolType::Default;                      ///< Allocation pool.
    std::string m_name;                                       ///< Debug name.
    void* m_pMapped = nullptr;  ///< The host mapping `Map` returned, or null when unmapped.
};

/// A VMA-backed image owned by the graph but implemented by the device.
///
/// The image and its view are created together; the graph only ever reads the handles and the create
/// info out of it.
class VkGraphImage final : public RenderGraph::IGraphImage
{
public:
    /// Allocate the image, its memory and its view.
    /// @param device      Device to allocate on and label.
    /// @param allocator   Allocator that owns the memory.
    /// @param imageInfo   Image create info.
    /// @param memoryUsage Intended memory usage.
    /// @param name        Debug name; may be empty.
    VkGraphImage(VkRenderDevice& device, VkMemoryAllocator& allocator, const VkImageCreateInfo& imageInfo,
                 VmaMemoryUsage memoryUsage, const std::string& name);
    /// Destroy the view, the image and its memory.
    ~VkGraphImage() override;

    /// Owns Vulkan handles, so copying would double-destroy the image.
    VkGraphImage(const VkGraphImage&) = delete;
    /// See the copy constructor.
    VkGraphImage& operator=(const VkGraphImage&) = delete;

    /// @return The Vulkan image handle.
    VkImage getImage() const override { return m_image; }
    /// @return The image's view.
    VkImageView getView() const override { return m_view; }
    /// @return The create-info used for the view.
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
    RenderGraph::ResourceKind GetResourceKind() const override { return RenderGraph::ResourceKind::IMAGE; }
    /// @return `VK_OBJECT_TYPE_IMAGE`.
    VkObjectType GetVkObjectType() const override { return VK_OBJECT_TYPE_IMAGE; }
    /// Label the image for the validation layer and a capture tool.
    /// @param name Debug name to attach.
    void SetDebugName(const std::string& name) const override;

private:
    VkRenderDevice& m_device;         ///< Device the image belongs to.
    VkMemoryAllocator& m_allocator;   ///< Allocator that owns the memory.
    VkImageCreateInfo m_imageInfo{};  ///< The create info it was made with.
    VkImageViewCreateInfo m_imageViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};  ///< The view create info.
    VkImage m_image = VK_NULL_HANDLE;                                                 ///< The image.
    VmaAllocation m_allocation = VK_NULL_HANDLE;                                      ///< Its memory.
    VkImageView m_view = VK_NULL_HANDLE;                                              ///< Its view.
    std::string m_name;                                                               ///< Debug name.
};

/// The VMA-backed implementation of the graph's resource allocator.
///
/// This is where the graph's resources really come from in this repository: one block of code that knows
/// about VMA, so `muyo_rg` itself does not. A consumer with another memory manager implements
/// `RenderGraph::IGraphResourceAllocator` instead.
class VkGraphResourceAllocator final : public RenderGraph::IGraphResourceAllocator
{
public:
    /// @param device    Device resources are created on; must outlive this allocator.
    /// @param allocator Allocator that owns the memory; must outlive this allocator.
    VkGraphResourceAllocator(VkRenderDevice& device, VkMemoryAllocator& allocator);

    /// @return A buffer and the memory it owns.
    std::unique_ptr<RenderGraph::IGraphBuffer> CreateBuffer(size_t size, VkBufferUsageFlags usage,
                                                            VmaMemoryUsage memoryUsage, const std::string& debugName,
                                                            RenderGraph::GraphBufferPool pool) override;
    /// @return An image, its memory and its view.
    std::unique_ptr<RenderGraph::IGraphImage> CreateImage(const VkImageCreateInfo& imageInfo,
                                                          VmaMemoryUsage memoryUsage,
                                                          const std::string& debugName) override;
    /// @return An unbound buffer.
    VkBuffer CreateUnboundBuffer(VkDeviceSize size, VkBufferUsageFlags usage) override;
    /// Destroy an unbound buffer.
    void DestroyUnboundBuffer(VkBuffer buffer) override;
    /// @param buffer An unbound buffer.
    /// @return Its memory requirements.
    VkMemoryRequirements QueryBufferRequirements(VkBuffer buffer) const override;
    /// @return An unbound image.
    VkImage CreateUnboundImage(const VkImageCreateInfo& imageInfo) override;
    /// Destroy an unbound image.
    void DestroyUnboundImage(VkImage image) override;
    /// @param image An unbound image.
    /// @return Its memory requirements.
    VkMemoryRequirements QueryImageRequirements(VkImage image) const override;
    /// @return An aliasing block.
    RenderGraph::GraphMemoryBlock AllocateAliasingBlock(const VkMemoryRequirements& requirements,
                                                        VmaMemoryUsage memoryUsage) override;
    /// Bind a buffer into an aliasing block.
    void BindBufferToBlock(VkBuffer buffer, RenderGraph::GraphMemoryBlock block, VkDeviceSize offset) override;
    /// Bind an image into an aliasing block.
    void BindImageToBlock(VkImage image, RenderGraph::GraphMemoryBlock block, VkDeviceSize offset) override;
    /// Free an aliasing block.
    void FreeAliasingBlock(RenderGraph::GraphMemoryBlock block) override;
    /// Allocate a buffer and the memory it owns.
    void AllocateBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkBuffer& buffer,
                        RenderGraph::GraphMemoryBlock& memory) override;
    /// Free a buffer and its memory.
    void FreeBuffer(VkBuffer& buffer, RenderGraph::GraphMemoryBlock& memory) override;
    /// Map a block.
    void* MapMemory(RenderGraph::GraphMemoryBlock block, VkDeviceSize offset) override;
    /// Unmap a block.
    void UnmapMemory(RenderGraph::GraphMemoryBlock block) override;
    /// @return A new image view.
    VkImageView CreateImageView(const VkImageViewCreateInfo& createInfo) override;
    /// Destroy an image view.
    void DestroyImageView(VkImageView view) override;

private:
    VkRenderDevice& m_device;        ///< Device resources are created on.
    VkMemoryAllocator& m_allocator;  ///< Allocator that owns the memory.
};

/// Create the device's graph resource allocator for `device`.
///
/// The convenience the graph's `RenderGraphBuilder(VkRenderDevice*)` constructor and the in-tree hosts
/// use. A consumer that keeps its own `VkMemoryAllocator` should construct `VkGraphResourceAllocator`
/// directly instead.
/// @param device Device to allocate on.
/// @return The allocator, owned by the caller.
std::unique_ptr<RenderGraph::IGraphResourceAllocator> CreateVkGraphResourceAllocator(VkRenderDevice* device);
}  // namespace Muyo
