#include "DescriptorManager.h"

#include <cassert>

#include "Debug.h"
#include "SamplerManager.h"
#include "VkRenderDevice.h"

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
    assert(vkCreateDescriptorPool(GetRenderDevice()->GetDevice(), &poolInfo,
                                  nullptr, &m_descriptorPool) == VK_SUCCESS);
}

void DescriptorManager::destroyDescriptorPool()
{
    vkDestroyDescriptorPool(GetRenderDevice()->GetDevice(), m_descriptorPool,
                            nullptr);
}

void DescriptorManager::createDescriptorSetLayouts()
{
    // GBuffer lighting layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            getPerViewDataBinding(0),      // Pre view data
            getSamplerArrayBinding(1, 4),  // gbuffers
        };
        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) == VK_SUCCESS);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "Lighting");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_LIGHTING] = layout;
    }

    // Single sampler descriptor set layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            getSamplerBinding(0),
        };
        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;

        assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) == VK_SUCCESS);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "Single sampler");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_SINGLE_SAMPLER] = layout;
    }

    // Per view layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            getPerViewDataBinding(0)};

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) == VK_SUCCESS);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "PerViewData");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_PER_VIEW_DATA] = layout;
    }

    // Material layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            getSamplerArrayBinding(
                0, Material::TEX_COUNT)};  // PBR material textures

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) == VK_SUCCESS);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "Material");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_MATERIALS] = layout;
    }

    // GBuffer layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = {
            getSamplerArrayBinding(
                0, RenderPassGBuffer::LightingAttachments::
                       GBUFFER_ATTACHMENTS_COUNT)};  // GBuffer textures

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) == VK_SUCCESS);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "GBuffer");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_GBUFFER] = layout;
    }
}

void DescriptorManager::destroyDescriptorSetLayouts()
{
    for (auto& descriptorSetLayout : m_aDescriptorSetLayouts)
    {
        vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                     descriptorSetLayout, nullptr);
    }
}

VkDescriptorSet DescriptorManager::allocateMaterialDescriptorSet()
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts =
        &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_MATERIALS];
    assert(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "Material");

    return descriptorSet;
}

VkDescriptorSet DescriptorManager::allocateMaterialDescriptorSet(
    const Material::PBRViews& textureViews)
{
    VkDescriptorSet descSet = allocateMaterialDescriptorSet();
    updateMaterialDescriptorSet(descSet, textureViews);
    return descSet;
}

void DescriptorManager::updateMaterialDescriptorSet(
    VkDescriptorSet descriptorSet, const Material::PBRViews& textureViews)
{
    assert(textureViews.size() == Material::TEX_COUNT);

    // prepare image descriptor
    std::array<VkDescriptorImageInfo, Material::TEX_COUNT> imageInfos = {
        // albedo
        GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
        textureViews.at(Material::TEX_ALBEDO),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        // normal
        GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
        textureViews.at(Material::TEX_NORMAL),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        // metalness
        GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
        textureViews.at(Material::TEX_METALNESS),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        // roughness
        GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
        textureViews.at(Material::TEX_ROUGHNESS),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        // AO
        GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
        textureViews.at(Material::TEX_AO),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = static_cast<uint32_t>(imageInfos.size());
    descriptorWrite.pImageInfo = imageInfos.data();

    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), 1, &descriptorWrite,
                           0, nullptr);
}

VkDescriptorSet DescriptorManager::allocateLightingDescriptorSet(
    const UniformBuffer<PerViewData>& perViewData, VkImageView position,
    VkImageView albedo, VkImageView normal, VkImageView uv)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts =
        &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_LIGHTING];

    assert(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) == VK_SUCCESS);

    // Prepare buffer descriptor
    {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = perViewData.buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(PerViewData);

        std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // prepare image descriptor
        std::array<VkDescriptorImageInfo, 4> imageInfos = {
            // position
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            position,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // albedo
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            albedo,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // normal
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            normal,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // UV
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            uv,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount =
            static_cast<uint32_t>(imageInfos.size());
        descriptorWrites[1].pImageInfo = imageInfos.data();

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(),
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
    return descriptorSet;
}

VkDescriptorSet DescriptorManager::allocateGBufferDescriptorSet()
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_GBUFFER];

    assert(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "GBuffer");

    return descriptorSet;
}

VkDescriptorSet DescriptorManager::allocateGBufferDescriptorSet(
    const RenderPassGBuffer::GBufferViews& gbufferViews)
{
    VkDescriptorSet descriptorSet = allocateGBufferDescriptorSet();
    updateGBufferDescriptorSet(descriptorSet, gbufferViews);
    return descriptorSet;
}

void DescriptorManager::updateGBufferDescriptorSet(
    VkDescriptorSet descriptorSet,
    const RenderPassGBuffer::GBufferViews& gbufferViews)

{
    VkWriteDescriptorSet descriptorWrite = {};
    // prepare image descriptor
    std::array<
        VkDescriptorImageInfo,
        RenderPassGBuffer::LightingAttachments::GBUFFER_ATTACHMENTS_COUNT>
        imageInfos = {
            // position
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            gbufferViews[0],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // albedo
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            gbufferViews[1],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // normal
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            gbufferViews[2],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // UV
            GetSamplerManager()->getSampler(SAMPLER_1_MIPS),
            gbufferViews[3],
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = static_cast<uint32_t>(imageInfos.size());
    descriptorWrite.pImageInfo = imageInfos.data();

    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), 1, &descriptorWrite,
                           0, nullptr);
}

VkDescriptorSet DescriptorManager::allocateSingleSamplerDescriptorSet(
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

    assert(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet),
                            VK_OBJECT_TYPE_DESCRIPTOR_SET, "Single Sampler");

    // Prepare buffer descriptor
    {
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
        descriptorWrite.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), 1,
                               &descriptorWrite, 0, nullptr);
    }
    return descriptorSet;
}

VkDescriptorSet DescriptorManager::allocatePerviewDataDescriptorSet(
    const UniformBuffer<PerViewData>& perViewData)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts =
        &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_PER_VIEW_DATA];

    assert(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo,
                                    &descriptorSet) == VK_SUCCESS);

    // Bind uniform buffer to descriptor
    {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = perViewData.buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(PerViewData);
        std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(),
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    return descriptorSet;
}

DescriptorManager* GetDescriptorManager() { return &descriptorManager; }

