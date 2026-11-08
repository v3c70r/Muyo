#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
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

}  // namespace Muyo::RenderGraph
