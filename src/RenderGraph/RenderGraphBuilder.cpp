#include "RenderGraphBuilder.h"
#include <stdexcept>
#include <unordered_map>

namespace Muyo
{
void RenderGraphBuilder::AddNode(const std::string& nodeName, RenderGraphParameters* parameters)
{
    if (m_renderGraphNodes.find(nodeName) != m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node with name '" + nodeName + "' already exists in the render graph.");
    }

    m_renderGraphNodes[nodeName] = {.name = nodeName, .parameters = parameters};
}

void RenderGraphBuilder::AddDependency(const std::string& fromNode, const std::string& toNode)
{
    if (m_renderGraphNodes.find(fromNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + fromNode + "' does not exist in the render graph.");
    }

    if (m_renderGraphNodes.find(toNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + toNode + "' does not exist in the render graph.");
    }

    if (!m_dependencyGraph.AddEdge(fromNode, toNode))
    {
        throw std::runtime_error("Adding dependency from '" + fromNode + "' to '" + toNode + "' creates a cycle in the render graph.");
    }
}

void RenderGraphBuilder::Build()
{
    if (m_dependencyGraph.HasCycle())
    {
        throw std::runtime_error("Render graph contains a cycle!");
    }
    std::unordered_map<std::string, uint32_t> resourceCurrentVersions;
    std::vector<std::string> executionOrder = m_dependencyGraph.TopologicalSort();
    for (const auto& nodeName: executionOrder)
    {
        // Update handle versions
        auto& node = m_renderGraphNodes.at(nodeName);
        for (auto& resource : node.parameters->m_vInputResources)
        {
            std::string key = std::string(resource.GetName());
            if (resourceCurrentVersions.find(key) == resourceCurrentVersions.end())
            {
                resourceCurrentVersions[key] = 0;
                m_resourceLastUsedVersion[key] = 0;
            }
        }

        m_renderGraphNodes.at(nodeName).parameters->OnGraphBuild();
    }
    // Additional build logic can be added here if needed
}

std::vector<std::string> RenderGraphBuilder::GetExecutionOrder() const
{
    return m_dependencyGraph.TopologicalSort();
}

}  // namespace Muyo
