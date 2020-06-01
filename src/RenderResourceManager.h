#pragma once
#include "RenderTargetResource.h"
#include "Debug.h"

#include <memory>
#include <string>
#include <unordered_map>


class RenderResourceManager
{

    using ResourceMap =
        std::unordered_map<std::string, std::unique_ptr<IRenderResource>>;
public:
    void Initialize(){};

    void Unintialize() { m_mResources.clear(); }

    RenderTarget* getRenderTarget(const std::string name, bool bColorTarget,
                                  VkExtent2D extent, VkFormat format, uint32_t numMips = 1, uint32_t numLayers = 1)
    {
        if (m_mResources.find(name) == m_mResources.end())
        {
            m_mResources[name] = std::make_unique<RenderTarget>(
                format,
                bColorTarget ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                             : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                extent.width, extent.height, numMips, numLayers);

            setDebugUtilsObjectName(
                reinterpret_cast<uint64_t>(
                    static_cast<ImageResource*>(m_mResources[name].get())
                        ->getImage()),
                VK_OBJECT_TYPE_IMAGE, name.c_str());
        }
        return static_cast<RenderTarget*>(m_mResources[name].get());
    }

    RenderTarget* getDepthTarget(const std::string name, VkExtent2D extent,
                                 VkFormat format = VK_FORMAT_D32_SFLOAT)
    {
        return getRenderTarget(name, false, extent, format);
    }

    RenderTarget* getColorTarget(const std::string name, VkExtent2D extent,
                                 VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT)
    {
        return getRenderTarget(name, true, extent, format);
    }

    void removeResource(const std::string name)
    {
        if (auto it = m_mResources.find(name); it != m_mResources.end())
        {
            m_mResources.erase(it);
        }
    }
    const ResourceMap& getResourceMap() {return m_mResources;}

protected:
    ResourceMap m_mResources;
};

RenderResourceManager* GetRenderResourceManager();
