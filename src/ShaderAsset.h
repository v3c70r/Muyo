#pragma once
#include <vulkan/vulkan.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>

#include "ShaderReflection.h"
#include "ShaderReflectionFetcher.h"
namespace Muyo
{
// Centralized shader manager


class ShaderKey
{
public:
    using Value = uint32_t;

    constexpr ShaderKey() = default;
    explicit constexpr ShaderKey(std::string_view s) : m_value(Hash(s)) {}

    constexpr Value GetValue() const { return m_value; }

    // Required for std::map
    constexpr bool operator<(const ShaderKey& rhs) const noexcept { return m_value < rhs.m_value; }

    constexpr bool operator==(const ShaderKey&) const = default;

    struct Hasher
    {
        size_t operator()(const ShaderKey& key) const noexcept { return std::hash<uint64_t>{}(key.GetValue()); }
    };
    constexpr bool IsValid() const
    {
        return m_value != 0;
    }

private:
    Value m_value = 0;

    static constexpr Value Hash(std::string_view s)
    {
        uint32_t h = 2166136261U;
        for (char c : s)
        {
            h ^= static_cast<uint8_t>(c);
            h *= 16777619U;
        }
        return h;
    }
};

struct ShaderAsset
{
    ShaderKey key;
    std::vector<uint32_t> spirv;
    VkShaderModule shaderModule;
    ShaderReflection shaderReflection;
};

inline SpirvCode ReadSpv(const std::filesystem::path& spvPath)
{
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open file");
    std::streamsize size = file.tellg();
    if (size % sizeof(uint32_t) != 0) throw std::runtime_error("SPIR-V file size is not a multiple of 4");
    file.seekg(0, std::ios::beg);
    SpirvCode code(static_cast<size_t>(size) / sizeof(uint32_t));
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) throw std::runtime_error("Failed to read file");
    return code;
}

class ShaderAssetManager
{
public:
    explicit ShaderAssetManager(VkDevice device) : m_device(device) {}
    ~ShaderAssetManager()
    {
        for (auto& [key, asset] : m_shaderAssets)
        {
            vkDestroyShaderModule(m_device, asset.shaderModule, nullptr);
        }
    }

    std::optional<ShaderKey> LoadShader(const std::string& shaderName)
    {
        ShaderKey key(shaderName);

        if (m_shaderAssets.find(key) != m_shaderAssets.end())
        {
            return key;
        }

        std::filesystem::path shaderPath = std::filesystem::path("shaders") / (shaderName + ".spv");

        SpirvCode spirv = ReadSpv(shaderPath);
        if (spirv.empty())
        {
            return std::nullopt;
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            return std::nullopt;  // Failed to create shader module
        }

        // Construct shader refleciton info
        ShaderReflection reflection = FetchShaderReflection(spirv);

        ShaderAsset asset{.key = key, .spirv = spirv, .shaderModule = shaderModule, .shaderReflection = reflection};
        m_shaderAssets[key] = asset;
        return key;
    }

    const ShaderAsset* GetShaderAsset(const ShaderKey& key) const
    {
        auto it = m_shaderAssets.find(key);
        if (it != m_shaderAssets.end())
        {
            return &it->second;
        }
        return nullptr;
    }

private:
    VkDevice m_device;
    std::unordered_map<ShaderKey, ShaderAsset, ShaderKey::Hasher> m_shaderAssets;
};
}  // namespace Muyo
