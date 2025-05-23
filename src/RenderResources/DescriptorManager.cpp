#include "DescriptorManager.h"

#include <vulkan/vulkan_core.h>

#include <cassert>

#include "Debug.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "VkRenderDevice.h"

namespace Muyo
{

// Poor man's singletone
static DescriptorManager descriptorManager;

void DescriptorManager::createDescriptorPool()
{
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(POOL_SIZES.size());
    poolInfo.pPoolSizes = POOL_SIZES.data();
    poolInfo.maxSets =
        DESCRIPTOR_COUNT_EACH_TYPE * static_cast<uint32_t>(POOL_SIZES.size());
    VK_ASSERT(vkCreateDescriptorPool(GetRenderDevice()->GetDevice(), &poolInfo,
                                  nullptr, &m_descriptorPool) );
}

void DescriptorManager::destroyDescriptorPool()
{
    vkDestroyDescriptorPool(GetRenderDevice()->GetDevice(), m_descriptorPool,
                            nullptr);
}

void DescriptorManager::createDescriptorSetLayouts()
{
    // Single sampler descriptor set layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            GetSamplerBinding(0
#ifdef FEATURE_RAY_TRACING
                              ,
                              VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR
#endif
                              ),
        };
        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;

        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) );

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "Single sampler");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_SINGLE_SAMPLER] = layout;
    }

    {
        // Single storage image
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            GetBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL)};

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;

        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) );

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "Single storage image");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_SIGNLE_STORAGE_IMAGE] = layout;
    }

    // Per view layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            GetUniformBufferBinding(0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
#ifdef FEATURE_RAY_TRACING
                                           | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
#endif
                                    )};

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) );

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "PerViewData");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_PER_VIEW_DATA] = layout;
    }

    // Per obj layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            GetUniformBufferBinding(0)};  // VERTEX and FRAGMENT !?

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) );

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "PerObjData");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_PER_OBJ_DATA] = layout;
    }

    // Material layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            GetSamplerArrayBinding(0, Material::TEX_COUNT),
            GetUniformBufferBinding(1, VK_SHADER_STAGE_FRAGMENT_BIT)};  // PBR material

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) );

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "Material");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_MATERIALS] = layout;
    }

    // IBL layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 3> bindings = {
            GetSamplerBinding(0,
                              VK_SHADER_STAGE_FRAGMENT_BIT
#ifdef FEATURE_RAY_TRACING
                                  | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
#endif
                              ),  // Irradiance map
            GetSamplerBinding(1, VK_SHADER_STAGE_FRAGMENT_BIT
#ifdef FEATURE_RAY_TRACING
                                     | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
#endif
                              ),  // prefiltered environment
            GetSamplerBinding(2, VK_SHADER_STAGE_FRAGMENT_BIT
#ifdef FEATURE_RAY_TRACING
                                     | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
#endif
                              )};  // specular brdf lut

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) );

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "IBL");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_IBL] = layout;
    }

    // Light Data
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            GetBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT
#ifdef FEATURE_RAY_TRACING
                                                                    | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
#endif
                       ),
            GetBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT
#ifdef FEATURE_RAY_TRACING
                                                                    | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
#endif
                       ),
        };

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) );

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "light data");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_LIGHT_DATA] = layout;
    }
}

void DescriptorManager::destroyDescriptorSetLayouts()
{
    for (auto& descriptorSetLayout : m_aDescriptorSetLayouts)
    {
        vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(), descriptorSetLayout, nullptr);
    }
}

VkDescriptorSet DescriptorManager::AllocateMaterialDescriptorSet()
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts =
        &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_MATERIALS];
    VK_ASSERT(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) );

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "Material");

    return descriptorSet;
}

VkDescriptorSet DescriptorManager::AllocateMaterialDescriptorSet(const Material::MaterialParameters& materialParameters)
{
    VkDescriptorSet descSet = AllocateMaterialDescriptorSet();
    UpdateMaterialDescriptorSet(descSet, materialParameters);
    return descSet;
}

