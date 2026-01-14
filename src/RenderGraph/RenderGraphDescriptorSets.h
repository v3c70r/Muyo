#pragma once
// built-in descriptor bindings

#include <unordered_map>

#include "DescriptorManager.h"
#include "RenderGraph/RenderGraphResourceHandle.h"
#include "RenderGraph/bindings.h"
#include "RenderGraphNodeResource.h"
namespace Muyo::RenderGraph
{

template <typename E>
constexpr auto ENUM_COUNT = static_cast<size_t>(E::COUNT);

template <typename E>
constexpr size_t EnumIndex(E e)
{
    return static_cast<size_t>(e);
}

template <typename E, typename T>
struct EnumArray
{
    static_assert(std::is_enum_v<E>);

    std::array<T, ENUM_COUNT<E>> data{};

    constexpr T& operator[](E e) { return data[EnumIndex(e)]; }

    constexpr const T& operator[](E e) const { return data[EnumIndex(e)]; }
};

struct BindingInfo
{
    uint32_t bindingIndex;

    uint32_t setIndex;
};

static const uint32_t MAX_BINDLESS_TEXTURE_COUNT = 1024;

static std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindingsPerSet = {
    // PER_VIEW
    {
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_ALL},
    },
    // PER_OBJ
    {
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_ALL},

    },
    // MATERIAL
    {
        {.binding = BINDING_PBR_MATERIAL,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_ALL},
        {.binding = BINDING_TEXTURS,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = MAX_BINDLESS_TEXTURE_COUNT,
         .stageFlags = VK_SHADER_STAGE_ALL},
    }

};

class RenderGraphDescriptorSets
{
public:
    explicit RenderGraphDescriptorSets(DescriptorManager& descManager) : m_descManager(descManager)
    {
        m_descriptorSetLayouts[ResourceBindingSemantic::PER_VIEW] =
            m_descManager.AllocateDescriptorSetLayout(bindingsPerSet[EnumIndex(ResourceBindingSemantic::PER_VIEW)]);
        m_descriptorSetLayouts[ResourceBindingSemantic::PER_OBJ] =
            m_descManager.AllocateDescriptorSetLayout(bindingsPerSet[EnumIndex(ResourceBindingSemantic::PER_OBJ)]);
        m_descriptorSetLayouts[ResourceBindingSemantic::MATERIAL] =
            m_descManager.AllocateDescriptorSetLayout(bindingsPerSet[EnumIndex(ResourceBindingSemantic::MATERIAL)]);
        m_descriptorSets[ResourceBindingSemantic::PER_VIEW] =
            m_descManager.AllocateDescriptorSet(m_descriptorSetLayouts[ResourceBindingSemantic::PER_VIEW]);
        m_descriptorSets[ResourceBindingSemantic::PER_OBJ] =
            m_descManager.AllocateDescriptorSet(m_descriptorSetLayouts[ResourceBindingSemantic::PER_OBJ]);
        m_descriptorSets[ResourceBindingSemantic::MATERIAL] =
            m_descManager.AllocateDescriptorSet(m_descriptorSetLayouts[ResourceBindingSemantic::MATERIAL]);
    }
    ~RenderGraphDescriptorSets()
    {
        m_descManager.DestroyDescriptorSetLayout(m_descriptorSetLayouts[ResourceBindingSemantic::PER_VIEW]);
        m_descManager.DestroyDescriptorSetLayout(m_descriptorSetLayouts[ResourceBindingSemantic::PER_OBJ]);
        m_descManager.DestroyDescriptorSetLayout(m_descriptorSetLayouts[ResourceBindingSemantic::MATERIAL]);
    }

    VkDescriptorSet GetDescriptorSet(ResourceBindingSemantic bindingSemantic) const
    {
        return m_descriptorSets[bindingSemantic];
    }
    VkDescriptorSetLayout GetDescriptorSetLayout(ResourceBindingSemantic bindingSemantic) const
    {
        return m_descriptorSetLayouts[bindingSemantic];
    }

    static ResourceBindingSemantic GetSemantic(uint32_t setIndex)
    {
        switch (setIndex)
        {
            case 0:
                return ResourceBindingSemantic::PER_VIEW;
            case 1:
                return ResourceBindingSemantic::PER_OBJ;
            case 2:
                return ResourceBindingSemantic::MATERIAL;
            default:
                return ResourceBindingSemantic::NONE;
        }
    }

    void BindResourceToDescriptorSet(const IRenderResource* pResource, 
            ResourceBindingSemantic bindingSemantic, uint32_t bindingIndex)
    {
        VkDescriptorSet descriptorSet = m_descriptorSets[bindingSemantic];

        VkWriteDescriptorSet writeDescSet = {};
        writeDescSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescSet.dstSet = descriptorSet;
        writeDescSet.dstBinding = bindingIndex;
        writeDescSet.dstArrayElement = 0;
        writeDescSet.descriptorCount = 1;
        switch (bindingSemantic)
        {
            case ResourceBindingSemantic::PER_VIEW:
                writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case ResourceBindingSemantic::PER_OBJ:
                writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case ResourceBindingSemantic::MATERIAL:
                if (bindingIndex == BINDING_PBR_MATERIAL)
                {
                    writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                }
                else if (bindingIndex == BINDING_TEXTURS)
                {
                    writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                }
                else
                {
                    assert(false && "Invalid binding index for MATERIAL descriptor set");
                    return;
                }
                break;
            default:
                assert(false && "Unsupported binding semantic");
                return;
        }

        if (const auto* pBufferResource = dynamic_cast<const BufferResource*>(pResource))
        {
            VkDescriptorBufferInfo bufferInfo = {};
            bufferInfo.buffer = pBufferResource->buffer();
            bufferInfo.offset = 0;
            bufferInfo.range = pBufferResource->GetSize();
            writeDescSet.pBufferInfo = &bufferInfo;
        }
        else if (const auto* pImageResource = dynamic_cast<const ImageResource*>(pResource))
        {
            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageView = pImageResource->getView();
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writeDescSet.pImageInfo = &imageInfo;
        }
        else
        {
            assert(false && "Unsupported resource type for binding");
            return;
        }

        vkUpdateDescriptorSets(GetRenderDevice()->GetDevice(), 1, &writeDescSet, 0, nullptr);
    }

private:
    DescriptorManager& m_descManager;
    EnumArray<ResourceBindingSemantic, VkDescriptorSet> m_descriptorSets{};
    EnumArray<ResourceBindingSemantic, VkDescriptorSetLayout> m_descriptorSetLayouts{};
};
}  // namespace Muyo::RenderGraph
