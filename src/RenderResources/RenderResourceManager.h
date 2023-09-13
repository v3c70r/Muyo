#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "Debug.h"
#include "RenderTargetResource.h"
#include "StorageImageResource.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "VertexBuffer.h"
#include "DrawCommandBuffer.h"
namespace Muyo
{

class RenderResourceManager
{
    using ResourceMap = std::unordered_map<std::string, std::unique_ptr<IRenderResource>>;

public:
    void Initialize(){};

    void Unintialize() { m_mResources.clear(); }

    template <typename T>
    VertexBuffer<T>* GetVertexBuffer(const std::string& sName, const std::vector<T>& vVertexData, bool bStagedUpoload = true)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<VertexBuffer<T>>(vVertexData, bStagedUpoload);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<VertexBuffer<T>*>(m_mResources[sName].get());
    }

    template <class IndexType>
    IndexBuffer* GetIndexBuffer(const std::string& sName, const std::vector<IndexType> vIndexData, bool bStagedUpoload = true)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<IndexBuffer>(vIndexData, bStagedUpoload);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<IndexBuffer*>(m_mResources[sName].get());
    }

    RenderTarget* GetRenderTarget(const std::string& sName, bool bColorTarget,
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

    TextureResource* GetTexture(const std::string sName, const std::string path)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<TextureResource>();
            static_cast<TextureResource*>(m_mResources[sName].get())->LoadImage(path);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<TextureResource*>(m_mResources[sName].get());
    }

    TextureResource* GetTexture(const std::string sName, void* pixelData, int nWidth, int nHeight)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<TextureResource>();
            static_cast<TextureResource*>(m_mResources[sName].get())->LoadPixels(pixelData, nWidth, nHeight);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<TextureResource*>(m_mResources[sName].get());
    }

    RenderTarget* GetDepthTarget(const std::string sName, VkExtent2D extent,
                                 VkFormat format = VK_FORMAT_D32_SFLOAT)
    {
        return GetRenderTarget(sName, false, extent, format);
    }

    RenderTarget* GetColorTarget(
        const std::string sName, VkExtent2D extent,
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT, uint32_t numMips = 1,
        uint32_t numLayers = 1, VkImageUsageFlags nAdditionalUsageFlags = 0)
    {
        return GetRenderTarget(sName, true, extent, format, numMips, numLayers,
                               nAdditionalUsageFlags);
    }

    RenderTarget* GetColorTarget(const std::string sName)
    {
        return static_cast<RenderTarget*>(m_mResources[sName].get());
    }

    void RemoveResource(const std::string sName)
    {
        if (auto it = m_mResources.find(sName); it != m_mResources.end())
        {
            m_mResources.erase(it);
        }
    }
    const ResourceMap& GetResourceMap() { return m_mResources; }

    template <class T>
    UniformBuffer<T>* GetUniformBuffer(const std::string sName)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<UniformBuffer<T>>();
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<UniformBuffer<T>*>(m_mResources[sName].get());
    }

    template <class T>
    DrawCommandBuffer<T>* GetDrawCommandBuffer(const std::string sName, const std::vector<T>& drawCommands)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<DrawCommandBuffer<T>>(drawCommands.data(), (uint32_t)drawCommands.size());
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<DrawCommandBuffer<T>*>(m_mResources[sName].get());
    }

    template <class T>
    StorageBuffer<T>* GetStorageBuffer(const std::string sName, const std::vector<T>& structuredBuffers)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<StorageBuffer<T>>(structuredBuffers.data(), (uint32_t)structuredBuffers.size());
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<StorageBuffer<T>*>(m_mResources[sName].get());
    }

    AccelerationStructureBuffer* GetAccelerationStructureBuffer(
        const std::string& sName, VkDeviceSize nSize)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] =
                std::make_unique<AccelerationStructureBuffer>(nSize);
            m_mResources[sName]->SetDebugName(sName);
        }

        return static_cast<AccelerationStructureBuffer*>(
            m_mResources[sName].get());
    }

    AccelerationStructure* CreateTLAS(
        const std::string& sName, VkDeviceSize nSize)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<AccelerationStructure>(nSize, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<AccelerationStructure*>(m_mResources[sName].get());
    }

    AccelerationStructure* CreateBLAS(
        const std::string& sName, VkDeviceSize nSize)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] = std::make_unique<AccelerationStructure>(nSize, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
            m_mResources[sName]->SetDebugName(sName);
        }
        return static_cast<AccelerationStructure*>(m_mResources[sName].get());
    }
    AccelerationStructureBuffer* GetAccelerationStructureBuffer(
        const std::string& sName, const void* pData, VkDeviceSize nSize)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] =
                std::make_unique<AccelerationStructureBuffer>(pData, nSize);
            m_mResources[sName]->SetDebugName(sName);
        }

        return static_cast<AccelerationStructureBuffer*>(
            m_mResources[sName].get());
    }

    StorageImageResource* GetStorageImageResource(const std::string& sName, VkExtent2D extent, VkFormat format)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] =
                std::make_unique<StorageImageResource>(format, extent.width, extent.height);
            m_mResources[sName]->SetDebugName(sName);
        }

        return static_cast<StorageImageResource*>(m_mResources[sName].get());
    }

    ShaderBindingTableBuffer* GetShaderBindingTableBuffer(
        const std::string& sName, VkDeviceSize nSize)
    {
        if (m_mResources.find(sName) == m_mResources.end())
        {
            m_mResources[sName] =
                std::make_unique<ShaderBindingTableBuffer>(nSize);
            m_mResources[sName]->SetDebugName(sName);
        }

        return static_cast<ShaderBindingTableBuffer*>(
            m_mResources[sName].get());
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

    // Get an allocated resource
    template <class T>
    T* GetResource(const std::string& sName)
    {
        if (m_mResources.find(sName) != m_mResources.end())
        {
            return dynamic_cast<T*>(m_mResources[sName].get());
        }
        else
        {
            return nullptr;
        }
    }

protected:
    ResourceMap m_mResources;
};

RenderResourceManager* GetRenderResourceManager();
}  // namespace Muyo
