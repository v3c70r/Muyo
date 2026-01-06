#pragma once
// built-in descriptor bindings

#include <unordered_map>

#include "DescriptorManager.h"
#include "RenderGraph/RenderGraphResourceHandle.h"
namespace Muyo::RenderGraph
{

//static std::unordered_map<ResourceHandle, uint32_t> bindings;
//
//struct DescriptorSetBinding
//{
//    uint32_t binding;
//    VkDescriptorSetLayout layout;
//    VkDescriptorSetLayoutBinding binding;
//};

class RenderGraphDescriptorSets
{
    public:
        explicit RenderGraphDescriptorSets(DescriptorManager& descManager) : m_descManager(descManager) {}
        VkDescriptorSet GetDescriptorSet(ResourceHandle handle) const;
    private:
        DescriptorManager& m_descManager;
};
}
