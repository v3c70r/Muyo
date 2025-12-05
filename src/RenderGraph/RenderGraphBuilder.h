#pragma once
#include "RenderGraphResourceHandle.h"
#include "DependencyGraph.h"
#include "RenderGraphNodePipelineLayoutDesc.h"
#include <concepts>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Muyo::RenderGraph
{
class RenderGraphNodeParameters
{
    friend class RenderGraphBuilder;
public:
    virtual void OnGraphBuild() {}
    virtual void OnGraphExecute() {}
    virtual ~RenderGraphNodeParameters() = default;
private:
    VkPipelineLayout CreatePipelineLayout();

    // Store names and versions of input and output resources
    std::vector<RenderGraphResourceHandle> m_inputResources;
    std::vector<RenderGraphResourceHandle> m_outputResources;

    // Store pipeline layout description and resource description to create actual resource
    PipelineLayoutDesc m_pipelineLayoutDesc;
};

class RenderGraphBuilder
{
public:
    // Allocate parameters for a render graph node
    template <typename T>
    requires std::derived_from<T, RenderGraphNodeParameters>
    [[nodiscard]] T* AllocateRenderGraphNodeParameters(
            PipelineLayoutDesc&& inputDesc = {},
            PipelineLayoutDesc&& outputDesc = {}
            )
    {
        auto& param = m_renderGraphNodeParameters.emplace_back(std::make_unique<T>());

        // Construct input and output handles from layout description resource names
        for (const auto& descSet : inputDesc.vDescriptorSets)
        {
            for (const auto& bindingVariant : descSet.bindings)
            {
                std::visit([&param](auto&& binding) { param->m_inputResources.emplace_back(GetDescName(binding)); },
                           bindingVariant);
            }
        }
        for (const auto& descSet : outputDesc.vDescriptorSets)
        {
            for (const auto& bindingVariant : descSet.bindings)
            {
                std::visit([&param](auto&& binding) { param->m_outputResources.emplace_back(GetDescName(binding)); },
                           bindingVariant);
            }
        }

        param->m_pipelineLayoutDesc = std::move(inputDesc);
        param->m_pipelineLayoutDesc.Append(outputDesc);

        return static_cast<T*>(m_renderGraphNodeParameters.back().get());
    }

    // Add a render graph node
    void AddNode(const std::string& nodeName, RenderGraphNodeParameters* parameters);

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
        RenderGraphNodeParameters* parameters;
    };

    std::vector<std::unique_ptr<RenderGraphNodeParameters>> m_renderGraphNodeParameters;
    std::unordered_map<std::string, RenderGraphNode> m_renderGraphNodes;
    DependencyGraph<std::string> m_dependencyGraph;
    std::unordered_map<std::string, uint32_t> m_resourceLastUsedVersion;  // Track last used version of resources
};
}  // namespace Muyo
