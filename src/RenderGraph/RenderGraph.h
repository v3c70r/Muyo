#pragma once
#include "DependencyGraph.h"

namespace Muyo
{

class IRenderPass;
class IRenderResource;
struct RenderResourceHandle
{
    const IRenderResource* pRenderResource = nullptr;
    uint32_t nVersion = 0;
    bool operator == (const RenderResourceHandle& other) const
    {
        return pRenderResource == other.pRenderResource && nVersion == other.nVersion;
    }
};

struct RenderGraphNode
{
    std::vector<RenderResourceHandle> m_vInputResources;
    std::vector<RenderResourceHandle> m_vOutputResources;
    IRenderPass* m_pRenderPass = nullptr;
    bool operator == (const RenderGraphNode& other) const
    {
        return m_vInputResources == other.m_vInputResources && m_vOutputResources == other.m_vOutputResources && m_pRenderPass == other.m_pRenderPass;
    }
};

class RenderDependencyGraph : DependencyGraph<const RenderGraphNode*>
{
public:
    void AddNode(const std::vector<const IRenderResource*>&& vInputResources,
                 const std::vector<const IRenderResource*>&& vOutputResources, IRenderPass* pRenderPass)
    {
        // Bump output resource version
        m_vpRGNs.push_back(std::make_unique<RenderGraphNode>());
        RenderGraphNode& node = *m_vpRGNs.back();
        for (auto* outputResource : vOutputResources)
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

        for (auto* inputResource : vInputResources)
        {
            node.m_vInputResources.push_back({inputResource, m_mResourceVersion[inputResource]});
        }
        node.m_pRenderPass = pRenderPass;
    }

    void ConstructAdjList()
    {
        for (size_t i = 0; i < m_vpRGNs.size(); i++)
        {
            for (size_t j = i; i < m_vpRGNs.size(); j++)
            {
                for (const RenderResourceHandle& inputHandle : m_vpRGNs[j]->m_vInputResources)
                {
                    const RenderGraphNode* pFromNode = m_vpRGNs[i].get();
                    const RenderGraphNode* pToNode = m_vpRGNs[j].get();

                    // FromNode depends on ToNode, thus FromNode's input resource must be in ToNode's output resources

                    if (std::find(pFromNode->m_vInputResources.begin(), pFromNode->m_vInputResources.end(),
                                  inputHandle) != pFromNode->m_vOutputResources.end())
                    {
                        AddEdge(pFromNode, pToNode);
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
}    // namespace Muyo

namespace std
{
template <>
struct hash<Muyo::RenderResourceHandle>
{
    size_t operator()(const Muyo::RenderResourceHandle& handle) const
    {
        return hash<const Muyo::IRenderResource*>()(handle.pRenderResource) ^ ::std::hash<uint32_t>()(handle.nVersion);
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

        hash ^= std::hash<Muyo::IRenderPass*>{}(node.m_pRenderPass);
        return hash;
    }
};
}  // namespace std
