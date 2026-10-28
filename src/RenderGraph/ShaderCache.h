#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "ShaderAsset.h"

namespace Muyo::RenderGraph
{
/// A caller-owned cache of loaded shader modules and their reflection results, shared across builders.
///
/// `RenderGraphBuilder` loads `shaders/<name>.spv`, creates a `VkShaderModule` and runs SPIR-V reflection
/// while the graph is declared. Without this cache each builder repeats that work for the same shader,
/// and the modules live only as long as that builder. Inject one through
/// `RenderGraphServices::shaderCache` to share both:
///
/// - A hit creates no module and runs no reflection. It returns the identity of the entry already in
///   the cache, so two builders resolve the same `VkShaderModule` and the same reflection object.
/// - The key is the shader **name plus a hash of its SPIR-V words** (`ShaderAssetKey`), not the name
///   alone: a `.spv` rebuilt while the process runs is a different shader, and serving the old
///   reflection for it would compile a pipeline for content that is no longer there. Both entries stay
///   alive, because a builder may still refer to the old one.
/// - The cache owns the modules. A builder never destroys a module it did not create, and a module may
///   be destroyed after the pipelines created from it (Vulkan allows this), but a later `Build()` needs
///   it again, so the cache's lifetime is what keeps it valid.
///
/// **Lifetime.** Device-scoped, and it must outlive every builder that uses it. Destroy it only after
/// those builders are gone (or at least after their last `Build()`); no entry is refcounted.
///
/// `LoadShader()` still reads the `.spv` file to identify its content, even on a hit. The save is the
/// module creation and reflection, which are the expensive parts; a caller that wants to avoid the
/// read as well would need a content identity supplied from outside, which is not this contract.
///
/// **Threading.** One thread at a time, like the builder and the caches it uses. The map is not
/// synchronized.
class ShaderCache
{
public:
    /// @param device Device the shader modules are created on. Must not be `VK_NULL_HANDLE`, and has to
    ///        outlive the cache: destroying a module needs the same device.
    /// @throws std::runtime_error when `device` is `VK_NULL_HANDLE`.
    explicit ShaderCache(VkDevice device);
    /// Destroy every module this cache created. No builder may still resolve an entry through it.
    ~ShaderCache();

    /// Owns Vulkan handles and destroys them, so copying would double-destroy a module.
    ShaderCache(const ShaderCache&) = delete;
    /// Deleted for the same reason as the copy constructor.
    ShaderCache& operator=(const ShaderCache&) = delete;
    /// Deleted because a moved-from cache would leave the old object with no modules to destroy.
    ShaderCache(ShaderCache&&) = delete;
    /// Deleted for the same reason as the move constructor.
    ShaderCache& operator=(ShaderCache&&) = delete;

    /// Load `shaders/<shaderName>.spv`, or return the identity of the entry already cached for it.
    ///
    /// On a miss this creates the module and runs reflection, then counts one module creation. A hit
    /// creates nothing and counts nothing, which is the observable reuse.
    /// @param shaderName Shader name without the `.spv` suffix, as `RenderGraphNodeCreateInfo` names it.
    /// @return The entry's key, or `std::nullopt` when the file is empty or the module cannot be created.
    /// @throws std::runtime_error when the file cannot be read; that is the same failure a builder
    ///         without a shared cache reports.
    std::optional<ShaderAssetKey> LoadShader(const std::string& shaderName);

    /// @param key Key returned by `LoadShader`.
    /// @return The module, SPIR-V words and reflection for `key`, or nullptr when it is not cached.
    const ShaderAsset* GetShaderAsset(const ShaderAssetKey& key) const;

    /// @return How many shader modules the cache currently owns.
    std::size_t GetModuleCount() const { return m_assets.size(); }

    /// @return How many modules the cache has created over its lifetime (its misses). A second builder
    ///         loading the same shaders leaves this unchanged; tests use it as the reuse evidence.
    std::uint64_t GetModuleCreateCount() const { return m_moduleCreateCount; }

    /// @return The device the modules were created on, so a builder can reject a cache from another one.
    VkDevice GetDevice() const { return m_device; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    std::unordered_map<ShaderAssetKey, ShaderAsset, ShaderAssetKey::Hasher> m_assets;
    std::uint64_t m_moduleCreateCount = 0;
};
}  // namespace Muyo::RenderGraph