void DescriptorManager::UpdateMaterialDescriptorSet(VkDescriptorSet descriptorSet, const Material::MaterialParameters& materialParameters)
{
    assert(materialParameters.m_apTextures.size() == Material::TEX_COUNT);

    // prepare image array info for descriptor
    std::array<VkDescriptorImageInfo, Material::TEX_COUNT> imageInfos = {};
    for (size_t i = 0; i < Material::TEX_COUNT; i++)
    {
        imageInfos[i] = {
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            materialParameters.m_apTextures[i]->getView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }

    // Two writes, one for PBR texture array, another for PBR factors
    std::array<VkWriteDescriptorSet, 2> aWriteDescriptorSets = {};
    {
        // Material sampler views
        VkWriteDescriptorSet& writeDescriptorSet = aWriteDescriptorSets[0];
        writeDescriptorSet = {};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = static_cast<uint32_t>(imageInfos.size());
        writeDescriptorSet.pImageInfo = imageInfos.data();
    }

    // Material parameters
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = materialParameters.m_pFactors->buffer();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(PBRMaterial);
    {
        VkWriteDescriptorSet& writeDescriptorSet = aWriteDescriptorSets[1];
        writeDescriptorSet = {};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 1;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.pBufferInfo = &bufferInfo;
    }

    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), static_cast<uint32_t>(aWriteDescriptorSets.size()), aWriteDescriptorSets.data(),
                           0, nullptr);
}

VkDescriptorSet DescriptorManager::AllocateSingleSamplerDescriptorSet(
    VkImageView textureView)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts =
        &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_SINGLE_SAMPLER];

    VK_ASSERT(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) );

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "Single Sampler");

    UpdateSingleSamplerDescriptorSet(descriptorSet, textureView);

    return descriptorSet;
}

void DescriptorManager::UpdateSingleSamplerDescriptorSet(VkDescriptorSet& descriptorSet, VkImageView textureView)
{
    // Prepare buffer descriptor
    // prepare image descriptor
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureView;
    imageInfo.sampler = GetSamplerManager()->getSampler(SAMPLER_1_MIPS);

    VkWriteDescriptorSet descriptorWrite = {};

    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), 1,
                           &descriptorWrite, 0, nullptr);
}

VkDescriptorSet DescriptorManager::AllocateSingleStorageImageDescriptorSet(VkImageView imageView)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_SIGNLE_STORAGE_IMAGE];

    VK_ASSERT(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) );

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "Single storage image");

    // Prepare buffer descriptor
    {
        // prepare image descriptor
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = imageView;

        VkWriteDescriptorSet descriptorWrite = {};

        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), 1,
                               &descriptorWrite, 0, nullptr);
    }
    return descriptorSet;
}

VkDescriptorSet DescriptorManager::AllocatePerviewDataDescriptorSet(
    const UniformBuffer<PerViewData>& perViewData)
{
    return AllocateUniformBufferDescriptorSet(perViewData, 0, m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_PER_VIEW_DATA]);
}

VkDescriptorSet DescriptorManager::AllocateIBLDescriptorSet()
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts =
        &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_IBL];
    VK_ASSERT(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) );

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "IBL");

    return descriptorSet;
}
VkDescriptorSet DescriptorManager::AllocateIBLDescriptorSet(
    VkImageView irradianceMap,
    VkImageView prefilteredEnvMap,
    VkImageView specularBrdfLutMap)
{
    VkDescriptorSet descSet = AllocateIBLDescriptorSet();
    UpdateIBLDescriptorSet(descSet, irradianceMap, prefilteredEnvMap, specularBrdfLutMap);
    return descSet;
}

