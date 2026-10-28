#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
    constexpr bool IsValid() const { return m_value != 0; }

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

/// Identity of a loaded shader: its name plus the content it was loaded from.
///
/// The name alone is not enough to identify a compiled shader: the `.spv` file can be rebuilt while
/// the process is running, and a module/reflection pair keyed only on the name would then be served
/// for content it does not match. The content hash makes that a miss instead. It is also what lets a
/// shared cache keep both the old and the new module alive, instead of invalidating a builder that
/// still refers to the old one.
struct ShaderAssetKey
{
    ShaderKey name;            ///< FNV-1a hash of the shader name (no `.spv` suffix).
    uint64_t contentHash = 0;  ///< Hash of the SPIR-V words; see HashSpirv().
    /// @return True when both halves name a loaded shader.
    constexpr bool IsValid() const { return name.IsValid() && contentHash != 0; }
    /// @param other Key to compare.
    /// @return True when both keys name the same content of the same shader.
    constexpr bool operator==(const ShaderAssetKey& other) const = default;
    /// Hashes both halves of the key for the cache maps.
    struct Hasher
    {
        /// @param key Key to hash.
        /// @return Hash over the name and content.
        size_t operator()(const ShaderAssetKey& key) const noexcept
        {
            return std::hash<uint64_t>{}((static_cast<uint64_t>(key.name.GetValue()) << 32) ^ key.contentHash);
        }
    };
};

/// @param code Shader SPIR-V words.
/// @return A word-wise FNV-1a hash identifying the shader content.
///
/// This is the same function (and the same offset basis) the pipeline key uses for its shader hashes.
/// It must stay so: changing either the iteration order or the constant changes every pipeline key and
/// turns a shared cache into a cold one. The module cache and the pipeline cache therefore agree on
/// what "the same shader" means.
inline uint64_t HashSpirv(const SpirvCode& code)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const uint32_t word : code)
    {
        hash ^= word;
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct ShaderAsset
{
    ShaderAssetKey key;
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

    std::optional<ShaderAssetKey> LoadShader(const std::string& shaderName)
    {
        std::filesystem::path shaderPath = std::filesystem::path("shaders") / (shaderName + ".spv");

        SpirvCode spirv = ReadSpv(shaderPath);
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
        ShaderReflection reflection = FetchShaderReflection(spirv);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            return std::nullopt;  // Failed to create shader module
        }

        ShaderAsset asset{.key = key, .spirv = spirv, .shaderModule = shaderModule, .shaderReflection = reflection};
        m_shaderAssets[key] = asset;
        return key;
    }

    const ShaderAsset* GetShaderAsset(const ShaderAssetKey& key) const
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
    std::unordered_map<ShaderAssetKey, ShaderAsset, ShaderAssetKey::Hasher> m_shaderAssets;
};
}  // namespace Muyo
