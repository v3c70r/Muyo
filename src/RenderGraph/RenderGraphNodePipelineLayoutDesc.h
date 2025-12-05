#include <optional>
#include "RenderGraphResourceDesc.h"
namespace Muyo::RenderGraph
{
struct PushConstantDesc
{
    VkShaderStageFlags stages;
    size_t size;
};
struct PipelineLayoutDesc
{
    std::vector<ResourceDesc> bindings;
    std::optional<PushConstantDesc> pushConstant;
    void Append(const PipelineLayoutDesc& other)
    {
        bindings.insert(bindings.end(), other.bindings.begin(), other.bindings.end());
        if (other.pushConstant.has_value())
        {
            // handle the case where both layout descs have push constants
            assert(!pushConstant.has_value());
            pushConstant = other.pushConstant;
        }
    }
};

inline VkDescriptorSetLayout CreateDescriptorSetLayout(const std::vector<ResourceDesc>& bindingVariants)
{
    std::vector<VkDescriptorSetLayoutBinding> vkBindings(bindingVariants.size());
    uint32_t bindingIndx = 0;
    for (auto & vkBinding : vkBindings)
    {
        std::visit([&vkBinding, bindingIndx](auto&& binding) { 
                vkBinding.binding = bindingIndx;
                vkBinding.descriptorType = GetDescriptorType(binding);
                vkBinding.descriptorCount = 1;
                vkBinding.stageFlags = 
                ; },
                bindingVariants[bindingIndx]);
        bindingIndx++;
    }
}

inline VkPipelineLayoutCreateInfo GeneratePipelineLayoutCreateInfo(const PipelineLayoutDesc& pipelineLayoutDesc)
{
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };

    /*
    typedef struct VkPipelineLayoutCreateInfo {
    VkStructureType                 sType;
    const void*                     pNext;
    VkPipelineLayoutCreateFlags     flags;
    uint32_t                        setLayoutCount;
    const VkDescriptorSetLayout*    pSetLayouts;
    uint32_t                        pushConstantRangeCount;
    const VkPushConstantRange*      pPushConstantRanges;
    */
}

    // fill pipeline layout


    return pipelineLayoutInfo;
}

inline VkDescriptorSetLayout AllocateDescriptorSetLayout(const DescriptorSetDesc& desc)
{
}
}
