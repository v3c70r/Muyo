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
    void loadTexture(TextureTypes type, const std::string& path, const std::string& name)
    {
        const auto it = GetTextureManager()->m_vpTextures.find(name);
        if (it == GetTextureManager()->m_vpTextures.end())
        {
            GetTextureManager()->m_vpTextures[name] = std::make_unique<Texture>();
            GetTextureManager()->m_vpTextures[name]->LoadImage(path);
            GetTextureManager()->m_vpTextures[name]->setName(name);
            m_apTextures[type] = GetTextureManager()->m_vpTextures[name].get();
        }
        else
        {
            m_apTextures[type] = it->second.get();
        }
    }
    VkImageView getImageView(TextureTypes type) const
    {
        return m_apTextures[type]->getView();
    }
    VkDescriptorSet GetDescriptorSet() const;
    void AllocateDescriptorSet();

private:
    std::array<Texture*, TEX_COUNT> m_apTextures;
    const std::array<std::string, TEX_COUNT> m_aNames = {
        "TEX_ALBEDO", "TEX_NORMAL", "TEX_METALNESS", "TEX_ROUGHNESS", "TEX_AO"};
    VkDescriptorSet m_descriptorSet;
};

// TODO: Allocate textuer at material level so we don't exhaust descriptor count

class MaterialManager
{
public:
    void destroyMaterials() { m_mMaterials.clear(); }
    std::unordered_map<std::string, std::unique_ptr<Material>> m_mMaterials;

private:
};

MaterialManager* GetMaterialManager();
