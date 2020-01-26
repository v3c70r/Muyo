#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"
#include <cassert>
VkMemoryAllocator::VkMemoryAllocator()
{}
void VkMemoryAllocator::Initalize(VkRenderDevice* pDevice)
{
    assert(pDevice && "Device is not yet initialized");
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = pDevice->GetPhysicalDevice();
    allocatorInfo.device = pDevice->GetDevice();
    m_pAllocator = std::make_unique<VmaAllocator>();
    vmaCreateAllocator(&allocatorInfo, m_pAllocator.get());
}

VkMemoryAllocator::~VkMemoryAllocator()
{
    if (m_pAllocator)
    {
        vmaDestroyAllocator(*m_pAllocator);
    }
}

static VkMemoryAllocator allocator;
VkMemoryAllocator* GetMemoryAllocator()
{
    return &allocator;
}
