#include "Material.h"
#include "DescriptorManager.h"
static MaterialManager s_materialManager;
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
