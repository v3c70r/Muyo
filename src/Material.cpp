#include "Material.h"

#include "DescriptorManager.h"
static MaterialManager s_materialManager;

void MaterialManager::CreateDefaultMaterial()
{
    if (m_mMaterials.find(sDefaultName) == m_mMaterials.end())
    {
        // Load materials
        GetMaterialManager()->m_mMaterials[sDefaultName] =
            std::make_unique<Material>();
        GetMaterialManager()->m_mMaterials[sDefaultName]->loadTexture(
            Material::TEX_ALBEDO, "assets/Materials/white5x5.png",
            "defaultAlbedo");

        GetMaterialManager()->m_mMaterials[sDefaultName]->loadTexture(
            Material::TEX_METALNESS, "assets/Materials/white5x5.png",
            "defaultMetalness");

        GetMaterialManager()->m_mMaterials[sDefaultName]->loadTexture(
            Material::TEX_NORMAL, "assets/Materials/white5x5.png",
            "defaultNormal");

        GetMaterialManager()->m_mMaterials[sDefaultName]->loadTexture(
            Material::TEX_ROUGHNESS, "assets/Materials/white5x5.png",
            "defaultRoughness");

        GetMaterialManager()->m_mMaterials[sDefaultName]->loadTexture(
            Material::TEX_AO, "assets/Materials/white5x5.png",
            "defaultOcclusion");
        GetMaterialManager()
            ->m_mMaterials[sDefaultName]
            ->AllocateDescriptorSet();
    }
}
MaterialManager *GetMaterialManager()
{
    return &s_materialManager;
}

void Material::AllocateDescriptorSet()
{
    if (m_descriptorSet == VK_NULL_HANDLE)
    {
        const Material::PBRViews vPrimitiveMaterialViews = {
            getImageView(
                Material::TEX_ALBEDO),
            getImageView(
                Material::TEX_NORMAL),
            getImageView(
                Material::TEX_METALNESS),
            getImageView(
                Material::TEX_ROUGHNESS),
            getImageView(Material::TEX_AO),
        };
        m_descriptorSet = GetDescriptorManager()->allocateMaterialDescriptorSet(vPrimitiveMaterialViews);
    }
}

VkDescriptorSet Material::GetDescriptorSet() const
{
    return m_descriptorSet;
}
