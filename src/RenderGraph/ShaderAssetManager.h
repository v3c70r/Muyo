#pragma once
// Internal to muyo_rg: implementation detail, not part of the public surface. See docs/RenderGraph.md.
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "RenderGraphShader.h"
#include "ShaderSourceProvider.h"

namespace Muyo::RenderGraph
{
/// Loads and owns shader modules for one device.
///
/// Internal to `muyo_rg`: a caller shares shader modules through the public `ShaderCache`, and a builder that is
/// given no cache owns one of these privately. It is the graph-owned counterpart of the engine's former
/// `ShaderAssetManager`, moved so the graph's public headers no longer include the engine's shader header (#272).
/// The source of the words is not its state: every load is handed the provider the builder was given, so
/// the same manager serves the default file path and a host-supplied provider (#292).
class ShaderAssetManager
{
public:
    /// @param device Device the modules are created on; must outlive this manager.
    explicit ShaderAssetManager(VkDevice device) : m_device(device) {}
    /// Destroy every module this manager created.
    ~ShaderAssetManager()
    {
        for (auto& [key, asset] : m_shaderAssets)
        {
            vkDestroyShaderModule(m_device, asset.shaderModule, nullptr);
        }
    }

    /// Owns Vulkan handles, so copying would double-destroy a module.
    ShaderAssetManager(const ShaderAssetManager&) = delete;
    /// See the copy constructor.
    ShaderAssetManager& operator=(const ShaderAssetManager&) = delete;

    /// Load `shaderName` from `source`, or return the identity of the entry already loaded.
    /// @param shaderName Shader name without the `.spv` suffix.
    /// @param source Provider of the shader's SPIR-V words; must outlive the call.
    /// @return The entry's key, or `std::nullopt` when the source has no words or the module cannot be created.
    /// @throws std::runtime_error when the source throws; the default file provider does so for a missing file.
    std::optional<ShaderAssetKey> LoadShader(const std::string& shaderName, const IShaderSourceProvider& source)
    {
        std::optional<std::vector<uint32_t>> loaded = source.LoadShader(shaderName);
        if (!loaded.has_value() || loaded->empty()) return std::nullopt;
        std::vector<uint32_t> spirv = std::move(*loaded);

        // Name plus content: a rebuilt `.spv` - or a provider that changes its answer - is a different
        // shader, not a hit on the old module.
        const ShaderAssetKey key{ShaderKey(shaderName), HashSpirv(spirv)};
        if (m_shaderAssets.find(key) != m_shaderAssets.end())
        {
            return key;
        }

        // Reflection first: if it throws, no module has been created that would leak.
        Muyo::ShaderReflection reflection = Muyo::FetchShaderReflection(spirv);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            return std::nullopt;  // Failed to create shader module
        }

        ShaderAsset asset{
            .key = key, .spirv = std::move(spirv), .shaderModule = shaderModule, .shaderReflection = reflection};
        m_shaderAssets[key] = std::move(asset);
        return key;
    }

    /// @param key Key returned by `LoadShader`.
    /// @return The asset for `key`, or nullptr when it is not loaded.
    const ShaderAsset* GetShaderAsset(const ShaderAssetKey& key) const
    {
        const auto it = m_shaderAssets.find(key);
        return it == m_shaderAssets.end() ? nullptr : &it->second;
    }

private:
    VkDevice m_device;  ///< Device owning the modules.
    std::unordered_map<ShaderAssetKey, ShaderAsset, ShaderAssetKey::Hasher> m_shaderAssets;  ///< Loaded entries.
};
}  // namespace Muyo::RenderGraph
