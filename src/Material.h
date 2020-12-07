#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

#include "Texture.h"
#include "UniformBuffer.h"

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

    struct PBRFactors
    {
        float m_aBaseColorFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float m_fMetalicFactor = 1.0;
        float m_fRoughnessFactor = 1.0;
        uint32_t m_aUVIndices[TEX_COUNT] = {0, 0, 0, 0, 0};
        float m_fPadding1 = 0.0;
    };
    struct MaterialParameters
    {
        std::array<Texture *, TEX_COUNT> m_apTextures;
        UniformBuffer<PBRFactors> *m_pFactors = nullptr;
    };

public:
    void loadTexture(TextureTypes type, const std::string& path, const std::string& name)
    {
        const auto it = GetTextureManager()->m_vpTextures.find(name);
        if (it == GetTextureManager()->m_vpTextures.end())
        {
            GetTextureManager()->m_vpTextures[name] = std::make_unique<Texture>();
            GetTextureManager()->m_vpTextures[name]->LoadImage(path);
            GetTextureManager()->m_vpTextures[name]->setName(name);
            m_materialParameters.m_apTextures[type] = GetTextureManager()->m_vpTextures[name].get();
        }
        else
        {
            m_materialParameters.m_apTextures[type] = it->second.get();
        }
    }
    VkImageView getImageView(TextureTypes type) const
    {
        return m_materialParameters.m_apTextures[type]->getView();
    }
    void SetMaterialParameterFactors(const PBRFactors &factors, const std::string &sMaterialName);

    VkDescriptorSet GetDescriptorSet() const;
    void AllocateDescriptorSet();

private:
    MaterialParameters m_materialParameters;
    const std::array<std::string, TEX_COUNT> m_aNames = {
        "TEX_ALBEDO", "TEX_NORMAL", "TEX_METALNESS", "TEX_ROUGHNESS", "TEX_AO"};
    VkDescriptorSet m_descriptorSet;
};

class MaterialManager
{
public:
    void destroyMaterials() { m_mMaterials.clear(); }
    std::unordered_map<std::string, std::unique_ptr<Material>> m_mMaterials;
    void CreateDefaultMaterial();
    const Material* GetDefaultMaterial() {return m_mMaterials[sDefaultName].get();}
private:
    const std::string sDefaultName = "default";
};

MaterialManager* GetMaterialManager();
