#pragma once
#include "DependencyGraph.h"

namespace Muyo
{

class IRenderPass;
class IRenderResource;
struct RenderResourceHandle
{
    const IRenderResource* m_pRenderResource = nullptr;
    uint32_t m_nVersion = 0;
    bool operator == (const RenderResourceHandle& other) const
    {
        return m_pRenderResource == other.m_pRenderResource && m_nVersion == other.m_nVersion;
    }
};

struct RenderGraphNode
{
    std::vector<RenderResourceHandle> m_vInputResources;
    std::vector<RenderResourceHandle> m_vOutputResources;
    const IRenderPass* m_pRenderPass = nullptr;
    bool operator == (const RenderGraphNode& other) const
    {
        return m_vInputResources == other.m_vInputResources && m_vOutputResources == other.m_vOutputResources && m_pRenderPass == other.m_pRenderPass;
    }
};

class RenderDependencyGraph : public DependencyGraph<const RenderGraphNode*>
{
public:
    const RenderGraphNode* AddPass(const std::vector<const IRenderResource*>& vInputResources,
                 const std::vector<const IRenderResource*>& vOutputResources, const IRenderPass* pRenderPass)
    {
        // Bump output resource version
        m_vpRGNs.push_back(std::make_unique<RenderGraphNode>());
        RenderGraphNode& node = *m_vpRGNs.back();
        for (const auto* outputResource : vOutputResources)
        {
            if (m_mResourceVersion.count(outputResource) == 0)
            {
                m_mResourceVersion[outputResource] = 0;
            }
            else
            {
                m_mResourceVersion[outputResource]++;
            }

            node.m_vOutputResources.push_back({outputResource, m_mResourceVersion[outputResource]});
        }

        for (const auto* inputResource : vInputResources)
        {
            // record input resource version
            if (m_mResourceVersion.count(inputResource) == 0)
            {
                m_mResourceVersion[inputResource] = 0;
            }
            node.m_vInputResources.push_back({inputResource, m_mResourceVersion[inputResource]});
        }
        node.m_pRenderPass = pRenderPass;
        return m_vpRGNs.back().get();
    }

    void ConstructAdjList()
    {
        for (size_t i = 0; i < m_vpRGNs.size(); i++)
        {
            for (size_t j = i; j < m_vpRGNs.size(); j++)
            {
                const RenderGraphNode* pSourcePass = m_vpRGNs[i].get();
                const RenderGraphNode* pTargetPass = m_vpRGNs[j].get();

                for (const RenderResourceHandle& inputHandle : pTargetPass->m_vInputResources)
                {
                    // FromNode depends on ToNode, thus FromNode's input resource must be in ToNode's output resources
                    if (std::ranges::find( pSourcePass->m_vOutputResources.begin(), pSourcePass->m_vOutputResources.end(), inputHandle) != pSourcePass->m_vOutputResources.end())
                    {
                        AddEdge(pTargetPass, pSourcePass);
                        break;
                    }
                }
            }
        }
    }

private:
    std::vector<std::unique_ptr<RenderGraphNode>> m_vpRGNs;
    std::unordered_map<const IRenderResource*, uint32_t> m_mResourceVersion;    // track current resource version
};
} // namespace Muyo

namespace std
{
template <>
struct hash<Muyo::RenderResourceHandle>
{
    size_t operator()(const Muyo::RenderResourceHandle& handle) const
    {
        return hash<const Muyo::IRenderResource*>()(handle.m_pRenderResource) ^ ::std::hash<uint32_t>()(handle.m_nVersion);
    }
};

template <>
struct hash<Muyo::RenderGraphNode>
{
    size_t operator()(const Muyo::RenderGraphNode& node) const
    {
        size_t hash = 0;
        for (const Muyo::RenderResourceHandle& inputHandle : node.m_vInputResources)
        {
            hash ^= std::hash<Muyo::RenderResourceHandle>{}(inputHandle);
        }

        for (const Muyo::RenderResourceHandle& outputHandle : node.m_vOutputResources)
        {
            hash ^= std::hash<Muyo::RenderResourceHandle>{}(outputHandle);
        }

        hash ^= std::hash<const Muyo::IRenderPass*>{}(node.m_pRenderPass);
        return hash;
    }
};
}  // namespace std
