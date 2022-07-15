#include "RenderPassParameters.h"
#include "DescriptorManager.h"

void RenderPassParameters::AddParameter(const IRenderResource* pResource, VkDescriptorType type, VkShaderStageFlags stages, uint32_t nDescSetIdx)
{
    AddBinding(type, 1, stages, nDescSetIdx);
    AddDescriptorWrite(pResource, type, nDescSetIdx);
}

void RenderPassParameters::AddImageParameter(const ImageResource* pResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler, uint32_t nDescSetIdx)
{
    AddBinding(type, 1, stages, nDescSetIdx);
    AddImageDescriptorWrite(pResource, type, imageLayout, sampler, nDescSetIdx);
}
void RenderPassParameters::AddImageParameter(std::vector<const ImageResource*>& vpResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler, uint32_t nDescSetIdx)
{
    AddBinding(type, vpResource.size(), stages, nDescSetIdx);
    AddImageDescriptorWrite(vpResource, type, imageLayout, sampler, nDescSetIdx);
}
void RenderPassParameters::AddImageDescriptorWrite(const ImageResource* pResource, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler, uint32_t nDescSetIdx)
{
    if (m_vWriteDescSet.size() <= nDescSetIdx)
    {
        m_vWriteDescSet.resize(nDescSetIdx + 1);
    }
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE; // This is set later
    write.dstBinding = m_vWriteDescSet[nDescSetIdx].size();
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;


    VkDescriptorImageInfo imageInfo = {VK_NULL_HANDLE, pResource->getView(), imageLayout};
    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
    {
        imageInfo.sampler = sampler;
    }

    if (m_vDescriptorInfoIndex.size() <= nDescSetIdx)
    {
        m_vDescriptorInfoIndex.resize(nDescSetIdx + 1);
    }
    m_vDescriptorInfoIndex[nDescSetIdx].push_back(m_vImageInfos.size());

    m_vImageInfos.push_back(imageInfo);

    
    m_vWriteDescSet[nDescSetIdx].push_back(write);
}

void RenderPassParameters::AddImageDescriptorWrite(const std::vector<const ImageResource*> vpResources, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler, uint32_t nDescSetIdx)
{
    if (m_vWriteDescSet.size() <= nDescSetIdx)
    {
        m_vWriteDescSet.resize(nDescSetIdx + 1);
    }

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE; // This is set later
    write.dstBinding = m_vWriteDescSet[nDescSetIdx].size();
    write.dstArrayElement = 0;
    write.descriptorCount = static_cast<uint32_t>(vpResources.size());
    write.descriptorType = type;

    if (m_vDescriptorInfoIndex.size() <= nDescSetIdx)
    {
        m_vDescriptorInfoIndex.resize(nDescSetIdx + 1);
    }
    m_vDescriptorInfoIndex[nDescSetIdx].push_back(m_vImageInfos.size());

    for (const auto* pImageResource : vpResources)
    {
        VkDescriptorImageInfo imageInfo = {sampler, pImageResource->getView(), imageLayout};
        m_vImageInfos.push_back(imageInfo);
    }

    m_vWriteDescSet[nDescSetIdx].push_back(write);
}

