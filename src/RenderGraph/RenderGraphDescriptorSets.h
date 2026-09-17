#pragma once
// built-in descriptor bindings

#include <unordered_map>

#include "DescriptorManager.h"
#include "RenderGraph/RenderGraphResourceHandle.h"
#include "RenderGraph/bindings.h"
#include "RenderGraphNodeResource.h"
namespace Muyo::RenderGraph
{

/// Number of enumerators in an enum that has a trailing `COUNT` value.
template <typename E>
constexpr auto ENUM_COUNT = static_cast<size_t>(E::COUNT);

/// Converts an enum value to its underlying index.
template <typename E>
constexpr size_t EnumIndex(E e)
{
    return static_cast<size_t>(e);
}

/// `std::array` indexed by an enum value.
template <typename E, typename T>
struct EnumArray
{
    static_assert(std::is_enum_v<E>);

    std::array<T, ENUM_COUNT<E>> data{};  ///< Backing storage.

    /// @return Mutable reference to the element for enum value `e`.
    constexpr T& operator[](E e) { return data[EnumIndex(e)]; }

    /// @return Const reference to the element for enum value `e`.
    constexpr const T& operator[](E e) const { return data[EnumIndex(e)]; }
};

/// Descriptor binding location (set + binding).
struct BindingInfo
{
    uint32_t bindingIndex;  ///< Binding index within the set.
    uint32_t setIndex;      ///< Descriptor set index.
};

/// Capacity of the bindless texture array in the MATERIAL set.
static const uint32_t MAX_BINDLESS_TEXTURE_COUNT = 1024;

/// Static descriptor bindings of the three built-in semantic sets.
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

/// Owns and writes the three built-in semantic descriptor sets used by graphics nodes.
///
/// `PER_VIEW`, `PER_OBJ` and `MATERIAL` layouts/sets are allocated once per builder and reused for
/// every graphics node; `BindResourceToDescriptorSet` updates the binding before the node is bound.
class RenderGraphDescriptorSets
{
public:
    /// Allocate the three semantic set layouts and their descriptor sets.
    /// @param descManager Descriptor manager owning the pool and layout cache.
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
    /// Destroy the semantic set layouts owned by this instance.
    ~RenderGraphDescriptorSets()
    {
        m_descManager.DestroyDescriptorSetLayout(m_descriptorSetLayouts[ResourceBindingSemantic::PER_VIEW]);
        m_descManager.DestroyDescriptorSetLayout(m_descriptorSetLayouts[ResourceBindingSemantic::PER_OBJ]);
        m_descManager.DestroyDescriptorSetLayout(m_descriptorSetLayouts[ResourceBindingSemantic::MATERIAL]);
    }

    /// @return The descriptor set for a semantic set index.
    VkDescriptorSet GetDescriptorSet(ResourceBindingSemantic bindingSemantic) const
    {
        return m_descriptorSets[bindingSemantic];
    }
    /// @return The descriptor set layout for a semantic set index.
    VkDescriptorSetLayout GetDescriptorSetLayout(ResourceBindingSemantic bindingSemantic) const
    {
        return m_descriptorSetLayouts[bindingSemantic];
    }

    /// @return The semantic set corresponding to a raw descriptor set index (0/1/2).
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

    /// Write a resource into a semantic descriptor set.
    /// @param pResource       Buffer or image to bind.
    /// @param bindingSemantic Target semantic set.
    /// @param bindingIndex    Binding within the set.
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
