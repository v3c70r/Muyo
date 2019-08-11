#include "VkAllocator.h"
#include "VkRenderDevice.h"
VkAllocator::VkAllocator()
{
    VkRenderDevice* device = GetRenderDevice();
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = device->GetPhysicalDevice();
    allocatorInfo.device = device->GetDevice();
    vmaCreateAllocator(&allocatorInfo, m_pAllocator);
}

VkAllocator::~VkAllocator()
{
    vmaDestroyAllocator(*m_pAllocator);
}

static VkAllocator allocator;
VkAllocator* getAllocator()
{
    return &allocator;
}
