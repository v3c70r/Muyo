#pragma once
#include <vk_mem_alloc.h>

// Wrap allocator
class VkAllocator
{
public:
    VkAllocator();
    ~VkAllocator();
private:
    VmaAllocator *m_pAllocator = nullptr;
};

VkAllocator* GetAllocator();

