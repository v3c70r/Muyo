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
        vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
        vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer, nullptr);
    }
    void AddParameter(const IRenderResource* pResource, VkDescriptorType type, VkShaderStageFlags stages);
    void AddImageParameter(const ImageResource* pResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE);
    void AddImageParameter(std::vector<const ImageResource*>& vpResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE);

    // Color and depth outputs
    void AddAttachment(const ImageResource* pResource, VkFormat format, VkImageLayout initialLayout, VkImageLayout finalLayout, bool bClearAttachment);

    template <class T>
    void AddPushConstantParameter(VkShaderStageFlags stages = VK_SHADER_STAGE_ALL)
    {
        m_vPushConstantRanges.push_back({stages, 0, sizeof(T)});
    }

    VkDescriptorSet AllocateDescriptorSet(const std::string& sDescSetName);
    const VkDescriptorSetLayout& GetDescriptorSetLayout() const;


    // Create all the resources needed for the render pass
    void Finalize(const std::string& sPassName);

    // Set render area
    void SetRenderArea(VkExtent2D renderArea) { m_renderArea = renderArea; }
    // Get render area
    VkExtent2D GetRenderArea() const { return m_renderArea; }

    VkDescriptorSetLayout GetDescriptorLayout() {return m_descSetLayout;}
    VkPipelineLayout GetPipelineLayout() {return m_pipelineLayout;}
    VkRenderPass GetRenderPass() {return m_renderPass;}
    VkFramebuffer GetFramebuffer() {return m_framebuffer;}
    

private:
    void AddBinding(VkDescriptorType type, uint32_t count, VkShaderStageFlags stages);
    void AddImageDescriptorWrite(const ImageResource* pResource, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE);
    void AddImageDescriptorWrite(const std::vector<const ImageResource*> vpResources, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler);  // Use a single sampler for all image resources for now
    void AddDescriptorWrite(const IRenderResource* pResource, VkDescriptorType type);

    void CreateDescriptorSetLayout();
    void CreatePipelineLayout();
    void CreateRenderPass();
    void CreateFrameBuffer();

private:
    std::vector<VkWriteDescriptorSet> m_vWriteDescSet;
    std::vector<size_t> m_vDescriptorInfoIndex;  // store index of descriptor info in corresponding write descriptor info array
    std::vector<VkDescriptorSetLayoutBinding> m_vBindings;

    // Keep track of support structures
    
    // Descriptor related
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> m_vAccelerationStructureWrites;
    std::vector<VkDescriptorImageInfo> m_vImageInfos;
    std::vector<VkDescriptorBufferInfo> m_vBufferInfos;
    std::vector<VkPushConstantRange> m_vPushConstantRanges;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descSet = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Framebuffer related
    std::vector<VkAttachmentDescription> m_vAttachmentDescriptions;
    std::vector<VkAttachmentReference> m_vColorAttachmentReferences;
    VkAttachmentReference m_depthAttachmentReference = {};
    std::vector<const ImageResource*> m_vAttachmentResources;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;

    VkExtent2D m_renderArea = {0, 0};

    bool m_bIsFinalized = false;
};
