#pragma once
#include <algorithm>

#include "VkRenderDevice.h"
namespace Muyo
{

/*
 * RenderPassParameters creates render pass info.
 * 1. While preparing render pass.
 *      a. Call AddParameter/AddImageParameter/AddPushConstantParameter to prepare descriptor sets with resources. Or simiply pass in
 *         a nullptr so the resource will be updated later
 *
 *         Descriptor create info and update writes will be created. As well as resource infos required .
 *
 *      b. Call AddAttachment to setup render pass outputs.
 *
 *      c. Call finalize to create renderpass
 *
 *      d. Allocate descriptor set when using the resources in the pass.
 */

class IRenderResource;
class ImageResource;

class RenderPassParameters
{
public:
    ~RenderPassParameters()
    {
        std::for_each(m_vDescSetLayouts.begin(), m_vDescSetLayouts.end(), [](VkDescriptorSetLayout layout)
                      { vkDestroyDescriptorSetLayout(GetRenderDevice()->GetDevice(), layout, nullptr); });
        vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
        vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer, nullptr);
    }
    void AddParameter(const IRenderResource* pResource, VkDescriptorType type, VkShaderStageFlags stages, uint32_t nDescSetIdx = 0);
    void AddImageParameter(const ImageResource* pResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE, uint32_t nDescSetIdx = 0);
    void AddImageParameter(std::vector<const ImageResource*>& vpResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE, uint32_t nDescSetIdx = 0);

    // Color and depth outputs
    void AddAttachment(const ImageResource* pResource, VkImageLayout initialLayout, VkImageLayout finalLayout, bool bClearAttachment);

    template <class T>
    void AddPushConstantParameter(VkShaderStageFlags stages = VK_SHADER_STAGE_ALL)
    {
        m_vPushConstantRanges.push_back({stages, 0, sizeof(T)});
    }

    VkDescriptorSet AllocateDescriptorSet(const std::string& sDescSetName, uint32_t nDescSetIdx = 0);
    VkDescriptorSet AllocateDescriptorSet(const std::string& sDescSetName, const std::vector<const IRenderResource*>& vpResources, uint32_t nDescSetIdx = 0);  // Allocate descriptor set with resources
    std::vector<VkDescriptorSet> AllocateDescriptorSets();
    const VkDescriptorSetLayout& GetDescriptorSetLayout(uint32_t nDescSetIdx = 0) const;

    // Create all the resources needed for the render pass
    void Finalize(const std::string& sPassName);

    // Set render area
    void SetRenderArea(VkExtent2D renderArea) { m_renderArea = renderArea; }
    // Get render area
    VkExtent2D GetRenderArea() const { return m_renderArea; }

    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }
    VkRenderPass GetRenderPass() const { return m_renderPass; }
    VkFramebuffer GetFramebuffer() const { return m_framebuffer; }

    // Multiview mask
    void SetMultiviewMask(uint32_t nMultiviewMask) { m_nMultiviewMask = nMultiviewMask; }

    // Get input and output resources
    const std::vector<const IRenderResource*>& GetInputResources() const { return m_vpInputResources; }
    const std::vector<const IRenderResource*>& GetOutputResources() const { return m_vpOutputResources; }

    std::string GetName() const { return m_sName; }

  private:
    void AddBinding(VkDescriptorType type, uint32_t nCount, VkShaderStageFlags stages, uint32_t nDescSetIdx);
    void AddImageDescriptorWrite(const ImageResource* pResource, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE, uint32_t nDescSetIdx = 0);
    void AddImageDescriptorWrite(const std::vector<const ImageResource*> vpResources, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler, uint32_t nDescSetIdx);  // Use a single sampler for all image resources for now
    void AddDescriptorWrite(const IRenderResource* pResource, VkDescriptorType type, uint32_t nDescSetIdx);

    void CreateDescriptorSetLayout();
    void CreatePipelineLayout();
    void CreateRenderPass();
    void CreateFrameBuffer();

    /** Update descriptor with resources at nDescSetIdx
     * return true if descriptor update is executed
     */
    bool UpdateDescriptorSet(const std::vector<const IRenderResource*>& vpResources, uint32_t nDescSetIdx, VkDescriptorSet descriptorSet);

private:
    // [DescSetIndex][BindingIndex]
    // Descriptor writes for each descriptor set and binding
    std::vector<std::vector<VkWriteDescriptorSet>> m_vWriteDescSet;
    // Resource for each descriptor set and binding.
    // If there's an array the binding will have more than 1 resource.
    // Resources are flatten for eaching descriptor set and tracked by number of descriptors in each Write Descriptor Sets.
    std::vector<std::vector<const IRenderResource*>> m_vpResources;

    std::vector<std::vector<size_t>> m_vDescriptorInfoIndex;  // store index of descriptor info in corresponding write descriptor info array
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> m_vBindings;

    // Keep track of support structures

    // Descriptor related
    // These are the structures tracked in m_vDescriptorInfoIndex
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> m_vAccelerationStructureWrites;
    std::vector<VkDescriptorImageInfo> m_vImageInfos;
    std::vector<VkDescriptorBufferInfo> m_vBufferInfos;
    std::vector<VkPushConstantRange> m_vPushConstantRanges;

    // [DescSetIndex]
    std::vector<VkDescriptorSetLayout> m_vDescSetLayouts;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Framebuffer related
    std::vector<VkAttachmentDescription> m_vAttachmentDescriptions;
    std::vector<VkAttachmentReference> m_vColorAttachmentReferences;
    VkAttachmentReference m_depthAttachmentReference = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
    std::vector<const ImageResource*> m_vAttachmentResources;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    uint32_t m_nMultiviewMask = 0;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;

    VkExtent2D m_renderArea = {0, 0};

    bool m_bIsFinalized = false;

    // Track input and output resources for the render pass
    std::vector<const IRenderResource*> m_vpInputResources;
    std::vector<const IRenderResource*> m_vpOutputResources;
    std::string m_sName = "undefined";
};
}  // namespace Muyo
