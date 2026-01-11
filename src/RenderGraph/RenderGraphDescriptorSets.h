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

private:
    DescriptorManager& m_descManager;
    EnumArray<ResourceBindingSemantic, VkDescriptorSet> m_descriptorSets{};
    EnumArray<ResourceBindingSemantic, VkDescriptorSetLayout> m_descriptorSetLayouts{};
};
}  // namespace Muyo::RenderGraph
