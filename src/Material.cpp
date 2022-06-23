#include "Material.h"

#include "DescriptorManager.h"
#include "RenderResourceManager.h"
static MaterialManager s_materialManager;

// Material Manager
void MaterialManager::CreateDefaultMaterial()
{
    if (m_mMaterials.find(sDefaultName) == m_mMaterials.end())
    {
        GetMaterialManager()->m_mMaterials[sDefaultName] = std::make_unique<Material>();
        Material* pMaterial = GetMaterialManager()->m_mMaterials[sDefaultName].get();

        // Load PBR textures
        pMaterial->LoadTexture( Material::TEX_ALBEDO, "assets/Materials/white5x5.png", "defaultAlbedo"); 
        pMaterial->LoadTexture( Material::TEX_METALNESS, "assets/Materials/white5x5.png", "defaultMetalness"); 
        pMaterial->LoadTexture( Material::TEX_NORMAL, "assets/Materials/white5x5.png", "defaultNormal"); 
        pMaterial->LoadTexture( Material::TEX_ROUGHNESS, "assets/Materials/white5x5.png", "defaultRoughness");
        pMaterial->LoadTexture( Material::TEX_AO, "assets/Materials/white5x5.png", "defaultOcclusion");
        pMaterial->LoadTexture( Material::TEX_EMISSIVE, "assets/Materials/white5x5.png", "defaultOcclusion");

        // Load PBR Factors
        Material::PBRFactors pbrFactors;
        pMaterial->SetMaterialParameterFactors(pbrFactors, sDefaultName);

        pMaterial->AllocateDescriptorSet();
    }
}
MaterialManager *GetMaterialManager()
{
    return &s_materialManager;
}

void Material::SetMaterialParameterFactors(const PBRFactors &factors, const std::string &sMaterialName)
{
    auto* pUniformBuffer = GetRenderResourceManager()->getUniformBuffer<PBRFactors>(sMaterialName);
    pUniformBuffer->SetData(factors);
    m_materialParameters.m_pFactors = pUniformBuffer;
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
