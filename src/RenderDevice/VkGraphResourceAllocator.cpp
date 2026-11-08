#include "VkGraphResourceAllocator.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "Debug.h"

namespace Muyo
{
namespace
{
/// @param allocation A VMA allocation.
/// @return The graph-owned opaque handle for it.
RenderGraph::GraphMemoryBlock ToBlock(VmaAllocation allocation)
{
    return RenderGraph::GraphMemoryBlock{reinterpret_cast<void*>(allocation)};
}

/// @param block A graph-owned opaque handle.
/// @return The VMA allocation it names.
VmaAllocation ToAllocation(RenderGraph::GraphMemoryBlock block) { return reinterpret_cast<VmaAllocation>(block.pImpl); }

/// @param size Requested capacity in bytes.
/// @return The same size rounded up to 256 bytes, so consecutive uploads of the same order reuse an
///         allocation instead of recreating the buffer a descriptor or device address already names.
size_t RoundUpCapacity(size_t size)
{
    constexpr size_t nAlignment = 256;
    return (size + nAlignment - 1) & ~(nAlignment - 1);
}
}  // namespace

VkGraphBuffer::VkGraphBuffer(VkRenderDevice& device, VkMemoryAllocator& allocator, size_t size,
                             VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, const std::string& name,
                             PoolType pool)
    : m_device(device), m_allocator(allocator), m_usage(usage), m_memoryUsage(memoryUsage), m_pool(pool), m_name(name)
{
    Allocate(size);
    m_nSize = size;
    if (!name.empty()) SetDebugName(name);
}

VkGraphBuffer::~VkGraphBuffer()
{
    Unmap();
    if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
    {
        m_allocator.FreeBuffer(m_buffer, m_allocation);
    }
}

void VkGraphBuffer::Allocate(size_t size)
{
    if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
    {
        m_allocator.FreeBuffer(m_buffer, m_allocation);
    }
    m_allocator.AllocateBuffer(size, m_usage, m_memoryUsage, m_buffer, m_allocation, m_name, m_pool);
    m_nCapacity = size;
}

void VkGraphBuffer::SetDebugName(const std::string& name) const
{
    if (m_buffer == VK_NULL_HANDLE) return;
    setDebugUtilsObjectName(m_device.GetDevice(), m_device.GetInstance(), reinterpret_cast<uint64_t>(m_buffer),
                            VK_OBJECT_TYPE_BUFFER, name);
}

void* VkGraphBuffer::Map()
{
    if (m_pMapped != nullptr) return m_pMapped;
    m_allocator.MapBuffer(m_allocation, &m_pMapped);
    return m_pMapped;
}

void VkGraphBuffer::Unmap()
{
    // VMA asserts when an allocation is unmapped that was not mapped; a defensive unmap has to be a
    // no-op rather than a VMA contract violation.
    if (m_pMapped == nullptr) return;
    m_allocator.UnmapBuffer(m_allocation);
    m_pMapped = nullptr;
}

void VkGraphBuffer::SetData(const void* pData, size_t size)
{
    // A `VkBuffer` is fixed-size, so growing frees and re-creates it - leaving every descriptor set and
    // device address that named it pointing at the old handle (#13). Allocate with headroom and reuse the
    // handle while the request fits; only a request past the capacity reallocates.
    if (m_buffer == VK_NULL_HANDLE || size > m_nCapacity)
    {
        // Release a mapping before the allocation it points into is freed.
        Unmap();
        const size_t nNewCapacity = RoundUpCapacity(std::max(size, static_cast<size_t>(m_nCapacity) * 2));
        Allocate(nNewCapacity);
    }
    m_nSize = size;

    if (pData == nullptr || size == 0) return;

    if ((m_usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0)
    {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        m_allocator.AllocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, stagingBuffer,
                                   stagingAllocation);

        void* pMappedMemory = nullptr;
        m_allocator.MapBuffer(stagingAllocation, &pMappedMemory);
        std::memcpy(pMappedMemory, pData, size);
        m_allocator.UnmapBuffer(stagingAllocation);

        m_device.ExecuteImmediateCommand(
            [&](VkCommandBuffer commandBuffer)
            {
                VkBufferCopy copyRegion{};
                copyRegion.size = size;
                vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_buffer, 1, &copyRegion);
#ifdef FEATURE_RAY_TRACING
                // Make sure the copy is visible before an acceleration-structure build reads it. Assign
                // by name: the sync2 barrier structs are not laid out the way a positional initializer
                // would assume.
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

        m_allocator.FreeBuffer(stagingBuffer, stagingAllocation);
        return;
    }

    void* pMappedMemory = Map();
    std::memcpy(pMappedMemory, pData, size);
    Unmap();
}

VkGraphImage::VkGraphImage(VkRenderDevice& device, VkMemoryAllocator& allocator, const VkImageCreateInfo& imageInfo,
                           VmaMemoryUsage memoryUsage, const std::string& name)
    : m_device(device), m_allocator(allocator), m_imageInfo(imageInfo), m_name(name)
{
    m_allocator.AllocateImage(&m_imageInfo, memoryUsage, m_image, m_allocation);

    m_imageViewInfo.image = m_image;
    m_imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    m_imageViewInfo.format = m_imageInfo.format;
    m_imageViewInfo.subresourceRange.aspectMask =
        RenderGraph::FormatSupportsOptimalTilingDepthAttachment(m_device.GetPhysicalDevice(), m_imageInfo.format)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;
    m_imageViewInfo.subresourceRange.baseMipLevel = 0;
    m_imageViewInfo.subresourceRange.levelCount = m_imageInfo.mipLevels;
    m_imageViewInfo.subresourceRange.baseArrayLayer = 0;
    m_imageViewInfo.subresourceRange.layerCount = m_imageInfo.arrayLayers;
    VK_ASSERT(vkCreateImageView(m_device.GetDevice(), &m_imageViewInfo, nullptr, &m_view));

    if (!name.empty()) SetDebugName(name);
}

VkGraphImage::~VkGraphImage()
{
    if (m_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device.GetDevice(), m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
    {
        m_allocator.FreeImage(m_image, m_allocation);
    }
}

void VkGraphImage::SetDebugName(const std::string& name) const
{
    if (m_image == VK_NULL_HANDLE) return;
    setDebugUtilsObjectName(m_device.GetDevice(), m_device.GetInstance(), reinterpret_cast<uint64_t>(m_image),
                            VK_OBJECT_TYPE_IMAGE, name);
}

VkGraphResourceAllocator::VkGraphResourceAllocator(VkRenderDevice& device, VkMemoryAllocator& allocator)
    : m_device(device), m_allocator(allocator)
{
}

std::unique_ptr<RenderGraph::IGraphBuffer> VkGraphResourceAllocator::CreateBuffer(size_t size, VkBufferUsageFlags usage,
                                                                                  VmaMemoryUsage memoryUsage,
                                                                                  const std::string& debugName,
                                                                                  RenderGraph::GraphBufferPool pool)
{
    const PoolType devicePool =
        pool == RenderGraph::GraphBufferPool::ShaderBindingTable ? PoolType::SBT : PoolType::Default;
    return std::make_unique<VkGraphBuffer>(m_device, m_allocator, size, usage, memoryUsage, debugName, devicePool);
}

std::unique_ptr<RenderGraph::IGraphImage> VkGraphResourceAllocator::CreateImage(const VkImageCreateInfo& imageInfo,
                                                                                VmaMemoryUsage memoryUsage,
                                                                                const std::string& debugName)
{
    return std::make_unique<VkGraphImage>(m_device, m_allocator, imageInfo, memoryUsage, debugName);
}

VkBuffer VkGraphResourceAllocator::CreateUnboundBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBuffer buffer = VK_NULL_HANDLE;
    m_allocator.CreateUnboundBuffer(size, usage, buffer);
    return buffer;
}

void VkGraphResourceAllocator::DestroyUnboundBuffer(VkBuffer buffer)
{
    if (buffer == VK_NULL_HANDLE) return;
    VmaAllocation noMemory = VK_NULL_HANDLE;
    m_allocator.FreeBuffer(buffer, noMemory);
}

VkMemoryRequirements VkGraphResourceAllocator::QueryBufferRequirements(VkBuffer buffer) const
{
    return m_allocator.QueryBufferRequirements(buffer);
}

VkImage VkGraphResourceAllocator::CreateUnboundImage(const VkImageCreateInfo& imageInfo)
{
    VkImage image = VK_NULL_HANDLE;
    m_allocator.CreateUnboundImage(imageInfo, image);
    return image;
}

void VkGraphResourceAllocator::DestroyUnboundImage(VkImage image)
{
    if (image == VK_NULL_HANDLE) return;
    VmaAllocation noMemory = VK_NULL_HANDLE;
    m_allocator.FreeImage(image, noMemory);
}

VkMemoryRequirements VkGraphResourceAllocator::QueryImageRequirements(VkImage image) const
{
    return m_allocator.QueryImageRequirements(image);
}

RenderGraph::GraphMemoryBlock VkGraphResourceAllocator::AllocateAliasingBlock(const VkMemoryRequirements& requirements,
                                                                              VmaMemoryUsage memoryUsage)
{
    return ToBlock(m_allocator.AllocateAliasingBlock(requirements, memoryUsage));
}

void VkGraphResourceAllocator::BindBufferToBlock(VkBuffer buffer, RenderGraph::GraphMemoryBlock block,
                                                 VkDeviceSize offset)
{
    m_allocator.BindBufferToBlock(buffer, ToAllocation(block), offset);
}

void VkGraphResourceAllocator::BindImageToBlock(VkImage image, RenderGraph::GraphMemoryBlock block, VkDeviceSize offset)
{
    m_allocator.BindImageToBlock(image, ToAllocation(block), offset);
}

void VkGraphResourceAllocator::FreeAliasingBlock(RenderGraph::GraphMemoryBlock block)
{
    VmaAllocation allocation = ToAllocation(block);
    m_allocator.FreeAliasingBlock(allocation);
}

void VkGraphResourceAllocator::AllocateBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                                              VkBuffer& buffer, RenderGraph::GraphMemoryBlock& memory)
{
    VmaAllocation allocation = VK_NULL_HANDLE;
    m_allocator.AllocateBuffer(size, usage, memoryUsage, buffer, allocation);
    memory = ToBlock(allocation);
}

void VkGraphResourceAllocator::FreeBuffer(VkBuffer& buffer, RenderGraph::GraphMemoryBlock& memory)
{
    VmaAllocation allocation = ToAllocation(memory);
    m_allocator.FreeBuffer(buffer, allocation);
    memory = ToBlock(allocation);
}

void* VkGraphResourceAllocator::MapMemory(RenderGraph::GraphMemoryBlock block, VkDeviceSize offset)
{
    VmaAllocation allocation = ToAllocation(block);
    void* pMapped = nullptr;
    m_allocator.MapBuffer(allocation, &pMapped);
    return static_cast<uint8_t*>(pMapped) + offset;
}

void VkGraphResourceAllocator::UnmapMemory(RenderGraph::GraphMemoryBlock block)
{
    VmaAllocation allocation = ToAllocation(block);
    m_allocator.UnmapBuffer(allocation);
}

VkImageView VkGraphResourceAllocator::CreateImageView(const VkImageViewCreateInfo& createInfo)
{
    VkImageView view = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateImageView(m_device.GetDevice(), &createInfo, nullptr, &view));
    return view;
}

void VkGraphResourceAllocator::DestroyImageView(VkImageView view)
{
    if (view != VK_NULL_HANDLE) vkDestroyImageView(m_device.GetDevice(), view, nullptr);
}

std::unique_ptr<RenderGraph::IGraphResourceAllocator> CreateVkGraphResourceAllocator(VkRenderDevice* device)
{
    if (device == nullptr)
    {
        throw std::runtime_error("CreateVkGraphResourceAllocator: the render device is null.");
    }
    return std::make_unique<VkGraphResourceAllocator>(*device, *GetMemoryAllocator());
}
}  // namespace Muyo
