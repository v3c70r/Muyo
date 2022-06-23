#include "RenderPassParameters.h"
#include "DescriptorManager.h"

void RenderPassParameters::AddParameter(const IRenderResource* pResource, VkDescriptorType type, VkShaderStageFlags stages)
{
    AddBinding(type, 1, stages);
    AddDescriptorWrite(pResource, type);
}

void RenderPassParameters::AddImageParameter(const ImageResource* pResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler)
{
    AddBinding(type, 1, stages);
    AddImageDescriptorWrite(pResource, type, imageLayout, sampler);
}
void RenderPassParameters::AddImageParameter(std::vector<const ImageResource*>& vpResource, VkDescriptorType type, VkShaderStageFlags stages, VkImageLayout imageLayout, VkSampler sampler)
{
    AddBinding(type, vpResource.size(), stages);
    AddImageDescriptorWrite(vpResource, type, imageLayout, sampler);
}
void RenderPassParameters::AddImageDescriptorWrite(const ImageResource* pResource, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler)
{
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE; // This is set later
    write.dstBinding = m_vWriteDescSet.size();
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;


    VkDescriptorImageInfo imageInfo = {VK_NULL_HANDLE, pResource->getView(), imageLayout};
    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
    {
        imageInfo.sampler = sampler;
    }

    m_vDescriptorInfoIndex.push_back(m_vImageInfos.size());

    m_vImageInfos.push_back(imageInfo);
    m_vWriteDescSet.push_back(write);
}

void RenderPassParameters::AddImageDescriptorWrite(const std::vector<const ImageResource*> vpResources, VkDescriptorType type, VkImageLayout imageLayout, VkSampler sampler)
{
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE; // This is set later
    write.dstBinding = m_vWriteDescSet.size();
    write.dstArrayElement = 0;
    write.descriptorCount = static_cast<uint32_t>(vpResources.size());
    write.descriptorType = type;

    m_vDescriptorInfoIndex.push_back(m_vImageInfos.size());

    for (const auto* pImageResource : vpResources)
    {
        VkDescriptorImageInfo imageInfo = {sampler, pImageResource->getView(), imageLayout};
        m_vImageInfos.push_back(imageInfo);
    }
    m_vWriteDescSet.push_back(write);
}

void RenderPassParameters::AddDescriptorWrite(const IRenderResource* pResource, VkDescriptorType type)
{
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE; // This is set later
    write.dstBinding = m_vWriteDescSet.size();
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;




    if (type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
    {

        m_vDescriptorInfoIndex.push_back(m_vAccelerationStructureWrites.size());

        VkWriteDescriptorSetAccelerationStructureKHR accDesc = {};
        accDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        accDesc.accelerationStructureCount = 1;
        accDesc.accelerationStructureCount = 1;
        accDesc.pAccelerationStructures = &(static_cast<const AccelerationStructure*>(pResource)->GetAccelerationStructure());
        m_vAccelerationStructureWrites.push_back(accDesc);
    }
    else if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
    {
        m_vDescriptorInfoIndex.push_back(m_vBufferInfos.size());

        const BufferResource* pBufferResource = dynamic_cast<const BufferResource*>(pResource);
        assert(pBufferResource);

        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = pBufferResource->buffer();
        bufferInfo.offset = 0;
        bufferInfo.range = pBufferResource->GetSize();
        m_vBufferInfos.push_back(bufferInfo);
    }
    else
    {
        assert(false && u8"Unhandled descriptor type");
    }

    m_vWriteDescSet.push_back(write);
}

void RenderPassParameters::AddBinding(VkDescriptorType type, uint32_t count, VkShaderStageFlags stages)
{
    VkDescriptorSetLayoutBinding bindingInfo = {};
    bindingInfo.binding = m_vBindings.size();
    bindingInfo.descriptorType = type;
    bindingInfo.descriptorCount = count;
    bindingInfo.stageFlags = stages;
    m_vBindings.push_back(bindingInfo);
}

const VkDescriptorSetLayout& RenderPassParameters::GetDescriptorSetLayout() const
{
    return m_descSetLayout;
}
void RenderPassParameters::CreateDescriptorSetLayout()
{
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = (uint32_t)m_vBindings.size();
    descriptorSetLayoutInfo.pBindings = m_vBindings.data();
    assert(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(), &descriptorSetLayoutInfo, nullptr, &m_descSetLayout) == VK_SUCCESS);
}

VkDescriptorSet RenderPassParameters::AllocateDescriptorSet(const std::string& sDescSetName)
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    // Create descriptor sets
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = GetDescriptorManager()->GetDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descSetLayout;
    assert(vkAllocateDescriptorSets(GetRenderDevice()->GetDevice(), &allocInfo, &descriptorSet) == VK_SUCCESS);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(descriptorSet), VK_OBJECT_TYPE_DESCRIPTOR_SET, sDescSetName.c_str());
    for (size_t i = 0; i < m_vWriteDescSet.size(); ++i)
    {
        m_vWriteDescSet[i].dstSet = descriptorSet;
        // Resolve write descriptor set info
        if (m_vWriteDescSet[i].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || m_vWriteDescSet[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            m_vWriteDescSet[i].pBufferInfo = &m_vBufferInfos[m_vDescriptorInfoIndex[i]];
        }
        else if (m_vWriteDescSet[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || m_vWriteDescSet[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        {
            m_vWriteDescSet[i].pImageInfo = &m_vImageInfos[m_vDescriptorInfoIndex[i]];
        }
        else if (m_vWriteDescSet[i].descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
        {
            m_vWriteDescSet[i].pNext = &m_vAccelerationStructureWrites[m_vDescriptorInfoIndex[i]];
        }
        else
        {
            assert(false && u8"Unhandled descriptor type");
        }
    }
    vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), m_vWriteDescSet.size(), m_vWriteDescSet.data(), 0, nullptr);
    return descriptorSet;
}
