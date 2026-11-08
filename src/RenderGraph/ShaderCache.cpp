#include "RenderGraph/ShaderCache.h"

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
    return LoadShader(shaderName, m_defaultSource);
}

std::optional<ShaderAssetKey> ShaderCache::LoadShader(const std::string& shaderName,
                                                      const IShaderSourceProvider& source)
{
    std::optional<std::vector<uint32_t>> loaded = source.LoadShader(shaderName);
    if (!loaded.has_value() || loaded->empty()) return std::nullopt;
    std::vector<uint32_t> spirv = std::move(*loaded);

    const ShaderAssetKey key{ShaderKey(shaderName), HashSpirv(spirv)};
    if (m_assets.find(key) != m_assets.end()) return key;

    // Reflection first: if it throws, no module has been created that would leak.
    Muyo::ShaderReflection reflection = Muyo::FetchShaderReflection(spirv);

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