void RenderPassParameters::AddDescriptorWrite(const IRenderResource* pResource, VkDescriptorType type, uint32_t nDescSetIdx)
{
    if (m_vDescriptorInfoIndex.size() <= nDescSetIdx)
    {
        m_vDescriptorInfoIndex.resize(nDescSetIdx + 1);
    }

    if (m_vWriteDescSet.size() <= nDescSetIdx)
    {
        m_vWriteDescSet.resize(nDescSetIdx + 1);
    }

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE; // This is set later
    write.dstBinding = m_vWriteDescSet[nDescSetIdx].size();
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;

    if (type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
    {

        m_vDescriptorInfoIndex[nDescSetIdx].push_back(m_vAccelerationStructureWrites.size());

        VkWriteDescriptorSetAccelerationStructureKHR accDesc = {};
        accDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        accDesc.accelerationStructureCount = 1;
        accDesc.pAccelerationStructures = &(static_cast<const AccelerationStructure*>(pResource)->GetAccelerationStructure());
        m_vAccelerationStructureWrites.push_back(accDesc);
    }
    else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
    {
        m_vDescriptorInfoIndex[nDescSetIdx].push_back(m_vBufferInfos.size());

        VkDescriptorBufferInfo bufferInfo = {};
        if (pResource)
        {
            const BufferResource* pBufferResource = dynamic_cast<const BufferResource*>(pResource);
            bufferInfo.buffer = pBufferResource->buffer();
            bufferInfo.range = pBufferResource->GetSize();
        }
        else
        {
            bufferInfo.buffer = VK_NULL_HANDLE;
            bufferInfo.range = 0;
        }
        bufferInfo.offset = 0;
        m_vBufferInfos.push_back(bufferInfo);
    }
    else
    {
        assert(false && u8"Unhandled descriptor type");
    }

    m_vWriteDescSet[nDescSetIdx].push_back(write);
}

void RenderPassParameters::AddBinding(VkDescriptorType type, uint32_t nCount, VkShaderStageFlags stages, uint32_t nDescSetIdx)
{
    if (m_vBindings.size() <= nDescSetIdx)
    {
        m_vBindings.resize(nDescSetIdx + 1);
    }

    VkDescriptorSetLayoutBinding bindingInfo = {};
    bindingInfo.binding = m_vBindings[nDescSetIdx].size();
    bindingInfo.descriptorType = type;
    bindingInfo.descriptorCount = nCount;
    bindingInfo.stageFlags = stages;
    m_vBindings[nDescSetIdx].push_back(bindingInfo);
}

const VkDescriptorSetLayout& RenderPassParameters::GetDescriptorSetLayout(uint32_t nDescSetIdx) const
{
    return m_vDescSetLayouts[nDescSetIdx];
}
void RenderPassParameters::CreateDescriptorSetLayout()
{
    std::for_each(m_vBindings.begin(), m_vBindings.end(), [this](const std::vector<VkDescriptorSetLayoutBinding>& vBindings)
    {
        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pBindings = vBindings.data();
        layoutInfo.bindingCount = static_cast<uint32_t>(vBindings.size());

        VkDescriptorSetLayout layout;
        VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(), &layoutInfo, nullptr, &layout));
        m_vDescSetLayouts.push_back(layout);
    });
}

VkDescriptorSet RenderPassParameters::AllocateDescriptorSet(const std::string& sDescSetName, uint32_t nDescSetIdx)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = GetDescriptorManager()->GetDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_vDescSetLayouts[nDescSetIdx];
    VK_ASSERT(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo, &descriptorSet));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet), VK_OBJECT_TYPE_DESCRIPTOR_SET, sDescSetName.c_str());
    std::vector<VkWriteDescriptorSet>& vWriteDescriptorSets = m_vWriteDescSet[nDescSetIdx];
    std::vector<size_t>& vDescriptorInfoIndex = m_vDescriptorInfoIndex[nDescSetIdx];
    for (size_t i = 0; i < vWriteDescriptorSets.size(); ++i)
    {
        vWriteDescriptorSets[i].dstSet = descriptorSet;
        // Resolve write descriptor set info
        if (vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            vWriteDescriptorSets[i].pBufferInfo = &m_vBufferInfos[vDescriptorInfoIndex[i]];
        }
        else if (vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        {
            vWriteDescriptorSets[i].pImageInfo = &m_vImageInfos[vDescriptorInfoIndex[i]];
        }
        else if (vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
        {
            vWriteDescriptorSets[i].pNext = &m_vAccelerationStructureWrites[vDescriptorInfoIndex[i]];
        }
        else
        {
            assert(false && u8"Unhandled descriptor type");
        }
    }
    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), vWriteDescriptorSets.size(), vWriteDescriptorSets.data(), 0, nullptr);
    return descriptorSet;
}

VkDescriptorSet RenderPassParameters::AllocateDescriptorSet(const std::string& sDescSetName, const std::vector<const IRenderResource*>& vpResources, uint32_t nDescSetIdx)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = GetDescriptorManager()->GetDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_vDescSetLayouts[nDescSetIdx];
    VK_ASSERT(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo, &descriptorSet));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet), VK_OBJECT_TYPE_DESCRIPTOR_SET, sDescSetName.c_str());

    std::vector<VkWriteDescriptorSet>& vWriteDescriptorSets = m_vWriteDescSet[nDescSetIdx];
    std::vector<size_t>& vDescriptorInfoIndex = m_vDescriptorInfoIndex[nDescSetIdx];
    assert(vpResources.size() == vWriteDescriptorSets.size());

    for (size_t i = 0; i < vWriteDescriptorSets.size(); ++i)
    {
        vWriteDescriptorSets[i].dstSet = descriptorSet;
        // Resolve write descriptor set info
        if (vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            m_vBufferInfos[vDescriptorInfoIndex[i]].buffer = static_cast<const BufferResource*>(vpResources[i])->buffer();
            m_vBufferInfos[vDescriptorInfoIndex[i]].range = static_cast<const BufferResource*>(vpResources[i])->GetSize();
            vWriteDescriptorSets[i].pBufferInfo = &m_vBufferInfos[vDescriptorInfoIndex[i]];
        }
        else if (vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        {
            m_vImageInfos[vDescriptorInfoIndex[i]].imageView = static_cast<const ImageResource*>(vpResources[i])->getView();
            vWriteDescriptorSets[i].pImageInfo = &m_vImageInfos[vDescriptorInfoIndex[i]];
        }
        else if (vWriteDescriptorSets[i].descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
        {
            m_vAccelerationStructureWrites[vDescriptorInfoIndex[i]].accelerationStructureCount = 1;
            m_vAccelerationStructureWrites[vDescriptorInfoIndex[i]].pAccelerationStructures = &static_cast<const AccelerationStructure*>(vpResources[i])->GetAccelerationStructure();
            vWriteDescriptorSets[i].pNext = &m_vAccelerationStructureWrites[vDescriptorInfoIndex[i]];
        }
        else
        {
            assert(false && u8"Unhandled descriptor type");
        }
    }
    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), vWriteDescriptorSets.size(), vWriteDescriptorSets.data(), 0, nullptr);
    return descriptorSet;
}

