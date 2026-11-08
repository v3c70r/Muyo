#pragma once
// Internal to muyo_rg: implementation detail, not part of the public surface. See docs/RenderGraph.md.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "RenderGraphShader.h"

namespace Muyo::RenderGraph
{
/// Loads and owns shader modules for one device.
///
/// Internal to `muyo_rg`: a caller shares shader modules through the public `ShaderCache`, and a builder that is
/// given no cache owns one of these privately. It is the graph-owned counterpart of the engine's former
/// `ShaderAssetManager`, moved so the graph's public headers no longer include the engine's shader header (#272).
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

    /// Load `shaders/<shaderName>.spv`, or return the identity of the entry already loaded.
    /// @param shaderName Shader name without the `.spv` suffix.
    /// @return The entry's key, or `std::nullopt` when the file is empty or the module cannot be created.
    /// @throws std::runtime_error when the file cannot be read.
    std::optional<ShaderAssetKey> LoadShader(const std::string& shaderName)
    {
        const std::filesystem::path shaderPath = std::filesystem::path("shaders") / (shaderName + ".spv");

        std::vector<uint32_t> spirv = Muyo::ReadSpirvFile(shaderPath);
        if (spirv.empty())
        {
            return std::nullopt;
        }

        // Name plus content: a rebuilt `.spv` is a different shader, not a hit on the old module.
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

        ShaderAsset asset{.key = key, .spirv = spirv, .shaderModule = shaderModule, .shaderReflection = reflection};
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
