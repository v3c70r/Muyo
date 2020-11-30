#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <vector>

#include "Material.h"
#include "RenderPassGBuffer.h"
#include "UniformBuffer.h"

enum DescriptorLayoutType
{
    DESCRIPTOR_LAYOUT_LIGHTING,       // TODO: Remove this
    DESCRIPTOR_LAYOUT_SINGLE_SAMPLER, // A single sampler descriptor set layout
                                      // at binding 0
    DESCRIPTOR_LAYOUT_PER_VIEW_DATA,  // A layout contains mvp matrices at
                                      // binding 0
    DESCRIPTOR_LAYOUT_MATERIALS,      // A sampler array contains material textures
    DESCRIPTOR_LAYOUT_GBUFFER,        // Layouts contains output of GBuffer
    DESCRIPTOR_LAYOUT_COUNT,
};

class DescriptorManager
{
public:
    void createDescriptorPool();
    void destroyDescriptorPool();
    void createDescriptorSetLayouts();
    void destroyDescriptorSetLayouts();

    VkDescriptorSet allocateLightingDescriptorSet(
        const UniformBuffer<PerViewData> &perViewData, VkImageView position,
        VkImageView albedo, VkImageView normal, VkImageView uv); // Deprecating

    VkDescriptorSet allocateGBufferDescriptorSet(
        const RenderPassGBuffer::GBufferViews &gbufferViews);
    VkDescriptorSet allocateGBufferDescriptorSet();
    static void updateGBufferDescriptorSet(
        VkDescriptorSet descriptorSet,
        const RenderPassGBuffer::GBufferViews &gbufferViews);

    VkDescriptorSet allocateSingleSamplerDescriptorSet(VkImageView textureView);

    VkDescriptorSet allocatePerviewDataDescriptorSet(
        const UniformBuffer<PerViewData> &perViewData);

    // Material Descriptor Set
    VkDescriptorSet allocateMaterialDescriptorSet();
    VkDescriptorSet allocateMaterialDescriptorSet(const Material::MaterialParameters &materialParameters);
    static void updateMaterialDescriptorSet(VkDescriptorSet descriptorSet, const Material::MaterialParameters &materialParameters);

    VkDescriptorSetLayout getDescriptorLayout(DescriptorLayoutType type) const
    {
        return m_aDescriptorSetLayouts[type];
    }

    // Function template to allocate single type uniform buffer descriptor set
    template <class T>
    VkDescriptorSet AllocateUniformBufferDescriptorSet(const Uniformbuffer<T> &uniformBuffer, uint32_t binding)
    {
        return VK_NULL_HANDLE;
    }

private:
    static VkDescriptorSetLayoutBinding GetUniformBufferBinding(uint32_t binding, VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = binding;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.pImmutableSamplers = nullptr;
        uboLayoutBinding.stageFlags = stages;
        return uboLayoutBinding;
    }

    static VkDescriptorSetLayoutBinding GetSamplerArrayBinding(
        uint32_t binding, uint32_t numSamplers)
    {
        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = binding;
        samplerLayoutBinding.descriptorCount = numSamplers;
        samplerLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        return samplerLayoutBinding;
    }

    static VkDescriptorSetLayoutBinding GetInputAttachmentBinding(
        uint32_t binding, uint32_t numAttachments = 1)
    {
        VkDescriptorSetLayoutBinding descSetBinding;
        descSetBinding.binding = binding;
        descSetBinding.descriptorCount = numAttachments;
        descSetBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        descSetBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        return descSetBinding;
    }

    static VkDescriptorSetLayoutBinding GetSamplerBinding(uint32_t binding)
    {
        return GetSamplerArrayBinding(binding, 1);
    }

    std::array<VkDescriptorSetLayout, DESCRIPTOR_LAYOUT_COUNT>
        m_aDescriptorSetLayouts = {VK_NULL_HANDLE};
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    const uint32_t DESCRIPTOR_COUNT_EACH_TYPE = 1000;
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
DescriptorManager *GetDescriptorManager();
