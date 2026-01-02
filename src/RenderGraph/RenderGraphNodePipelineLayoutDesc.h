#include <optional>
#include "RenderGraphResourceDesc.h"
namespace Muyo::RenderGraph
{
struct BindingDesc
{
    ResourceDesc resource;
    VkShaderStageFlags stages;
};
struct DescriptorSetDesc
{
    std::vector<BindingDesc> bindings;
};

struct PipelineLayoutDesc
{
    std::vector<DescriptorSetDesc>  descriptorSets;
    std::vector<VkPushConstantRange> pushConstants;
};

struct PipelineLayoutObjects
{
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
};

inline VkDescriptorSetLayout CreateDescriptorSetLayout(const DescriptorSetDesc& desc)
{
    std::vector<VkDescriptorSetLayoutBinding> vkBindings(desc.bindings.size());
    uint32_t bindingIndx = 0;
    for (auto & vkBinding : vkBindings)
    {
        VkShaderStageFlags stages = desc.bindings[bindingIndx].stages;
        std::visit([&vkBinding, bindingIndx, &stages](auto&& binding) { 
                vkBinding.binding = bindingIndx;
                vkBinding.descriptorType = GetDescriptorType(binding);
                vkBinding.descriptorCount = GetDescriptorCount(binding);
                vkBinding.stageFlags = stages;
                },
                desc.bindings[bindingIndx].resource);
        bindingIndx++;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = static_cast<uint32_t>(vkBindings.size()),
        .pBindings = vkBindings.data(),
    };
    VkDescriptorSetLayout layout;
    VK_ASSERT(vkCreateDescriptorSetLayout(GetRenderDevice()->GetDevice(), &layoutInfo, nullptr, &layout));
    return layout;
}

inline PipelineLayoutObjects CreatePipelineObjects(const PipelineLayoutDesc& pipelineLayoutDesc)
{
    PipelineLayoutObjects pipelineLayoutObjects;
    for (const auto& descSet : pipelineLayoutDesc.descriptorSets)
    {
        VkDescriptorSetLayout layout = CreateDescriptorSetLayout(descSet);
        pipelineLayoutObjects.descriptorSetLayouts.push_back(layout);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = static_cast<uint32_t>(pipelineLayoutObjects.descriptorSetLayouts.size()),
        .pSetLayouts = nullptr, // to be filled
        .pushConstantRangeCount = static_cast<uint32_t>(pipelineLayoutDesc.pushConstants.size()),
        .pPushConstantRanges = pipelineLayoutDesc.pushConstants.data(),
    };

    // Create pipeline layout
    VK_ASSERT(vkCreatePipelineLayout(
                GetRenderDevice()->GetDevice(),
                &pipelineLayoutInfo,
                nullptr,
                &pipelineLayoutObjects.pipelineLayout));

    return pipelineLayoutObjects;
}
}  // namespace Muyo::RenderGraph
