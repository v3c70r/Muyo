#include <catch2/catch_test_macros.hpp>
#include <memory>
#include "RenderGraph/DependencyGraph.h"
#include "RenderGraph/RenderGraph.h"

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

namespace Muyo
{
class IRenderResource
{
public:
    std::string m_sName;
};

class IRenderPass
{
public:
    std::string m_sName;
};
}

TEST_CASE( "RenderDependencyGraph_Ping_Pong", "Ping Pong" ) {

    // pongPass -> pingPass Pong pass depends on ping pass

    auto inputResource = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"inputResource"});
    auto outputResource = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"outputResource"});

    auto pingPass = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pingPass"});
    auto pongPass = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pongPass"});

    Muyo::RenderDependencyGraph rdg;
    rdg.AddPass({inputResource.get()}, {outputResource.get()}, pingPass.get());
    rdg.AddPass({outputResource.get()}, {inputResource.get()}, pongPass.get());
    rdg.ConstructAdjList();
    auto executionLevels = rdg.GetParallelExecutionLevels();

    REQUIRE(executionLevels[0][0]->m_pRenderPass == pongPass.get());
    REQUIRE(executionLevels[1][0]->m_pRenderPass == pingPass.get());
    REQUIRE(executionLevels[0][0]->m_vOutputResources[0].nVersion == 1);
    REQUIRE(executionLevels[0][0]->m_vInputResources[0].pRenderResource->m_sName == "outputResource");
}

