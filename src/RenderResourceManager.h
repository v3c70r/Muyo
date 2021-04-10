#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "Debug.h"
#include "RenderTargetResource.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "ImageStorageResource.h"

class RenderResourceManager
{
    using ResourceMap =
        std::unordered_map<std::string, std::unique_ptr<IRenderResource>>;

public:
    void Initialize(){};

    void Unintialize() { m_mResources.clear(); }

    RenderTarget* getRenderTarget(const std::string sName, bool bColorTarget,
                                  VkExtent2D extent, VkFormat format,
                                  uint32_t numMips = 1, uint32_t numLayers = 1,
                                  VkImageUsageFlags nAdditionalUsageFlags = 0)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            VkImageUsageFlags nUsageFlags = nAdditionalUsageFlags;
            m_mResources[sName] = std::make_unique<RenderTarget>(
                format,
                nUsageFlags |
                    (bColorTarget
                         ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
                         : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT),
                extent.width, extent.height, numMips, numLayers);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<RenderTarget*>(m_mResources[sName].get());
    }

    Texture* getTexture(const std::string sName, const std::string path)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<Texture>();
            static_cast<Texture*>(m_mResources[sName].get())->LoadImage(path);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<Texture*>(m_mResources[sName].get());
    }

    RenderTarget* getDepthTarget(const std::string sName, VkExtent2D extent,
                                 VkFormat format = VK_FORMAT_D32_SFLOAT)
    {
        return getRenderTarget(sName, false, extent, format);
    }

    RenderTarget* getColorTarget(
        const std::string sName, VkExtent2D extent,
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT, uint32_t numMips = 1,
        uint32_t numLayers = 1, VkImageUsageFlags nAdditionalUsageFlags = 0)
    {
        return getRenderTarget(sName, true, extent, format, numMips, numLayers,
                               nAdditionalUsageFlags);
    }

    RenderTarget* getColorTarget(const std::string sName)
    {
        return static_cast<RenderTarget*>(m_mResources[sName].get());
    }

    void removeResource(const std::string sName)
    {
        if (auto it = m_mResources.find(sName); it != m_mResources.end())
        {
            m_mResources.erase(it);
        }
    }
    const ResourceMap& getResourceMap() { return m_mResources; }

    template <class T>
    UniformBuffer<T>* getUniformBuffer(const std::string sName)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<UniformBuffer<T>>();
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<UniformBuffer<T>*>(m_mResources[sName].get());
    }

    AccelerationStructureBuffer* GetAccelerationStructureBuffer(
        const std::string& sName, uint32_t size)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] =
                std::make_unique<AccelerationStructureBuffer>(size);
            m_mResources[sName]->SetDebugName(sName);
        }

        return static_cast<AccelerationStructureBuffer*>(
            m_mResources[sName].get());
    }

    AccelerationStructureBuffer* GetAccelerationStructureBuffer(
        const std::string& sName, const void* pData, uint32_t size)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] =
                std::make_unique<AccelerationStructureBuffer>(pData, size);
            m_mResources[sName]->SetDebugName(sName);
        }

        return static_cast<AccelerationStructureBuffer*>(
            m_mResources[sName].get());
    }

    ImageStorageResource* GetStorageImageResource(const std::string& sName, VkExtent2D extent, VkFormat format)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] =
                std::make_unique<ImageStorageResource>(format, extent.width, extent.height);
            m_mResources[sName]->SetDebugName(sName);
        }

        return static_cast<ImageStorageResource*>(m_mResources[sName].get());
    }

    // Swap an existing resource with a new pointer
    template <class T>
    void AssignResource(const std::string& sName, T* pResource)
    {
        // Make sure the swapped resource is the same type of resource
        if (m_mResources.find(sName) != m_mResources.end())
        {
            if (dynamic_cast<T*>(pResource) != nullptr)
            {
                m_mResources[sName] = std::unique_ptr<T>(pResource);
                m_mResources[sName]->SetDebugName(sName);
            }
        }
    }

protected:
    ResourceMap m_mResources;
};

RenderResourceManager* GetRenderResourceManager();
