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
        pMaterial->loadTexture( Material::TEX_ALBEDO, "assets/Materials/white5x5.png", "defaultAlbedo"); 
        pMaterial->loadTexture( Material::TEX_METALNESS, "assets/Materials/white5x5.png", "defaultMetalness"); 
        pMaterial->loadTexture( Material::TEX_NORMAL, "assets/Materials/white5x5.png", "defaultNormal"); 
        pMaterial->loadTexture( Material::TEX_ROUGHNESS, "assets/Materials/white5x5.png", "defaultRoughness");
        pMaterial->loadTexture( Material::TEX_AO, "assets/Materials/white5x5.png", "defaultOcclusion");

        // Load PBR Factors
        Material::PBRFactors pbrFactors = 
        {
            glm::vec4(1.0, 1.0, 1.0, 1.0), // Base colro factor
            0.0,    // Metalness
            1.0,    // Roughness
            0.0, 0.0    // Padding
        };
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
    pUniformBuffer->setData(factors);
    m_materialParameters.m_pFactors = pUniformBuffer;
}

void Material::AllocateDescriptorSet()
{
    if (m_descriptorSet == VK_NULL_HANDLE)
    {
        m_descriptorSet = GetDescriptorManager()->allocateMaterialDescriptorSet(m_materialParameters);
    }
}

VkDescriptorSet Material::GetDescriptorSet() const
{
    return m_descriptorSet;
}
