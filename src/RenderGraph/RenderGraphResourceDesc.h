#pragma once
#include "RenderResource.h"
#include "RenderResourceManager.h"
#include <concepts>
#include <string_view>
#include <variant>
#include <MeshVertex.h>

template <class D>
concept GraphResourceDesc = requires(const D& d, Muyo::RenderResourceManager* rs)
{
    //{ d.name } -> std::convertible_to<std::string_view>;
    { AllocateImp(d, rs) } -> std::convertible_to<Muyo::IRenderResource*>;
};

template <GraphResourceDesc D>
auto Allocate(const D& d, Muyo::RenderResourceManager* renderResourceManager) {
    return AllocateImp(d, renderResourceManager); // unqualified call → ADL finds it
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
    inline Muyo::IndexBuffer* AllocateImp(const IndexBufferDesc<T>& d, Muyo::RenderResourceManager* renderResourceManager)
    {
        return renderResourceManager->GetIndexBuffer<T>(d.name, std::vector<T>(d.count));
    }

    template<class T>
    struct VertexBufferDesc
    {
        std::string name;
        size_t count{};
        static constexpr size_t STRIDE = sizeof(T);
    };

    template<class T>
    inline Muyo::VertexBuffer<T>* AllocateImp(const VertexBufferDesc<T>& d, Muyo::RenderResourceManager* renderResourceManager)
    {
        return renderResourceManager->GetVertexBuffer<T>(d.name, std::vector<T>(d.count));
    }

    template <class T>
    struct StorageBufferDesc
    {
        std::string name;
        size_t count{};
        static constexpr size_t STRIDE = sizeof(T);
    };

    template <class T>
    inline Muyo::StorageBuffer<T>* AllocateImp(const StorageBufferDesc<T>& d,
                                               Muyo::RenderResourceManager* renderResourceManager)
    {
        return renderResourceManager->GetStorageBuffer<T>(d.name, std::vector<T>(d.count));
    }

    // TODO(qgu): Probalby need to generate known types during compile time
    using ResourceDesc = std::variant<
        IndexBufferDesc<uint8_t>, 
        IndexBufferDesc<uint16_t>, 
        VertexBufferDesc<Muyo::Vertex>, 
        VertexBufferDesc<Muyo::UIVertex>,
        StorageBufferDesc<uint8_t> >;
}

// Example usage:
// Muyo::RenderGraph::VertexBufferDesc<Muyo::Vertex> vbDesc{"MyVertexBuffer", 1000};
// auto* vertexBuffer = Allocate(vbDesc, renderResourceManager);
// Muyo::RenderGraph::IndexBufferDesc<uint32_t> ibDesc{"MyIndexBuffer", 3000};
// auto* indexBuffer = Allocate(ibDesc, renderResourceManager);
