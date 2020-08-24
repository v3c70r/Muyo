#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

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
        m_aTextures[type].setName(m_aNames[type]);
    }
    VkImageView getImageView(TextureTypes type) const
    {
        return m_aTextures[type].getView();
    }

private:
    std::array<Texture, TEX_COUNT> m_aTextures;
    const std::array<std::string, TEX_COUNT> m_aNames = {
        "TEX_ALBEDO", "TEX_NORMAL", "TEX_METALNESS", "TEX_ROUGHNESS", "TEX_AO"};
};

class MaterialManager
{
public:
    void destroyMaterials() { m_mMaterials.clear(); }
    std::unordered_map<std::string, std::unique_ptr<Material>> m_mMaterials;

private:
};

MaterialManager* GetMaterialManager();
