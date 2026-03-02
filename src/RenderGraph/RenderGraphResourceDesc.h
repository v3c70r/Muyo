#pragma once
#include "RenderResource.h"
#include "RenderResourceManager.h"
#include "RenderTargetResource.h"
#include "Utils.h"
#include <concepts>
#include <string_view>
#include <variant>
#include <MeshVertex.h>

// Note
// this is messy template playground.
// I'm experimenting with using concepts and ADL to create a flexible resource allocation system for the render graph.


template <class D>
concept GraphResourceDesc = requires(const D& d, Muyo::RenderResourceManager* rs)
{
    { AllocateImp(d, rs) } -> std::convertible_to<Muyo::IRenderResource*>;
    //{GetDescName(d)} -> std::convertible_to<std::string_view>;
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
    // Add this helper trait before your usage:
    template <typename>
    struct is_buffer_desc : std::false_type {};

    template <typename T>
    struct BufferDesc {
        using value_type = T;
        size_t count;
        VkBufferUsageFlags usage;
        VmaMemoryUsage memoryProperties;
        static constexpr size_t STRIDE = sizeof(T);
        auto operator<=>(const BufferDesc&) const = default;
    };

    template<typename T>
    inline Muyo::BufferResource* AllocateImp(const BufferDesc<T>& d, Muyo::RenderResourceManager* renderResourceManager)
    {
        //return renderResourceManager->GetBuffer(d.name, d.count * d.STRIDE, d.usage, d.memoryProperties);
    }

    // Buffer descriptor trait
    template <typename U>
    struct is_buffer_desc<BufferDesc<U>> : std::true_type {};


    template<typename T>
        struct BufferDescHasher {
            size_t operator()(const BufferDesc<T>& d) const {
                size_t seed = 0;
                Muyo::HashCombine(seed, d.count);
                Muyo::HashCombine(seed, d.usage);
                Muyo::HashCombine(seed, d.memoryProperties);
                Muyo::HashCombine(seed, sizeof(T));
                return seed;
            }
        };

    struct RenderTargetDesc
    {
        std::string name;
        VkExtent2D extent{ 0, 0 };
        VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
        uint32_t numMips{1};
        uint32_t numLayers{1};
        VkImageUsageFlags usage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    };
    inline Muyo::RenderTarget* AllocateImp(const RenderTargetDesc& d, Muyo::RenderResourceManager* renderResourceManager)
    {
        //return renderResourceManager->GetRenderTarget(d.name, d.extent, d.format, d.numMips, d.numLayers, d.usage);
    }


    
       // TODO(qgu): Probalby need to generate known types during compile time
    using ResourceDesc = std::variant<
        BufferDesc<uint8_t>, 
        BufferDesc<uint16_t>,
        BufferDesc<Vertex>
        >;
        //RenderTargetDesc >;

    // Hasher
    struct ResourceDescHasher {
            size_t operator()(const ResourceDesc& desc) const {
                return std::visit([](const auto& d) -> size_t {
                    // Using a specialized hasher for each type in the variant
                    using T = std::decay_t<decltype(d)>;
                    if constexpr (is_buffer_desc<T>::value) {
                        return BufferDescHasher<typename T::value_type>{}(d);
                    } else {
                        // This covers IndexBufferDesc, VertexBufferDesc, StorageBufferDesc
                        return BufferDescHasher<typename T::value_type>{}(d);
                    }
                }, desc);
            }
        };

    template <GraphResourceDesc T>
    constexpr uint32_t GetDescriptorCount(const T&) { return 1;}

    template <GraphResourceDesc T>
    constexpr VkDescriptorType GetDescriptorType(const T&) 
    {
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

}
