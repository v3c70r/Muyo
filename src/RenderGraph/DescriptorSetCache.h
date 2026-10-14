#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "DescriptorManager.h"
#include "RenderGraph/RenderGraphNodeResource.h"

namespace Muyo::RenderGraph
{
/// Everything that decides a descriptor set's contents, so two nodes that would write the same set share
/// one.
///
/// A descriptor set holds *concrete views* - a `VkBuffer` handle, a `VkImageView` handle - so its identity
/// is the set of resources bound into it, not the declaration that named them. Two nodes binding the same
/// buffer at the same (set, binding) therefore have the same key and share one `VkDescriptorSet`; two
/// nodes binding different buffers (including two aliased transients over one block, which are different
/// `VkImage`s) have different keys and get different sets. That is what makes a content-addressed cache
/// safe under aliasing: it cannot hand one node another node's view.
///
/// The reflected layout is part of the key, so two nodes that bind different descriptor *types* at the
/// same set index never share either. The layout and the bound resources are stored in a canonical order
/// (by binding), because reflection does not promise one.
struct DescriptorSetKey
{
    /// One resource bound into a binding, with everything the write needs.
    struct Bound
    {
        uint32_t binding = 0;                                   ///< Binding within the set.
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;    ///< Descriptor type, from reflection.
        const IRenderResource* resource = nullptr;              ///< Bound resource, by identity.
        VkDeviceSize bufferOffset = 0;                          ///< Buffer bind offset, already resolved.
        VkDeviceSize bufferRange = 0;                           ///< Buffer bind range, already resolved.
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;  ///< Image layout the descriptor samples in.

        /// @param other Binding to compare against.
        /// @return A strict weak ordering, so the bound set can key a `std::map`.
        bool operator<(const Bound& other) const;
    };

    uint32_t set = 0;                                  ///< Descriptor set index.
    std::vector<VkDescriptorSetLayoutBinding> layout;  ///< Reflected bindings, sorted by binding.
    std::vector<Bound> bound;                          ///< Bound resources, sorted by binding.

    /// @param other Key to compare against.
    /// @return A strict weak ordering, so a key set can be a `std::map`.
    bool operator<(const DescriptorSetKey& other) const;
};

/// Owns graph descriptor sets and reuses one per distinct set of contents.
///
/// Set allocation charges the pool a set's declared descriptor counts, and a bindless array is 1024 of
/// them. Allocating one set per node duplicated that; this allocates one set per *content* instead, so a
/// material-binding node that binds the same material buffer as another pays once. The cache owns every
/// set and layout it creates and frees them together.
///
/// The cache is cleared between builds, because a rebuild may resolve a resource to a new handle and the
/// old set would then be stale. Retaining frame-invariant sets across builds is a later step (see
/// `docs/DescriptorSet-Lifecycle-Design.md`).
class DescriptorSetCache
{
public:
    /// One cache entry: the layout a set was allocated from and the set itself.
    struct Entry
    {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;  ///< Shared layout, freed by the cache.
        VkDescriptorSet set = VK_NULL_HANDLE;           ///< Shared set, freed by the cache.
    };

    /// @param device      Device the sets are created on.
    /// @param descManager Manager owning the pool and layout creation.
    DescriptorSetCache(VkDevice device, DescriptorManager& descManager);
    /// Frees every set and layout this cache owns.
    ~DescriptorSetCache();

    /// The cache owns raw Vulkan handles, so copying would double-free.
    DescriptorSetCache(const DescriptorSetCache&) = delete;
    /// Deleted for the same reason as the copy constructor.
    DescriptorSetCache& operator=(const DescriptorSetCache&) = delete;

    /// Return the set for a key, allocating and writing it on first use.
    ///
    /// @param key   What the set must contain.
    /// @param write Called with a freshly allocated set to fill it; never called on a cache hit.
    /// @return The layout and set for that content.
    Entry Acquire(const DescriptorSetKey& key, const std::function<void(VkDescriptorSet)>& write);

    /// Free every set and layout. Called between builds, so a rebuild cannot reuse a stale view.
    void Clear();

    /// @return How many distinct sets the cache holds; the dedup observable for tests.
    std::size_t SetCount() const { return m_entries.size(); }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    DescriptorManager& m_descManager;
    std::map<DescriptorSetKey, Entry> m_entries;
};
}  // namespace Muyo::RenderGraph
