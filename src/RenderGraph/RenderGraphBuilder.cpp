#include "RenderGraphBuilder.h"
#include <stdexcept>

namespace Muyo
{
void RenderGraphBuilder::AddNode(const std::string& nodeName, RenderGraphParameters* parameters)
{
    if (m_renderGraphNodes.find(nodeName) != m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node with name '" + nodeName + "' already exists in the render graph.");
    }

    m_renderGraphNodes[nodeName] = {nodeName, parameters};
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

    // Additional build logic can be added here if needed
}

std::vector<std::string> RenderGraphBuilder::GetExecutionOrder() const
{
    return m_dependencyGraph.TopologicalSort();
}

}  // namespace Muyo
