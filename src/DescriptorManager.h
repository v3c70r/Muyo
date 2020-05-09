#pragma once
#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "UniformBuffer.h"

// 1. Create descpritor set pool
// 2. Create descriptor layout
// 3. Allocate a descriptor from bool using the layout

class DescriptorManager
{
    void createDescriptorPool();
    void destroyDescriptorPool();
    void createDescriptorSetLayouts();
    void destroyDescriptorSetLayouts();
    VkDescriptorSet allocateGBufferDescriptorSet(
        const UniformBuffer<PerViewData>& perViewData, VkImageView textureView);
    VkDescriptorSet allocateLightingDescriptorSet(
        const UniformBuffer<PerViewData>& perViewData, VkImageView position,
        VkImageView albedo, VkImageView Normal, VkImageView UV);

private:
    static VkDescriptorSetLayoutBinding getPerViewDataBinding(uint32_t binding)
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = binding;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.pImmutableSamplers = nullptr;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        return uboLayoutBinding;
    }

    static VkDescriptorSetLayoutBinding getSamplerBinding(uint32_t binding)
    {
        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = binding;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        return samplerLayoutBinding;
    }

    std::unordered_map<std::string, VkDescriptorSetLayout>
        m_mDescriptorSetLayouts;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    const uint32_t DESCRIPTOR_COUNT_EACH_TYPE = 100;
    const std::vector<VkDescriptorPoolSize> POOL_SIZES = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, DESCRIPTOR_COUNT_EACH_TYPE},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, DESCRIPTOR_COUNT_EACH_TYPE}};
};
DescriptorManager& GetDescriptorManager();
