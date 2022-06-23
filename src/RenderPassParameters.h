#pragma once
#include "VkRenderDevice.h"

class IRenderResource;
class ImageResource;

class RenderPassParameters
{
public:
    ~RenderPassParameters()
    {
        vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(), m_descSetLayout, nullptr);
    }
    void AddParameter(const IRenderResource* pResource, VkDescriptorType type, VkShaderStageFlags stages);
    void AddImageParameter(const ImageResource* pResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE);
    void AddImageParameter(std::vector<const ImageResource*>& vpResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE);
    VkDescriptorSet AllocateDescriptorSet(const std::string& sDescSetName);
    const VkDescriptorSetLayout& GetDescriptorSetLayout() const;
    void CreateDescriptorSetLayout();

private:
    void AddBinding(VkDescriptorType type, uint32_t count, VkShaderStageFlags stages);
    void AddImageDescriptorWrite(const ImageResource* pResource, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE);
    void AddImageDescriptorWrite(const std::vector<const ImageResource*> vpResources, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler);  // Use a single sampler for all image resources for now
    void AddDescriptorWrite(const IRenderResource* pResource, VkDescriptorType type);

private:
    std::vector<VkWriteDescriptorSet> m_vWriteDescSet;
    std::vector<size_t> m_vDescriptorInfoIndex;  // store index of descriptor info in corresponding write descriptor info array
    std::vector<VkDescriptorSetLayoutBinding> m_vBindings;

    // Keep track of support structures
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> m_vAccelerationStructureWrites;
    std::vector<VkDescriptorImageInfo> m_vImageInfos;
    std::vector<VkDescriptorBufferInfo> m_vBufferInfos;

    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descSet = VK_NULL_HANDLE;
};
