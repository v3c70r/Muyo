#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cassert>

#include "Debug.h"
#include "VkExtFuncsLoader.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{
class IRenderResource
{
public:
    virtual ~IRenderResource(){};
    virtual VkObjectType GetVkObjectType() const = 0;
    virtual void SetDebugName(const std::string& sName) const = 0;
};

class ImageResource : public IRenderResource
{
public:
    VkImageView getView() const { return m_view; }
    VkImage getImage() const { return m_image; }
    /// @return The create-info used for the view, for descriptor-heap writes that need the view
    ///         description rather than a VkImageView handle.
    const VkImageViewCreateInfo& GetImageViewCreateInfo() const { return m_imageViewInfo; }
    virtual ~ImageResource()
    {
        vkDestroyImageView(GetRenderDevice()->GetDevice(), m_view, nullptr);
        GetMemoryAllocator()->FreeImage(m_image, m_allocation);
    }
    virtual VkObjectType GetVkObjectType() const override { return VK_OBJECT_TYPE_IMAGE; }
    virtual void SetDebugName(const std::string& sName) const override
    {
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_image), GetVkObjectType(), sName.c_str());
    }
    VkFormat GetImageFormat() const { return m_imageInfo.format; }
    /// @return The image's extent, for a consumer that must know the render area (an imported render
    ///         target has no ResourceDesc to read it from).
    VkExtent2D GetImageExtent() const { return {m_imageInfo.extent.width, m_imageInfo.extent.height}; }
    /// @return The image's usage flags as created, including the attachment bits the allocator adds for
    ///         the format. Build() compares this against a declaration before reusing a resource (#209).
    VkImageUsageFlags GetImageUsage() const { return m_imageInfo.usage; }
    /// @return The created mip level count.
    uint32_t GetImageMipCount() const { return m_imageInfo.mipLevels; }
    /// @return The created array layer count.
    uint32_t GetImageLayerCount() const { return m_imageInfo.arrayLayers; }

protected:
    void CreateImageInternal(const VmaMemoryUsage& memoryUsage)
    {
        GetMemoryAllocator()->AllocateImage(&m_imageInfo, memoryUsage, m_image, m_allocation);
    }

    void CreateImageViewInternal()
    {
        assert(m_image != VK_NULL_HANDLE);
        m_imageViewInfo.image = m_image;
        m_imageViewInfo.format = m_imageInfo.format;

        VK_ASSERT(vkCreateImageView(GetRenderDevice()->GetDevice(), &m_imageViewInfo, nullptr, &m_view));
    }

    static void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                      uint32_t nMipCount = 1, uint32_t nLayerCount = 1);

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkImageCreateInfo m_imageInfo = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,                          // flags
        VK_IMAGE_TYPE_2D,           // imageType
        VK_FORMAT_UNDEFINED,        // Format
        {0, 0, 0},                  // Extent
        1,                          // mipLevels;
        1,                          // arrayLayers;
        VK_SAMPLE_COUNT_1_BIT,      // samples;
        VK_IMAGE_TILING_OPTIMAL,    // tiling;
        0,                          // usage;
        VK_SHARING_MODE_EXCLUSIVE,  // sharingMode;
        0,                          // queueFamilyIndexCount;
        nullptr,                    // pQueueFamilyIndices;
        VK_IMAGE_LAYOUT_UNDEFINED   // initialLayout;
    };

    VkImageViewCreateInfo m_imageViewInfo = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,  // sType;
        nullptr,                                   // pNext;
        0,                                         // flags;
        VK_NULL_HANDLE,                            // image;
        VK_IMAGE_VIEW_TYPE_2D,                     // viewType;
        VK_FORMAT_UNDEFINED,                       // format;
        {},                                        // components;
        {}                                         // subresourceRange;
    };
};

