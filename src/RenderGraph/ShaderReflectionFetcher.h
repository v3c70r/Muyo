#pragma once
#include <spirv_reflect.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cassert>
#include <vector>

namespace Muyo::RenderGraph
{
class ShaderReflectionFetcher
{
public:
    ShaderReflectionFetcher(const void* spirv_code, size_t spirv_nbytes);
    ~ShaderReflectionFetcher();

    // Public getters for testing
    const auto& GetDescriptorSets() const { return m_descriptorSets; }
    const auto& GetPushConstantRanges() const { return m_pushConstantRanges; }

private:
    // Utility to enumerate SPIRV-Reflect objects into a std::vector
    template <typename T, typename EnumFn>
    void EnumerateToVector(EnumFn enumFn, std::vector<T*>& outVec);

    static void ExtractBindingsForDescriptorSet(
            const SpvReflectDescriptorSet* reflectSet,
            VkShaderStageFlags stageFlags,
            std::vector<VkDescriptorSetLayoutBinding>& outBindings)
    {
        outBindings.clear();
        outBindings.reserve(reflectSet->binding_count);

        for (uint32_t bindingIdx = 0; bindingIdx < reflectSet->binding_count; ++bindingIdx)
        {
            const SpvReflectDescriptorBinding* reflBinding = reflectSet->bindings[bindingIdx];

            VkDescriptorSetLayoutBinding vkBinding{};
            vkBinding.binding = reflBinding->binding;
            vkBinding.descriptorType = static_cast<VkDescriptorType>(reflBinding->descriptor_type);
            vkBinding.descriptorCount = reflBinding->count;
            vkBinding.stageFlags = stageFlags;
            vkBinding.pImmutableSamplers = nullptr; // Not handled here

            outBindings.push_back(vkBinding);
        }
    }
    void ExtractDescriptorSetBindings();
    void ExtractPushConstantReanges();
    SpvReflectShaderModule m_module{};

    std::vector<std::vector<VkDescriptorSetLayoutBinding>> m_descriptorSets;
    std::vector<VkPushConstantRange> m_pushConstantRanges;
};

class ShaderReflectionPipelineBuilder
{
public:
    explicit ShaderReflectionPipelineBuilder(const std::vector<ShaderReflectionFetcher>& fetchers)
        : m_fetchers(fetchers)
    {
        // Find the maximum set index used by any fetcher
        size_t maxSetIndex = 0;
        for (const auto& fetcher : m_fetchers)
        {
            maxSetIndex = std::max(maxSetIndex, fetcher.GetDescriptorSets().size());
        }
        m_descriptorSets.resize(maxSetIndex);

        // Merge descriptor sets from all fetchers
        for (const auto& fetcher : m_fetchers)
        {
            const auto& sets = fetcher.GetDescriptorSets();
            for (size_t setIdx = 0; setIdx < sets.size(); ++setIdx)
            {
                for (const auto& binding : sets[setIdx])
                {
                    // Check if this binding already exists in the merged set
                    auto& mergedSet = m_descriptorSets[setIdx];
                    auto it = std::find_if(mergedSet.begin(), mergedSet.end(),
                        [&](const VkDescriptorSetLayoutBinding& b) { return b.binding == binding.binding; });
                    if (it == mergedSet.end())
                    {
                        mergedSet.push_back(binding);
                    }
                    else
                    {
                        // Merge stageFlags if binding already exists
                        it->stageFlags |= binding.stageFlags;
                        // Optionally, check for type/count compatibility here

                    }
                }
                // Sort bindings by binding number for driver compatibility
                std::sort(m_descriptorSets[setIdx].begin(), m_descriptorSets[setIdx].end(),
                          [](const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b) {
                              return a.binding < b.binding;
                          });
            }
        }
        // Merge push constant ranges
        for (const auto& fetcher : m_fetchers)
        {
            const auto& pcRanges = fetcher.GetPushConstantRanges();
            for (const auto& pcRange : pcRanges)
            {
                // Check if this push constant range overlaps with existing ones
                bool merged = false;
                for (auto& existingRange : m_pushConstantRanges)
                {
                    if ((pcRange.offset < existingRange.offset + existingRange.size) &&
                        (existingRange.offset < pcRange.offset + pcRange.size))
                    {
                        // Merge overlapping ranges by expanding existing range
                        uint32_t newStart = std::min(existingRange.offset, pcRange.offset);
                        uint32_t newEnd = std::max(existingRange.offset + existingRange.size,
                                                   pcRange.offset + pcRange.size);
                        existingRange.offset = newStart;
                        existingRange.size = newEnd - newStart;
                        existingRange.stageFlags |= pcRange.stageFlags;
                        merged = true;
                        break;
                    }
                }
                if (!merged)
                {
                    m_pushConstantRanges.push_back(pcRange);
                }
            }
        }
    }
private:
    std::vector<ShaderReflectionFetcher> m_fetchers;
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> m_descriptorSets;
    std::vector<VkPushConstantRange> m_pushConstantRanges;
};
}  // namespace Muyo::RenderGraph
