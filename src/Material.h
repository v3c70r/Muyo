#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

#include "Texture.h"
#include "UniformBuffer.h"
#include "SharedStructures.h"

class Material
{

public:
    enum TextureType
    {
        TEX_ALBEDO,
        TEX_NORMAL,
        TEX_METALNESS,
        TEX_ROUGHNESS,
        TEX_AO,
        TEX_EMISSIVE,
        TEX_COUNT
    };

    struct MaterialParameters
    {
        std::array<const TextureResource *, TEX_COUNT> m_apTextures;
        std::array<uint32_t, TEX_COUNT> m_aTextureIndices;
        UniformBuffer<PBRFactors> *m_pFactors = nullptr;
    };

public:
    VkDeviceAddress GetPBRFactorsDeviceAdd() const
    {
        return GetRenderDevice()->GetBufferDeviceAddress(m_materialParameters.m_pFactors->buffer());
    }

    void LoadTexture(TextureType type, const std::string& path, const std::string& name)
    {
        m_materialParameters.m_apTextures[type] = GetTextureResourceManager()->CreateAndLoadOrGetTexture(name, path);
        m_materialParameters.m_aTextureIndices[type] = GetTextureResourceManager()->GetTextureIndex(name);
    }

    void FillPbrTextureIndices(std::array<uint32_t, TEX_COUNT>& aIndices) const
    {
        aIndices = m_materialParameters.m_aTextureIndices;
    }

    void SetMaterialParameterFactors(const PBRFactors &factors, const std::string &sMaterialName);

    VkDescriptorSet GetDescriptorSet() const;
    void AllocateDescriptorSet();

    bool IsTransparent() const { return m_bIsTransparent; }
    void SetTransparent() { m_bIsTransparent = true; }
    void SetOpaque() { m_bIsTransparent = false; }

private:
    MaterialParameters m_materialParameters;
    const std::array<std::string, TEX_COUNT> m_aNames = {
        "TEX_ALBEDO", "TEX_NORMAL", "TEX_METALNESS", "TEX_ROUGHNESS", "TEX_AO", "TEX_EMISSIVE"};
    VkDescriptorSet m_descriptorSet;
    bool m_bIsTransparent = false;
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
