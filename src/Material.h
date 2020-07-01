#pragma once
#include <array>
#include <unordered_map>
#include <string>
#include <memory>
#include <vulkan/vulkan.h>
#include "Texture.h"

class Material
{
public:

    enum TextureTypes
    {
        TEX_ALBEDO,
        TEX_NORMAL,
        TEX_METALNESS,
        TEX_ROUGHNESS,
        TEX_AO,
        TEX_COUNT
    };
    using PBRViews = std::array<VkImageView, TEX_COUNT>;
    void loadTexture(TextureTypes type, const std::string& path)
    {
        m_aTextures[type].LoadImage(path);
    }
    VkImageView getImageView(TextureTypes type) const
    {
        return m_aTextures[type].getView();
    }
private:
    std::array<Texture, TEX_COUNT> m_aTextures;
};

class MaterialManager
{
public:
    void destroyMaterials()
    {
        m_mMaterials.clear();
    }
    std::unordered_map<std::string, std::unique_ptr<Material>> m_mMaterials;
private:
};



MaterialManager *GetMaterialManager();
