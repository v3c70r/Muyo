#include "DescriptorSetCache.h"

#include <algorithm>

namespace Muyo::RenderGraph
{
namespace
{
/// @param a Left binding.
/// @param b Right binding.
/// @return A strict weak ordering over the fields that identify a reflected binding. `pImmutableSamplers`
///         is deliberately ignored: the graph never sets an immutable sampler, so comparing it would only
///         compare null pointers.
bool LayoutBindingLess(const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b)
{
    if (a.binding != b.binding) return a.binding < b.binding;
    if (a.descriptorType != b.descriptorType) return a.descriptorType < b.descriptorType;
    if (a.descriptorCount != b.descriptorCount) return a.descriptorCount < b.descriptorCount;
    return a.stageFlags < b.stageFlags;
}
}  // namespace

bool DescriptorSetKey::Bound::operator<(const Bound& other) const
{
    if (binding != other.binding) return binding < other.binding;
    if (type != other.type) return type < other.type;
    // std::less on pointers gives the total order std::map needs; comparing pointers with `<` directly
    // is unspecified for unrelated objects.
    if (resource != other.resource) return std::less<const IRenderResource*>{}(resource, other.resource);
    if (bufferOffset != other.bufferOffset) return bufferOffset < other.bufferOffset;
    if (bufferRange != other.bufferRange) return bufferRange < other.bufferRange;
    // A sampler is a pointer handle; std::less gives the total order std::map needs.
    if (sampler != other.sampler) return std::less<VkSampler>{}(sampler, other.sampler);
    return imageLayout < other.imageLayout;
}

bool DescriptorSetKey::operator<(const DescriptorSetKey& other) const
{
    if (set != other.set) return set < other.set;
    // Compare the layout with the field comparator in both directions rather than `!=`, which would need
    // an operator== the Vulkan struct does not have.
    if (std::lexicographical_compare(layout.begin(), layout.end(), other.layout.begin(), other.layout.end(),
                                     LayoutBindingLess))
    {
        return true;
    }
    if (std::lexicographical_compare(other.layout.begin(), other.layout.end(), layout.begin(), layout.end(),
                                     LayoutBindingLess))
    {
        return false;
    }
    return bound < other.bound;
}

DescriptorSetCache::DescriptorSetCache(VkDevice device, DescriptorManager& descManager)
    : m_device(device), m_descManager(descManager)
{
}

DescriptorSetCache::~DescriptorSetCache() { Clear(); }

void DescriptorSetCache::Clear()
{
    for (auto& [key, entry] : m_entries)
    {
        if (entry.set != VK_NULL_HANDLE) m_descManager.FreeDescriptorSet(entry.set);
        if (entry.layout != VK_NULL_HANDLE) m_descManager.DestroyDescriptorSetLayout(entry.layout);
    }
    m_entries.clear();
}

DescriptorSetCache::Entry DescriptorSetCache::Acquire(const DescriptorSetKey& key,
                                                      const std::function<void(VkDescriptorSet)>& write)
{
    const auto it = m_entries.find(key);
    if (it != m_entries.end()) return it->second;

    Entry entry;
    entry.layout = m_descManager.AllocateDescriptorSetLayout(key.layout);
    entry.set = m_descManager.AllocateDescriptorSet(entry.layout);
    write(entry.set);
    m_entries.emplace(key, entry);
    return entry;
}
}  // namespace Muyo::RenderGraph
