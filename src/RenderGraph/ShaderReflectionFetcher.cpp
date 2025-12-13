#include "RenderGraph/ShaderReflectionFetcher.h"
#include <algorithm>
#include <cassert>

namespace Muyo::RenderGraph
{
ShaderReflectionFetcher::ShaderReflectionFetcher(const void* spirv_code, size_t spirv_nbytes)
{
    SpvReflectResult result = spvReflectCreateShaderModule(spirv_nbytes, spirv_code, &m_module);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);

    // Build bindings
    ExtractDescriptorSetBindings();
    ExtractPushConstantReanges();
}

ShaderReflectionFetcher::~ShaderReflectionFetcher()
{
    spvReflectDestroyShaderModule(&m_module);
}

template <typename T, typename EnumFn>
void ShaderReflectionFetcher::EnumerateToVector(EnumFn enumFn, std::vector<T*>& outVec)
{
    uint32_t count = 0;
    enumFn(&m_module, &count, nullptr);
    outVec.resize(count);
    if (count > 0)
    {
        enumFn(&m_module, &count, outVec.data());
    }
}

// Explicit template instantiation for the types used
template void ShaderReflectionFetcher::EnumerateToVector<SpvReflectDescriptorSet>(SpvReflectResult(*)(const SpvReflectShaderModule*, uint32_t*, SpvReflectDescriptorSet**), std::vector<SpvReflectDescriptorSet*>&);

void ShaderReflectionFetcher::ExtractDescriptorSetBindings()
{
    std::vector<SpvReflectDescriptorSet*> sets;
    EnumerateToVector(spvReflectEnumerateDescriptorSets, sets);

    uint32_t maxDescriptorSetIndex = 0;
    for (const auto& set : sets)
    {
        maxDescriptorSetIndex = std::max(set->set, maxDescriptorSetIndex);
    }

    m_descriptorSets.resize(maxDescriptorSetIndex + 1);

    for (const auto& descriptorSet : sets)
    {
        uint32_t setIndex = descriptorSet->set;
        ExtractBindingsForDescriptorSet(descriptorSet,
                static_cast<VkShaderStageFlags>(m_module.shader_stage),
                m_descriptorSets[setIndex]);
    }
}

void ShaderReflectionFetcher::ExtractPushConstantReanges()
{
    uint32_t pushConstantCount = 0;
    spvReflectEnumeratePushConstantBlocks(&m_module, &pushConstantCount, nullptr);
    std::vector<SpvReflectBlockVariable*> pushConstants(pushConstantCount);
    spvReflectEnumeratePushConstantBlocks(&m_module, &pushConstantCount, pushConstants.data());

    for (uint32_t pcIdx = 0; pcIdx < pushConstantCount; ++pcIdx)
    {
        const SpvReflectBlockVariable& reflPC = *(pushConstants[pcIdx]);

        VkPushConstantRange vkPCRange{};
        vkPCRange.stageFlags = static_cast<VkShaderStageFlags>(m_module.shader_stage);
        vkPCRange.offset = reflPC.offset;
        vkPCRange.size = reflPC.size;

        m_pushConstantRanges.push_back(vkPCRange);
    }
}
} // namespace Muyo::RenderGraph
