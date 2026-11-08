#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ShaderReflectionFetcher.h"

namespace Muyo::RenderGraph
{
/// FNV-1a hash of a shader's name, used as one half of `ShaderAssetKey`.
///
/// A hash rather than the string keeps the key trivially copyable and cheap to compare; the collision
/// risk is accepted because a collision also has to agree on `contentHash` to be mistaken for a hit.
class ShaderKey
{
public:
    /// The stored hash type.
    using Value = uint32_t;

    /// Construct the zero (invalid) key.
    constexpr ShaderKey() = default;
    /// @param s Shader name, without the `.spv` suffix.
    explicit constexpr ShaderKey(std::string_view s) : m_value(Hash(s)) {}

    /// @return The hash.
    constexpr Value GetValue() const { return m_value; }

    /// @param rhs Key to compare.
    /// @return True when this key sorts before `rhs`; required for `std::map`.
    constexpr bool operator<(const ShaderKey& rhs) const noexcept { return m_value < rhs.m_value; }

    /// @param rhs Key to compare.
    /// @return True when both keys hold the same hash.
    constexpr bool operator==(const ShaderKey& rhs) const = default;

    /// Hashes a `ShaderKey` for the `std::unordered_map` used by the caches.
    struct Hasher
    {
        /// @param key Key to hash.
        /// @return Its hash.
        size_t operator()(const ShaderKey& key) const noexcept { return std::hash<uint64_t>{}(key.GetValue()); }
    };
    /// @return True when this key names a shader rather than the default-constructed zero.
    constexpr bool IsValid() const { return m_value != 0; }

private:
    Value m_value = 0;  ///< The hash, or zero for the invalid key.

    /// @param s Shader name.
    /// @return Its FNV-1a hash.
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
inline uint64_t HashSpirv(const std::vector<uint32_t>& code)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const uint32_t word : code)
    {
        hash ^= word;
        hash *= 1099511628211ULL;
    }
    return hash;
}

/// One loaded shader: its identity, SPIR-V words, module and reflection.
struct ShaderAsset
{
    ShaderAssetKey key;                       ///< Identity of the loaded content.
    std::vector<uint32_t> spirv;              ///< The SPIR-V words the module was created from.
    VkShaderModule shaderModule;              ///< The module the graph's pipelines use.
    Muyo::ShaderReflection shaderReflection;  ///< Reflection of `spirv`, used to compile descriptors.
};

/// Loads and owns shader modules for one device.
///
/// The graph-owned counterpart of the engine's former `ShaderAssetManager`, moved so the graph's public
/// headers no longer include the engine's shader header (#272). A builder without an injected
/// `ShaderCache` owns one of these and its modules live as long as the builder.
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
