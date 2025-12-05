#pragma once
#include "RenderResource.h"
#include "RenderResourceManager.h"
#include "RenderTargetResource.h"
#include <concepts>
#include <string_view>
#include <variant>
#include <MeshVertex.h>

template <class D>
concept GraphResourceDesc = requires(const D& d, Muyo::RenderResourceManager* rs)
{
    { AcquireImp(d, rs) } -> std::convertible_to<Muyo::IRenderResource*>;
    //{GetDescName(d)} -> std::convertible_to<std::string_view>;
};

template <GraphResourceDesc D>
auto Allocate(const D& d, Muyo::RenderResourceManager* renderResourceManager) {
    return AcquireImp(d, renderResourceManager); // unqualified call → ADL finds it
}

template <GraphResourceDesc D>
constexpr std::string_view GetDescName(const D& d)
{
    return std::string_view(d.name);
}

namespace Muyo::RenderGraph
{
    template<class T>
    struct IndexBufferDesc
    {
        std::string name;
        size_t count{};
        static constexpr size_t STRIDE = sizeof(T);
    };
    template <class T>
    inline Muyo::IndexBuffer* AcquireImp(const IndexBufferDesc<T>& d, Muyo::RenderResourceManager* renderResourceManager)
    {
        return renderResourceManager->GetIndexBuffer<T>(d.name, std::vector<T>(d.count));
    }

    template <class T>
    VkDescriptorType GetDescriptorType(const IndexBufferDesc<T>&) {
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // or appropriate type
    }


    template<class T>
    struct VertexBufferDesc
    {
        std::string name;
        size_t count{};
        static constexpr size_t STRIDE = sizeof(T);
    };

    template<class T>
    inline Muyo::VertexBuffer<T>* AcquireImp(const VertexBufferDesc<T>& d, Muyo::RenderResourceManager* renderResourceManager)
    {
        return renderResourceManager->GetVertexBuffer<T>(d.name, std::vector<T>(d.count));
    }
    template <class T>
    VkDescriptorType GetDescriptorType(const VertexBuffer<T>&) {
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // or appropriate type
    }

    template <class T>
    struct StorageBufferDesc
    {
        std::string name;
        size_t count{};
        bool allowReadback{false};
        static constexpr size_t STRIDE = sizeof(T);
    };

    template <class T>
    inline Muyo::StorageBuffer<T>* AcquireImp(const StorageBufferDesc<T>& d,
                                               Muyo::RenderResourceManager* renderResourceManager)
    {
        return renderResourceManager->GetStorageBuffer<T>(d.name, std::vector<T>(d.count));
    }
    template <class T>
    VkDescriptorType GetDescriptorType(const StorageBufferDesc<T>&) {
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    struct RenderTargetDesc
    {
        std::string name;
        VkExtent2D extent{ 0, 0 };
        VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
        uint32_t numMips{1};
        uint32_t numLayers{1};
        VkImageUsageFlags usage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    };
    inline Muyo::RenderTarget* AcquireImp(const RenderTargetDesc& d, Muyo::RenderResourceManager* renderResourceManager)
    {
        return renderResourceManager->GetRenderTarget(d.name, d.extent, d.format, d.numMips, d.numLayers, d.usage);
    }

    // TODO(qgu): Probalby need to generate known types during compile time
    using ResourceDesc = std::variant<
        IndexBufferDesc<uint8_t>, 
        IndexBufferDesc<uint16_t>, 
        VertexBufferDesc<Muyo::Vertex>, 
        VertexBufferDesc<Muyo::UIVertex>,
        StorageBufferDesc<uint8_t>,
        RenderTargetDesc >;
}

// Example usage:
// Muyo::RenderGraph::VertexBufferDesc<Muyo::Vertex> vbDesc{"MyVertexBuffer", 1000};
// auto* vertexBuffer = Allocate(vbDesc, renderResourceManager);
// Muyo::RenderGraph::IndexBufferDesc<uint32_t> ibDesc{"MyIndexBuffer", 3000};
// auto* indexBuffer = Allocate(ibDesc, renderResourceManager);
