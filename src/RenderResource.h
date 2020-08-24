#pragma once
#include <vk_mem_alloc.h>

#include "Debug.h"

class IRenderResource
{
public:
    virtual ~IRenderResource(){};
};

class ImageResource : public IRenderResource
{
public:
    VkImageView getView() const { return m_view; }
    VkImage getImage() const { return m_image; }
    void setName(const std::string& sName)
    {
        if (m_image != VK_NULL_HANDLE)
        {
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_image),
                                    VK_OBJECT_TYPE_IMAGE, sName.data());
        }
    }

protected:
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
};

