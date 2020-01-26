#pragma once
#include <vk_mem_alloc.h>
#include <memory>
class VkRenderDevice;

// Wrap allocator
class VkMemoryAllocator
{
public:
    void Initalize(VkRenderDevice *pDevice);
    VkMemoryAllocator();
    ~VkMemoryAllocator();
private:
    std::unique_ptr<VmaAllocator> m_pAllocator = nullptr;
};

VkMemoryAllocator* GetMemoryAllocator();

