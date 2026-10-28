#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Muyo
{
class VkRenderDevice;
}

namespace Muyo::RenderGraph
{
/// A caller-owned cache of `VkPipeline` objects, shared across render graph builders.
///
/// A `RenderGraphBuilder` caches the pipelines it compiles, but that cache belongs to the builder. Two
/// builders - a second graph, or a builder recreated after a swapchain change - therefore compile
/// identical pipelines independently, and a builder's teardown releases only the pipelines it created.
/// This type moves ownership out of the builder: entries live until `Reset()` or the cache's own
/// destruction, and a builder that is given one borrows entries instead of owning them.
///
/// Inject it through `RenderGraphServices::pipelineCache`. When the field is null the builder keeps its
/// existing per-builder cache, so a caller that never shares anything needs no extra object.
///
/// **Lifetime.** The cache is device-scoped and must outlive every builder that uses it. It is *not*
/// safe to destroy it while a builder or a submitted execution still names one of its entries; the
/// device must be idle before the cache is destroyed or `Reset()` is called. A builder validates that the
/// cache belongs to the same `VkDevice` at construction, because an entry created on another device is a
/// handle the driver cannot use.
///
/// **Key.** The key is opaque to this type. `RenderGraphBuilder` builds it from the full pipeline state
/// (the `PSODesc`, shader content, attachment formats, specialization values and queue type) as a byte
/// string, using the same function as the per-builder fallback, so the two paths cannot disagree about
/// what "the same pipeline" means. Different declarations therefore miss even when they produce
/// compatible drivers state: correctness comes before a higher hit rate.
///
/// **Threading.** One thread at a time, like the builder and the device objects it borrows. The map is
/// not synchronized.
///
/// **Driver cache.** The object also owns a `VkPipelineCache`, passed to every pipeline creation a
/// builder performs, so the driver can reuse its own compiled state across runs. `GetSerializedData()`
/// returns the bytes `vkGetPipelineCacheData` produces; loading and saving a file is the caller's policy,
/// not this type's, and the same is true of deciding where it lives. A `PipelineCache` created from
/// those bytes starts with the driver's cache loaded and an empty map of `VkPipeline` handles: the
/// handles are process-local, but the key path is the same on the next run.
class PipelineCache
{
public:
    /// @param renderDevice Device the cached pipelines are created on. Must be non-null; it has to
    ///        outlive the cache, because destroying a cached pipeline goes through its `VkDevice`.
    /// @throws std::runtime_error when `renderDevice` is null or has no logical device.
    explicit PipelineCache(VkRenderDevice* renderDevice);
    /// Create a cache with driver data from an earlier `GetSerializedData()` on the same device.
    ///
    /// The data is handed to `vkCreatePipelineCache` unchanged. It is the caller's job to have saved it
    /// from this device: Vulkan's header carries the vendor, device and pipeline-cache UUID, and a
    /// driver may ignore data that does not match. This type does not parse or validate the header.
    /// @param renderDevice Device the cached pipelines are created on; same requirements as above.
    /// @param initialData  Bytes from `GetSerializedData()`, or an empty span for a cold cache.
    /// @throws std::runtime_error when `renderDevice` is null or has no logical device.
    PipelineCache(VkRenderDevice* renderDevice, std::span<const std::uint8_t> initialData);
    /// Destroy every cached pipeline and the `VkPipelineCache` itself. The device must be idle, and no
    /// builder or execution may still refer to an entry - the same requirement as `Reset()`.
    ~PipelineCache();

    /// Owns raw Vulkan handles and destroys them, so copying would double-destroy a pipeline.
    PipelineCache(const PipelineCache&) = delete;
    /// Deleted for the same reason as the copy constructor.
    PipelineCache& operator=(const PipelineCache&) = delete;
    /// Deleted because a moved-from cache would leave the old object with no entries to destroy.
    PipelineCache(PipelineCache&&) = delete;
    /// Deleted for the same reason as the move constructor.
    PipelineCache& operator=(PipelineCache&&) = delete;

    /// @param key Byte-string key produced by the builder's full-state pipeline key.
    /// @return The cached pipeline, or `VK_NULL_HANDLE` when `key` has no entry.
    VkPipeline Find(std::string_view key) const;

    /// Take ownership of a pipeline and cache it under `key`.
    ///
    /// A pipeline is created only after `Find()` missed, so a duplicate key means two builders raced, not
    /// that the caller picked the wrong entry. The cache keeps the first pipeline and reports the
    /// duplicate; the caller keeps ownership of `pipeline` and has to destroy it (or adopt the existing
    /// entry after another `Find()`).
    /// @param key      Byte-string key produced by the builder's full-state pipeline key.
    /// @param pipeline Pipeline created on `GetDevice()`; ownership transfers only on success.
    /// @return True when the entry was stored and is now owned by the cache; false when `key` was taken.
    bool Insert(std::string_view key, VkPipeline pipeline);

    /// Destroy every cached pipeline and clear the map. Safe to call repeatedly.
    ///
    /// Only after the GPU is idle: a cached pipeline may be recorded in a command buffer that is still
    /// pending (`VUID-vkDestroyPipeline-pipeline-00765`). Builders that borrowed an entry must also be
    /// done with it; the cache has no way to know about the handles they hold.
    ///
    /// The underlying `VkPipelineCache` is **not** reset: this releases the `VkPipeline` handles, not the
    /// driver's data. Destroy the object and create a new one to discard that too.
    void Reset();

    /// @return How many pipelines the cache currently owns. Used by tests and diagnostics to observe
    ///         reuse across builders; it is not a frame-cost metric.
    std::size_t GetPipelineCount() const { return m_pipelines.size(); }

    /// @return The logical device the entries were created on. A builder compares this to its own device
    ///         before borrowing, so a cache shared with the wrong device is a named error.
    VkDevice GetDevice() const { return m_device; }

    /// @return The `VkPipelineCache` passed to pipeline creation. Never `VK_NULL_HANDLE` for a live
    ///         object; the builder passes it to every graphics, compute and ray-tracing create call.
    VkPipelineCache GetVkPipelineCache() const { return m_vkPipelineCache; }

    /// @return The driver's pipeline-cache bytes (`vkGetPipelineCacheData`), or an empty vector when the
    ///         driver reports none.
    ///
    /// The bytes are what a caller persists and hands back to the `initialData` constructor. The device
    /// must be idle while this runs: Vulkan requires host access to a `VkPipelineCache` to be externally
    /// synchronized, and a pipeline creation on another thread is the conflict.
    std::vector<std::uint8_t> GetSerializedData() const;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkPipeline> m_pipelines;
};
}  // namespace Muyo::RenderGraph