class BufferResource : public IRenderResource
{
public:
    BufferResource(VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage)
        : BUFFER_USAGE(bufferUsage), MEMORY_USAGE(memoryUsage)
    {
    }
    BufferResource(VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, size_t sizeInByte)
        // Declaration order, not logical order: `m_nSize` is declared before the two const members, and a
        // mismatched initializer list is `-Wreorder` per translation unit that includes this header (#25).
        : m_nSize(sizeInByte), BUFFER_USAGE(bufferUsage), MEMORY_USAGE(memoryUsage)
    {
        AllocateOwnedBuffer(sizeInByte);
    }
    virtual VkBuffer buffer() const { return m_buffer; }
    virtual ~BufferResource() { GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation); }
    virtual VkObjectType GetVkObjectType() const override { return VK_OBJECT_TYPE_BUFFER; }

    virtual void SetDebugName(const std::string& sName) const override
    {
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_buffer), GetVkObjectType(), sName.c_str());
    }
    /// Upload content into this buffer, allocating or reallocating storage if the base class owns it.
    ///
    /// Virtual for the same reason `Map` is: a resource whose storage belongs to a shared block cannot
    /// allocate, and cannot grow. An override reaches the staging copy - the part that is about the
    /// resource rather than about its memory - by calling this with a size the resource already has.
    /// @param pData Source bytes, or `nullptr` to size the buffer without uploading anything. The UI pass
    ///        uses that to reserve a CPU-visible buffer before mapping it.
    /// @param size  Byte count, which must not exceed the storage the resource has.
    virtual void SetData(const void* pData, size_t size)
    {
        // A `VkBuffer` is fixed-size, so growing used to free and re-create it - leaving every descriptor
        // set and device address that named it pointing at the old handle (#13). Allocate with headroom
        // and reuse the handle while the request fits; only a request past the capacity reallocates, and
        // that is the case a caller has to re-bind for.
        if (m_buffer == VK_NULL_HANDLE || size > m_nCapacity)
        {
            const size_t nNewCapacity = RoundUpCapacity(std::max(size, static_cast<size_t>(m_nCapacity) * 2));
            if (m_buffer != VK_NULL_HANDLE)
            {
                GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
                m_buffer = VK_NULL_HANDLE;
            }
            AllocateOwnedBuffer(nNewCapacity);
        }
        m_nSize = size;

        // A null source means "size the buffer, upload nothing". `memcpy(dst, nullptr, n)` is undefined for
        // `n > 0`, and the assert that used to stand here was both false for the intended reserve call and
        // removed by `NDEBUG`, so the release build copied from null (#191). Skipping the copy states the
        // contract instead: a caller that has data passes it, and the UI pass sizes then maps.
        if ((BUFFER_USAGE & VK_BUFFER_USAGE_TRANSFER_DST_BIT) && pData != nullptr)
        {
            // Create staging buffer
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VmaAllocation stagingAllocation = VK_NULL_HANDLE;
            GetMemoryAllocator()->AllocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                 stagingBuffer, stagingAllocation);

            void* pMappedMemory = nullptr;
            GetMemoryAllocator()->MapBuffer(stagingAllocation, &pMappedMemory);
            memcpy(pMappedMemory, pData, size);
            GetMemoryAllocator()->UnmapBuffer(stagingAllocation);

            // Submit the copy immedietly
            GetRenderDevice()->ExecuteImmediateCommand(
                [&](VkCommandBuffer commandBuffer)
                {
                    VkBufferCopy copyRegion = {};
                    copyRegion.size = size;
                    vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_buffer, 1, &copyRegion);
#ifdef FEATURE_RAY_TRACING
                    // Make sure the copy of the instance buffer are copied before triggering the
                    // acceleration structure build
                    // Note: assign by name - the sync2 barrier structs are not laid out the way a
                    // positional initializer would assume.
                    VkMemoryBarrier2 barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
                    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                    VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    dependencyInfo.memoryBarrierCount = 1;
                    dependencyInfo.pMemoryBarriers = &barrier;
                    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
#endif
                });

            GetMemoryAllocator()->FreeBuffer(stagingBuffer, stagingAllocation);
        }
        else
        {
            if (pData != nullptr)
            {
                void* pMappedMemory = nullptr;
                GetMemoryAllocator()->MapBuffer(m_allocation, &pMappedMemory);
                memcpy(pMappedMemory, pData, size);
                GetMemoryAllocator()->UnmapBuffer(m_allocation);
            }
        }
    }

    /// Map this resource's memory on the host.
    ///
    /// Virtual because a resource's memory is not always its own: a render-graph transient is bound into
    /// a shared block, so what has to be mapped is the block, at an offset. The base implementation maps
    /// the resource's own allocation, which is what everything that allocates its own memory wants.
    /// @return A host pointer to the start of this resource's storage.
    virtual void* Map()
    {
        void* pMappedPointer;
        GetMemoryAllocator()->MapBuffer(m_allocation, &pMappedPointer);
        return pMappedPointer;
    }
    /// Release a mapping taken by `Map`. Must be called before the resource is destroyed - and, for an
    /// aliased resource, before the block it shares is freed.
    virtual void Unmap() { GetMemoryAllocator()->UnmapBuffer(m_allocation); }

    size_t GetSize() const { return m_nSize; }
    /// @return The bytes actually allocated, which is at least `GetSize()`: a buffer grown by `SetData`
    ///         keeps its allocation, so a repeat upload of the same order never re-creates the handle.
    size_t GetCapacity() const { return m_nCapacity; }
    /// @return The Vulkan usage flags the buffer was created with.
    VkBufferUsageFlags GetBufferUsage() const { return BUFFER_USAGE; }
    /// @return The VMA memory usage the buffer was created with.
    VmaMemoryUsage GetMemoryUsage() const { return MEMORY_USAGE; }

