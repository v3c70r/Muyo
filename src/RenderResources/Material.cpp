#include "Material.h"

#include "DescriptorManager.h"
#include "RenderResourceManager.h"

namespace Muyo
{

static MaterialManager s_materialManager;

Material &MaterialManager::GetOrCreateMaterial(const std::string sMaterialName)
{
    if (HasMaterial(sMaterialName))
    {
        return m_vMaterials[m_mMaterialIndexMap[sMaterialName]];
    }
    else
    {
        uint32_t index = m_vMaterials.size();
        assert(m_vMaterials.size() == m_vMaterials.size());

        m_mMaterialIndexMap[sMaterialName] = index;

        m_vMaterials.emplace_back(index);
        m_vMaterialBufferCPU.push_back({});
        return m_vMaterials.back();
    }
}

bool MaterialManager::HasMaterial(const std::string sMaterialName)
{
    return m_mMaterialIndexMap.find(sMaterialName) != m_mMaterialIndexMap.end();
}

// Material Manager
void MaterialManager::CreateDefaultMaterial()
{
    if (!HasMaterial(sDefaultName))
    {
        Material &material = GetOrCreateMaterial(sDefaultName);

        PBRMaterial pbrMaterial;
        material
            .SetMaterialParameterFactors(pbrMaterial, sDefaultName)
            .LoadTexture(Material::TEX_ALBEDO, "assets/Materials/white5x5.png", "defaultAlbedo")
            .LoadTexture(Material::TEX_METALNESS, "assets/Materials/white5x5.png", "defaultMetalness")
            .LoadTexture(Material::TEX_NORMAL, "assets/Materials/white5x5.png", "defaultNormal")
            .LoadTexture(Material::TEX_ROUGHNESS, "assets/Materials/white5x5.png", "defaultRoughness")
            .LoadTexture(Material::TEX_AO, "assets/Materials/white5x5.png", "defaultOcclusion")
            .LoadTexture(Material::TEX_EMISSIVE, "assets/Materials/white5x5.png", "defaultOcclusion");
        material.AllocateDescriptorSet();
    }
    
}

void MaterialManager::UploadMaterialBuffer() const
{
    GetRenderResourceManager()->GetStorageBuffer(sMaterialBufferName, m_vMaterialBufferCPU);
}

const StorageBuffer<PBRMaterial>* MaterialManager::GetMaterialBuffer() const
{
    return GetRenderResourceManager()->GetResource<StorageBuffer<PBRMaterial>>(sMaterialBufferName);
}

MaterialManager *GetMaterialManager()
{
    return &s_materialManager;
}

Material &Material::LoadTexture(TextureType type, const std::string &path, const std::string &name)
{
    m_materialParameters.m_apTextures[type] = GetTextureResourceManager()->CreateAndLoadOrGetTexture(name, path);
    uint32_t nTextureIndex = GetTextureResourceManager()->GetTextureIndex(name);

    m_materialParameters.m_aTextureIndices[type] = nTextureIndex;
    GetMaterialManager()->m_vMaterialBufferCPU[m_nMaterialIndex].textureIds[type] = nTextureIndex;

    return *this;
}

Material &Material::SetMaterialParameterFactors(const PBRMaterial &factors, const std::string &sMaterialName)
{
    auto *pUniformBuffer = GetRenderResourceManager()->GetUniformBuffer<PBRMaterial>(sMaterialName);
    pUniformBuffer->SetData(factors);
    m_materialParameters.m_pFactors = pUniformBuffer;

    return *this;
}

void Material::AllocateDescriptorSet()
{
    if (m_descriptorSet == VK_NULL_HANDLE)
    {
        m_descriptorSet = GetDescriptorManager()->AllocateMaterialDescriptorSet(m_materialParameters);
    }
}

VkDescriptorSet Material::GetDescriptorSet() const
{
    return m_descriptorSet;
}

}  // namespace Muyo
