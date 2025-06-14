#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace Hash
{
constexpr uint64_t StrFnV1A64(std::string_view str)
{
    uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (char c : str)
    {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}
}

namespace Muyo
{
//-------------------------------------
// Texture Description
//-------------------------------------
struct TextureDesc
{
    uint32_t m_width = 1;
    uint32_t m_height = 1;
    uint32_t m_depth = 1;  // for 3D textures
    uint32_t m_mipLevels = 1;
    uint32_t m_arrayLayers = 1;
    VkFormat m_format = VK_FORMAT_R8G8B8A8_UNORM;

    VkImageUsageFlags m_usage = 0;
    VkImageType m_imageType = VK_IMAGE_TYPE_2D;
    VkImageTiling m_tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageViewType m_viewType = VK_IMAGE_VIEW_TYPE_2D;

    std::string m_debugName;

    bool operator==(const TextureDesc& other) const;
};

//-------------------------------------
// Buffer Description
//-------------------------------------
struct BufferDesc
{
    uint64_t m_size = 0;
    VkBufferUsageFlags m_usage = 0;
    VkMemoryPropertyFlags m_memoryProperties = 0;  // e.g., device local, host visible
    std::string m_debugName;

    bool operator==(const BufferDesc& other) const;
};

//-------------------------------------
// Abstract resource handle
//-------------------------------------
enum class ResourceType : uint8_t
{
    TEXTURE,
    BUFFER
};

struct ResourceDesc
{
    ResourceType m_type;
    union
    {
        TextureDesc m_texture;
        BufferDesc m_buffer;
    };

    ResourceDesc() = delete;
    static ResourceDesc MakeTexture(const TextureDesc& desc);
    static ResourceDesc MakeBuffer(const BufferDesc& desc);
};

class RenderGraphResourceHandle
{
public:
    explicit RenderGraphResourceHandle(std::string_view name)
        : m_name(name), m_nameHash(Hash::StrFnV1A64(name))
    {
    }
    bool operator==(const RenderGraphResourceHandle& other) const {
        return m_nameHash == other.m_nameHash && m_nVersion == other.m_nVersion;
    }
private:
    const std::string m_name;
    const uint64_t m_nameHash;
    uint32_t m_nVersion = 0;
};

};  // namespace Muyo