protected:
    /// Allocate this resource's own storage and record its capacity, so a later `SetData` reuses the
    /// handle instead of freeing the buffer a descriptor set or device address already names (#13).
    /// @param size Bytes to allocate.
    void AllocateOwnedBuffer(size_t size)
    {
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation);
        m_nCapacity = size;
    }
    /// @param size     Bytes to allocate.
    /// @param name     Debug name and VMA user data.
    /// @param poolType Pool to allocate from: SBT and BVH need their own alignment and cannot be shared.
    void AllocateOwnedBuffer(size_t size, const std::string& name, PoolType poolType = PoolType::Default)
    {
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation, name, poolType);
        m_nCapacity = size;
    }

    /// @param size Requested capacity in bytes.
    /// @return The same size rounded up to a small alignment, so consecutive uploads of the same order
    ///         reuse the allocation instead of re-creating it. Over-allocation is at most 255 bytes.
    static size_t RoundUpCapacity(size_t size)
    {
        constexpr size_t nAlignment = 256;
        return (size + nAlignment - 1) & ~(nAlignment - 1);
    }

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    size_t m_nSize = 0;
    size_t m_nCapacity = 0;
    const VkBufferUsageFlags BUFFER_USAGE = 0x0;
    const VmaMemoryUsage MEMORY_USAGE = VMA_MEMORY_USAGE_UNKNOWN;
};

// Ray tracing related buffers
class AccelerationStructureBuffer : public BufferResource
{
public:
    AccelerationStructureBuffer(VkDeviceSize size)
        : BufferResource(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY)
    {
        AllocateOwnedBuffer(size, "AccelerationStrucutre", PoolType::BVH);
    }

    AccelerationStructureBuffer(const void* pData, uint32_t size)
        : BufferResource(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY)
    {
        AllocateOwnedBuffer(size, "AccelerationStrucutre", PoolType::BVH);
        SetData(pData, size);
    }
};

class AccelerationStructure : public IRenderResource
{
public:
    /// A heap acceleration-structure descriptor requires the structure's device address to be a multiple
    /// of 256 (`VUID-VkResourceDescriptorInfoEXT-type-11454`), and the AS create-info offset must itself be
    /// a multiple of 256 (`VUID-VkAccelerationStructureCreateInfoKHR-offset-03734`). The device's
    /// `minAccelerationStructureScratchOffsetAlignment` can be smaller (llvmpipe reports 8), so the BVH
    /// allocation in `VkMemoryAllocator` requests 256-byte alignment and the structure sits at offset 0.
    AccelerationStructure(VkDeviceSize size, VkAccelerationStructureTypeKHR type) : m_accelerationStructureBuffer(size)
    {
        VkAccelerationStructureCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.type = type;
        createInfo.size = size;
        createInfo.offset = 0;
        createInfo.buffer = m_accelerationStructureBuffer.buffer();
        VkExt::vkCreateAccelerationStructureKHR(GetRenderDevice()->GetDevice(), &createInfo, nullptr,
                                                &m_accelerationStructure);
    }
    virtual ~AccelerationStructure() override
    {
        VkExt::vkDestroyAccelerationStructureKHR(GetRenderDevice()->GetDevice(), m_accelerationStructure, nullptr);
    }

    virtual VkObjectType GetVkObjectType() const override { return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR; }

    /// @return The device address used to write this acceleration structure into a descriptor heap.
    VkDeviceAddress GetDeviceAddress() const
    {
        VkAccelerationStructureDeviceAddressInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        info.accelerationStructure = m_accelerationStructure;
        return VkExt::vkGetAccelerationStructureDeviceAddressKHR(GetRenderDevice()->GetDevice(), &info);
    }

    virtual void SetDebugName(const std::string& sName) const override
    {
        m_accelerationStructureBuffer.SetDebugName(sName);
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_accelerationStructure), GetVkObjectType(), sName.c_str());
    }

    const VkAccelerationStructureKHR& GetAccelerationStructure() const { return m_accelerationStructure; }

    /// @return The buffer the structure's memory lives in.
    ///
    /// A `vkCmdPipelineBarrier2` has no barrier type for an acceleration structure, and its memory *is* this
    /// buffer's - so a queue-family ownership handover is expressed as a buffer barrier on it, which is the
    /// mechanism the specification recommends for a cross-queue transfer of a structure (#153).
    VkBuffer GetBackingBuffer() const { return m_accelerationStructureBuffer.buffer(); }

    VkDeviceAddress GetAccelerationStructureAddress() const
    {
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, m_accelerationStructure};
        return VkExt::vkGetAccelerationStructureDeviceAddressKHR(GetRenderDevice()->GetDevice(), &addressInfo);
    }

private:
    AccelerationStructureBuffer m_accelerationStructureBuffer;
    VkAccelerationStructureKHR m_accelerationStructure = VK_NULL_HANDLE;
};

class ShaderBindingTableBuffer : public BufferResource
{
public:
    ShaderBindingTableBuffer(uint32_t size)
        : BufferResource(VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU)
    {
        AllocateOwnedBuffer(size, "SBT", PoolType::SBT);
    }
};

template <class T>
class StorageBuffer : public BufferResource
{
public:
    StorageBuffer(const T* buffer, uint32_t nNumStructs, bool bAllowReadback = false)
        : BufferResource(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         bAllowReadback ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY)

    {
        uint32_t nSize = sizeof(T) * nNumStructs;
        AllocateOwnedBuffer(nSize, "Storage Buffer");
        SetData(static_cast<const void*>(buffer), nSize);
        m_nNumStructs = nNumStructs;
    }
    uint32_t GetNumStructs() const { return m_nNumStructs; }

private:
    uint32_t m_nNumStructs = 0;
};
}  // namespace Muyo
