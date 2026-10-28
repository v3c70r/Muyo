#include "RenderGraph/ShaderCache.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace Muyo::RenderGraph
{
ShaderCache::ShaderCache(VkDevice device) : m_device(device)
{
    if (m_device == VK_NULL_HANDLE)
    {
        throw std::runtime_error(
            "ShaderCache: device is VK_NULL_HANDLE, so there is no device to create or "
            "destroy shader modules on.");
    }
}

ShaderCache::~ShaderCache()
{
    for (const auto& entry : m_assets)
    {
        if (entry.second.shaderModule != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_device, entry.second.shaderModule, nullptr);
    }
    m_assets.clear();
}

std::optional<ShaderAssetKey> ShaderCache::LoadShader(const std::string& shaderName)
{
    const std::filesystem::path shaderPath = std::filesystem::path("shaders") / (shaderName + ".spv");

    SpirvCode spirv = ReadSpv(shaderPath);
    if (spirv.empty()) return std::nullopt;

    const ShaderAssetKey key{ShaderKey(shaderName), HashSpirv(spirv)};
    if (m_assets.find(key) != m_assets.end()) return key;

    // Reflection first: if it throws, no module has been created that would leak.
    ShaderReflection reflection = FetchShaderReflection(spirv);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) return std::nullopt;

    m_assets.emplace(key, ShaderAsset{.key = key,
                                      .spirv = std::move(spirv),
                                      .shaderModule = shaderModule,
                                      .shaderReflection = std::move(reflection)});
    ++m_moduleCreateCount;
    return key;
}

const ShaderAsset* ShaderCache::GetShaderAsset(const ShaderAssetKey& key) const
{
    const auto it = m_assets.find(key);
    return it == m_assets.end() ? nullptr : &it->second;
}
}  // namespace Muyo::RenderGraph
