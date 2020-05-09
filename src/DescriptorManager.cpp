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

        std::array<VkDescriptorSetLayoutBinding, 5> bindings = {
            getPerViewDataBinding(0), 
            getSamplerBinding(1),   // Positions
            getSamplerBinding(2),   // Albedo
            getSamplerBinding(3),   // Normal
            getSamplerBinding(4)    // UV
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
        VkImageView albedo, VkImageView Normal, VkImageView UV)
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


        std::array<VkWriteDescriptorSet, 5> descriptorWrites = {};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        std::array<VkImageView*, 4> imageViews = {&position, &albedo, &Normal, &UV};
        // prepare image descriptor
        for (uint32_t i = 1; i <= 4; i++)
        {
            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = *imageViews[i - 1];

            imageInfo.sampler =
                GetSamplerManager()->getSampler(SAMPLER_FULL_SCREEN_TEXTURE);
            descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[i].dstSet = descriptorSet;
            descriptorWrites[i].dstBinding = i;
            descriptorWrites[i].dstArrayElement = 0;
            descriptorWrites[i].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[i].descriptorCount = 1;
            descriptorWrites[i].pImageInfo = &imageInfo;
        }

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(),
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
    return descriptorSet;
}

DescriptorManager* GetDescriptorManager() { return &descriptorManager; }