void DescriptorManager::UpdateIBLDescriptorSet(
    VkDescriptorSet& descriptorSet,
    VkImageView irradianceMap,
    VkImageView prefilteredEnvMap,
    VkImageView specularBrdfLutMap)
{
    // prepare image descriptor
    std::array<VkImageView, 3> aIBLViews = {irradianceMap, prefilteredEnvMap, specularBrdfLutMap};
    std::array<VkDescriptorImageInfo, 3> imageInfos;
    std::array<VkWriteDescriptorSet, 3> writeDescriptorSets;

    for (uint32_t i = 0; i < aIBLViews.size(); i++)
    {
        VkDescriptorImageInfo& imageInfo = imageInfos[i];
        // Needs 8 mips for prefiltered env map
        imageInfo = {GetSamplerManager()->getSampler(i == 1 ? SAMPLER_8_MIPS : SAMPLER_1_MIPS), aIBLViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        VkWriteDescriptorSet& writeDescriptorSet = writeDescriptorSets[i];
        writeDescriptorSet = {};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = i;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.pImageInfo = &imageInfo;
    }
    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

VkDescriptorSet DescriptorManager::AllocateLightDataDescriptorSet(uint32_t nNumLights, const StorageBuffer<LightData>& lightData)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_LIGHT_DATA];
    VK_ASSERT(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) );

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "Light Data");
    UpdateRayLightDataDescriptorSet(descriptorSet, nNumLights, lightData);
    return descriptorSet;
}

void DescriptorManager::UpdateRayLightDataDescriptorSet(VkDescriptorSet descriptorSet, uint32_t nNumLights, const StorageBuffer<LightData>& lightData)
{
    // Allocate a single buffer for number of lights
    UniformBuffer<uint32_t>* numLightsBuffer = GetRenderResourceManager()->GetUniformBuffer<uint32_t>("num lights");
    numLightsBuffer->SetData(nNumLights);

    std::array<VkWriteDescriptorSet, 2> writes;

    // Num of lights
    VkDescriptorBufferInfo numLightBufferInfo = {};
    numLightBufferInfo.buffer = numLightsBuffer->buffer();
    numLightBufferInfo.offset = 0;
    numLightBufferInfo.range = sizeof(uint32_t);

    writes[0] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext = nullptr;
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &numLightBufferInfo;

    // Light buffer
    VkDescriptorBufferInfo lightDataBufferInfo = {};
    lightDataBufferInfo.buffer = lightData.buffer();
    lightDataBufferInfo.offset = 0;
    lightDataBufferInfo.range = lightData.GetSize();

    writes[1] = {};
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].pNext = nullptr;
    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &lightDataBufferInfo;

    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

size_t DescriptorManager::GetImGuiTextureId(const std::string& sResourceName)
{
    if (m_mImGuiTextureIds.find(sResourceName) == m_mImGuiTextureIds.end())
    {
        TextureResource* pTexture = GetRenderResourceManager()->GetResource<TextureResource>(sResourceName);
        if (pTexture)
        {
            // Allocate a descriptor for this resource.
            m_mImGuiTextureIds[sResourceName] = m_vImGuiTextureDescriptorSets.size();
            m_vImGuiTextureDescriptorSets.push_back(GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(pTexture->getView()));
        }
    }
    else
    {
        // Update descriptor set each time we get the texture id, This is because the resource could be destroyed and recreated.
        // It's a little bit overkill but not too bad for UI render.
        // We should only update the descriptoer set when the texture view is changed on the resource.
        // This update takes 0.01ms on GTX 1070 card. Not cheap
        VkImageView textureView = GetRenderResourceManager()->GetResource<TextureResource>(sResourceName)->getView();
        GetDescriptorManager()->UpdateSingleSamplerDescriptorSet(m_vImGuiTextureDescriptorSets[m_mImGuiTextureIds[sResourceName]], textureView);
    }
    return m_mImGuiTextureIds[sResourceName];
}

DescriptorManager* GetDescriptorManager() { return &descriptorManager; }

}  // namespace Muyo
