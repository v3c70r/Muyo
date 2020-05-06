#pragma once
#include <vk_mem_alloc.h>

class IRenderResource
{
public:
    virtual ~IRenderResource(){};
};

class ImageResource: public IRenderResource
{
public:
    VkImageView getView() const { return m_view; }
    VkImage getImage() const { return m_image; }

protected:
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
};


