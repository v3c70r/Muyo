#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#include "SharedStructures.h"
#include "Texture.h"
#include "UniformBuffer.h"
namespace Muyo
{

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
        std::array<const TextureResource*, TEX_COUNT> m_apTextures;
        std::array<uint32_t, TEX_COUNT> m_aTextureIndices;
        UniformBuffer<PBRMaterial>* m_pFactors = nullptr;
    };

public:
    explicit Material(uint32_t nMaterialIndex) : m_nMaterialIndex(nMaterialIndex) {}
    VkDeviceAddress GetPBRMaterialDeviceAdd() const
    {
        return GetRenderDevice()->GetBufferDeviceAddress(m_materialParameters.m_pFactors->buffer());
    }

    Material& LoadTexture(TextureType type, const std::string& path, const std::string& name);
    Material& SetMaterialParameterFactors(const PBRMaterial& factors, const std::string& sMaterialName);

    void FillPbrTextureIndices(std::array<uint32_t, TEX_COUNT>& aIndices) const
    {
        aIndices = m_materialParameters.m_aTextureIndices;
    }


    void FillPbrTextureIndices(uint32_t* aIndices) const
    {
        memcpy((uint32_t*)(m_materialParameters.m_aTextureIndices.data()), aIndices, sizeof(uint32_t) * TEX_COUNT);
    }


    VkDescriptorSet GetDescriptorSet() const;
    void AllocateDescriptorSet();

    bool IsTransparent() const { return m_bIsTransparent; }
    void SetTransparent() { m_bIsTransparent = true; }
    void SetOpaque() { m_bIsTransparent = false; }

    const uint32_t GetMaterialIndex() const
    {
        return m_nMaterialIndex;
    }

private:
    MaterialParameters m_materialParameters;
    const std::array<std::string, TEX_COUNT> m_aNames = {
        "TEX_ALBEDO", "TEX_NORMAL", "TEX_METALNESS", "TEX_ROUGHNESS", "TEX_AO", "TEX_EMISSIVE"};
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    bool m_bIsTransparent = false;

    uint32_t m_nMaterialIndex = 0;      // Material index in material resource manager
};

class MaterialManager
{
    friend class Material;
public:
    void DestroyMaterials() { m_vMaterials.clear(); }
    void CreateDefaultMaterial();
    Material& GetOrCreateMaterial(const std::string sMaterialName);
    const Material& GetMaterial(uint32_t index) { return m_vMaterials[index]; }
    const Material& GetDefaultMaterial() { return m_vMaterials[nDefaultMaterialIndex]; }

    bool HasMaterial(const std::string sMaterialName);

private:

    const std::string sDefaultName = "default";
    uint32_t nDefaultMaterialIndex = 0;


    // Track material buffer
    std::unordered_map<std::string, uint32_t> m_mMaterialIndexMap;   // Map material name to position in m_vMaterials

    std::vector<Material> m_vMaterials;

    std::vector<PBRMaterial> m_vMaterialBufferCPU;
    StorageBuffer<PBRMaterial>* m_vMaterialBuffer;

};

MaterialManager* GetMaterialManager();
}  // namespace Muyo
