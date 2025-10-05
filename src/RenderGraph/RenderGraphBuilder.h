#pragma once
#include "RenderGraph/RenderGraphResourceDesc.h"
#include "RenderGraphResourceHandle.h"
#include "DependencyGraph.h"
#include <concepts>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Muyo
{
class RenderGraphParameters
{
public:
    virtual ~RenderGraphParameters() = default;

    std::vector<RenderGraphResourceHandle> vInputResources;
    std::vector<RenderGraphResourceHandle> vOutputResources;
    virtual void OnGraphBuild() {}
    virtual void OnGraphExecute() {}
};

class MyRGParam : public RenderGraphParameters
{
};

class RenderGraphBuilder
{
public:
    // Allocate parameters for a render graph node
    template <typename T>
    requires std::derived_from<T, RenderGraphParameters>
    [[nodiscard]] T* AllocateRenderGraphNodeParameters()
    {
        m_renderGraphNodeParameters.emplace_back(std::make_unique<T>());
        return static_cast<T*>(m_renderGraphNodeParameters.back().get());
    }

    // Add a render graph node
    void AddNode(const std::string& nodeName, RenderGraphParameters* parameters);

    // Add a dependency between two nodes
    void AddDependency(const std::string& fromNode, const std::string& toNode);

    // Build the render graph
    void Build();

    void Execute();

    // Retrieve the execution order of nodes
    std::vector<std::string> GetExecutionOrder() const;

private:
    struct RenderGraphNode
    {
        std::string name;
        RenderGraphParameters* parameters;
    };

    std::vector<std::unique_ptr<RenderGraphParameters>> m_renderGraphNodeParameters;
    std::unordered_map<std::string, RenderGraphNode> m_renderGraphNodes;
    DependencyGraph<std::string> m_dependencyGraph;
    std::unordered_map<std::string, uint32_t> m_resourceLastUsedVersion;  // Track last used version of resources
};
}  // namespace Muyo
