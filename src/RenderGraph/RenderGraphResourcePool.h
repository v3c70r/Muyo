#pragma once
#include <unordered_map>
#include <vector>
#include <string_view>

#include "RenderGraphResourceDesc.h"
#include "RenderResourceManager.h"
namespace Muyo::RenderGraph
{
class RenderGraphResourcePool
{
public:
    // Use the hasher we built in the previous step
    using ResourceMap = std::unordered_map<ResourceDesc, std::vector<Muyo::IRenderResource*>, ResourceDescHasher>;

    ResourceBucketPool(Muyo::RenderResourceManager* rm) : m_rm(rm) {}

    // Acquire: Pop from bucket or create new
    Muyo::IRenderResource* Acquire(const ResourceDesc& desc, std::string_view name)
    {
        auto& bucket = m_availableResources[desc];

        if (!bucket.empty())
        {
            Muyo::IRenderResource* res = bucket.back();
            bucket.pop_back();
            // Optionally update the debug name for the new use case
            // m_rm->UpdateDebugName(res, name);
            return res;
        }

        // Bucket empty: Create a new physical resource
        // This uses the ADL Allocate we defined earlier
        return Muyo::Allocate(desc, m_rm);
    }

    // Release: Put back into the bucket
    void Release(const ResourceDesc& desc, Muyo::IRenderResource* resource)
    {
        m_availableResources[desc].push_back(resource);
    }

    // Cleanup: Call this when shutting down the engine
    void Clear()
    {
        // Note: In a real engine, you'd tell the RM to actually
        // destroy these pointers or let the RM's own lifecycle manage them.
        m_availableResources.clear();
    }

private:
    Muyo::RenderResourceManager* m_rm;
    ResourceMap m_availableResources;
};
}  // namespace Muyo::RenderGraph
