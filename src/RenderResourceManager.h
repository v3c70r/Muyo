#pragma once
#include "DepthResource.h"
#include <unordered_map>
#include <string>
#include <memory>

class RenderResourceManager
{
public:
    DepthResource* getDepthResource(const std::string name, VkExtent2D extent)
    {
        if (m_mResources.find(name) == m_mResources.end())
        {
            m_mResources[name] = std::make_unique<DepthResource>(extent.width, extent.height);
        }
        return static_cast<DepthResource*>(m_mResources[name].get());
    }
    void removeResource(const std::string name)
    {
        if (auto it = m_mResources.find(name); it != m_mResources.end())
        {
            m_mResources.erase(it);
        }
    }
protected:
    using ResourceMap = std::unordered_map<std::string, std::unique_ptr<IRenderResource>>;
    ResourceMap m_mResources;
};

RenderResourceManager* GetRenderResourceManager();