void RenderPassParameters::AddAttachment(const ImageResource* pResource, VkFormat format, VkImageLayout initialLayout, VkImageLayout finalLayout, bool bClearAttachment)
{

    // Create attachment description
    VkAttachmentDescription attachmentDesc = {};
    attachmentDesc.format = format;
    attachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    attachmentDesc.loadOp = bClearAttachment ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachmentDesc.initialLayout = initialLayout;
    attachmentDesc.finalLayout = finalLayout;

    m_vAttachmentDescriptions.push_back(attachmentDesc);

    // Create attachment reference
    VkAttachmentReference attachmentRef = {};
    // check if format is depth format
    // TODO: add more depth foramts if we started to use them
    attachmentRef.attachment = (uint32_t)m_vAttachmentDescriptions.size() - 1;
    if (format == VK_FORMAT_D32_SFLOAT_S8_UINT | format == VK_FORMAT_D32_SFLOAT)
    {
        attachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        assert(m_depthAttachmentReference.attachment != VK_ATTACHMENT_UNUSED);
        m_depthAttachmentReference = attachmentRef;
    }
    else
    {
        attachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_vColorAttachmentReferences.push_back(attachmentRef);
    }

    m_vAttachmentResources.push_back(pResource);
}

void RenderPassParameters::CreatePipelineLayout()
{
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_vDescSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = m_vDescSetLayouts.data();

    if (m_vPushConstantRanges.size() > 0)
    {
        pipelineLayoutInfo.pushConstantRangeCount = m_vPushConstantRanges.size();
        pipelineLayoutInfo.pPushConstantRanges = m_vPushConstantRanges.data();
    }

    VK_ASSERT(vkCreatePipelineLayout(GetRenderDevice()->GetDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout));

}

void RenderPassParameters::CreateRenderPass()
{
   // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = (uint32_t)m_vColorAttachmentReferences.size();
    subpass.pColorAttachments = m_vColorAttachmentReferences.data();
    subpass.pDepthStencilAttachment = &m_depthAttachmentReference;

   // Subpass dependency
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDep.dstSubpass = 0;
    subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.srcAccessMask = 0;
    subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(m_vAttachmentDescriptions.size());
    renderPassInfo.pAttachments = m_vAttachmentDescriptions.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    VK_ASSERT(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo, nullptr, &m_renderPass));

}

void RenderPassParameters::CreateFrameBuffer()
{
    std::vector<VkImageView> attachments;
    for (const auto& attachment : m_vAttachmentResources)
    {
        attachments.push_back(attachment->getView());
    }
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_renderArea.width;
    framebufferInfo.height = m_renderArea.height;
    framebufferInfo.layers = 1;
    VK_ASSERT(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo, nullptr, &m_framebuffer));
}

void RenderPassParameters::Finalize(const std::string& sPassName)
{
    if (!m_vBindings.empty())
    {
        CreateDescriptorSetLayout();
        //setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_descSetLayout), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, sPassName.c_str());
    }
    CreatePipelineLayout();
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, sPassName.c_str());

    if (!m_vAttachmentResources.empty())
    {
        CreateRenderPass();
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_renderPass), VK_OBJECT_TYPE_RENDER_PASS, sPassName.c_str());
        CreateFrameBuffer();
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_framebuffer), VK_OBJECT_TYPE_FRAMEBUFFER, sPassName.c_str());
    }

    m_bIsFinalized = true;
}

