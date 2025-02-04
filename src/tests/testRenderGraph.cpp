#include <catch2/catch_test_macros.hpp>
#include <memory>
#include "RenderGraph/DependencyGraph.h"
#include "RenderGraph/RenderGraph.h"


namespace Muyo
{
class IRenderResource
{
public:
    std::string sName;
};

class IRenderPass
{
public:
    std::string sName;
};
}

TEST_CASE("DependencyGraph_TopSort", "TopSort")
{
    Muyo::DependencyGraph<int> graph;
    for (int i = 1; i <= 10; i++)
    {
        graph.AddEdge(i, i + 1);
    }
    graph.TopologicalSort();
    auto sortedNodes = graph.TopologicalSort();
    for (int i = 0; i <= 10; i++)
    {
        REQUIRE(sortedNodes[i] == i + 1);
    }
}

TEST_CASE("DependencyGraph_ParallelExecutionLevels", "ParallelExecutionLevels")
{
    Muyo::DependencyGraph<int> graph;

    // 1 -> 2 -> 3 -> 4 -> 7
    //  \-> 5 -> 6 --/
    //  1 depends on 2 and 5 ...
    //  so 7 needs to be excuted first.
    graph.AddEdge(1, 2);
    graph.AddEdge(1, 5);
    graph.AddEdge(2, 3);
    graph.AddEdge(3, 4);
    graph.AddEdge(4, 7);
    graph.AddEdge(5, 6);
    graph.AddEdge(6, 4);

    auto executionLevels = graph.GetParallelExecutionLevels();

    std::vector<std::vector<int>> expectedLevels = {
        {1},
        {2, 5},
        {3, 6},
        {4},
        {7},
    };
    REQUIRE(executionLevels == expectedLevels);
}

TEST_CASE( "Factorials are computed", "[factorial]" ) {

    auto inputResource = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"inputResource"});
    auto outputResource = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"outputResource"});

    auto pingPass = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pingPass"});
    auto pongPass = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pongPass"});

    // construct two render graph nodes
    Muyo::RenderResourceHandle pingInputResource = 
    {
        .pRenderResource = inputResource.get(),
        .nVersion = 0
    };

    Muyo::RenderResourceHandle pingOutputResource = 
    {
        .pRenderResource = outputResource.get(),
        .nVersion = 0
    };
    Muyo::RenderGraphNode pingNode = {
        .m_vInputResources = {pingInputResource},
        .m_vOutputResources = {pingOutputResource},
        .m_pRenderPass = pingPass.get()
    };

    // Pong node
    Muyo::RenderResourceHandle pongInputResource = 
    {
        .pRenderResource = outputResource.get(),
        .nVersion = 1 // bump version since it's an output resource of ping
    };
    Muyo::RenderResourceHandle pongOutputResource = 
    {
        .pRenderResource = inputResource.get(),
        .nVersion = 0
    };
    Muyo::RenderGraphNode pongNode = {
        .m_vInputResources = {pongInputResource},
        .m_vOutputResources = {pongOutputResource},
        .m_pRenderPass = pongPass.get()
    };

    Muyo::DependencyGraph<Muyo::RenderGraphNode> graph;
    graph.AddEdge(pingNode, pongNode);
    auto sortedNodes = graph.TopologicalSort();

    REQUIRE(sortedNodes.size() == 2);
}


