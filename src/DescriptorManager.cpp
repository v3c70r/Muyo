#include "DescriptorManager.h"


#include "VkRenderDevice.h"
#include "Debug.h"
#include "SamplerManager.h"
#include <cassert>

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
    // GBuffer layouts
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            getPerViewDataBinding(0), getSamplerBinding(1)};

        descriptorSetLayoutInfo.bindingCount = (uint32_t)bindings.size();
        descriptorSetLayoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                           &descriptorSetLayoutInfo, nullptr,
                                           &layout) == VK_SUCCESS);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(layout),
                                VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                                "Gbuffer");
        m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_GBUFFER] = layout;
    }

    // GBuffer lighting layout
    {
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
        descriptorSetLayoutInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            getPerViewDataBinding(0),       // Pre view data
            getSamplerArrayBinding(1, 4),   // gbuffers
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
}

void DescriptorManager::destroyDescriptorSetLayouts()
{
    for (auto& descriptorSetLayout : m_aDescriptorSetLayouts)
    {
        vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(),
                                     descriptorSetLayout, nullptr);
    }
}

VkDescriptorSet DescriptorManager::allocateGBufferDescriptorSet(
    const UniformBuffer<PerViewData>& perViewData, VkImageView textureView)
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

    // Prepare buffer descriptor
    {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = perViewData.buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(PerViewData);

        // prepare image descriptor
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = textureView;
        imageInfo.sampler = GetSamplerManager()->getSampler(SAMPLER_FULL_SCREEN_TEXTURE);

        std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(),
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
    return descriptorSet;
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
    allocInfo.pSetLayouts = &m_aDescriptorSetLayouts[DESCRIPTOR_LAYOUT_LIGHTING];

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
            GetSamplerManager()->getSampler(SAMPLER_FULL_SCREEN_TEXTURE),
            position,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // albedo
            GetSamplerManager()->getSampler(SAMPLER_FULL_SCREEN_TEXTURE),
            albedo,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // normal
            GetSamplerManager()->getSampler(SAMPLER_FULL_SCREEN_TEXTURE),
            normal,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            // UV
            GetSamplerManager()->getSampler(SAMPLER_FULL_SCREEN_TEXTURE),
            uv,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = static_cast<uint32_t>(imageInfos.size());
        descriptorWrites[1].pImageInfo = imageInfos.data();

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(),
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
    return descriptorSet;
}

DescriptorManager* GetDescriptorManager() { return &descriptorManager; }

