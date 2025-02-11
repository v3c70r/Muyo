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

TEST_CASE("RenderDependencyGraph_Ping_Pong", "Ping Pong" ) {

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
    REQUIRE(executionLevels[0][0]->m_vOutputResources[0].m_nVersion == 1);
    REQUIRE(executionLevels[0][0]->m_vInputResources[0].m_pRenderResource->m_sName == "outputResource");
}

TEST_CASE("RenderDependencyGraph_DAG", "DAG")
{

    // Resources and Passes flow
    //        +---> Res2 -->Pass3 --->Res4                  
    // Res1   |                        |                    
    // |      |                        |                    
    // +-> Pass1                       +---> Pass3 ---> Res6
    //        |                        |                    
    //        |                        |                    
    //        +---> Res3 -->Pass2 --->Res5                  
    auto res1 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res1"});
    auto res2 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res2"});
    auto res3 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res3"});
    auto res4 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res4"});
    auto res5 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res5"});
    auto res6 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res6"});

    auto pass1 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass1"});
    auto pass2 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass2"});
    auto pass3 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass3"});
    auto pass4 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass4"});

    Muyo::RenderDependencyGraph rdg;
    rdg.AddPass({res1.get()}, {res2.get(), res3.get()}, pass1.get());
    rdg.AddPass({res2.get()}, {res4.get()}, pass3.get());
    rdg.AddPass({res3.get()}, {res5.get()}, pass2.get());
    rdg.AddPass({res4.get(), res5.get()}, {res6.get()}, pass4.get());
    rdg.ConstructAdjList();
    auto executionLevels = rdg.GetParallelExecutionLevels();

    // Dependency graph should be
    //                                                     
    //          Pass3  <---+                             
    //            |        |                             
    //            |        |                             
    //   Pass1 <--+     Pass3                            
    //            |        |                             
    //            |        |                             
    //          Pass2 <----+      
    //
    REQUIRE(executionLevels[2][0]->m_pRenderPass == pass1.get());
    REQUIRE(executionLevels[0][0]->m_pRenderPass == pass4.get());
}

TEST_CASE("RenderDependencyGraph_in_out_same_resource", "in_out_same_resource")
{
    auto res = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res"});
    auto pass1 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass1"});
    auto pass2 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass2"});

    Muyo::RenderDependencyGraph rdg;
    rdg.AddPass({res.get()}, {res.get()}, pass1.get());
    rdg.AddPass({res.get()}, {res.get()}, pass2.get());
    rdg.ConstructAdjList();
    auto executionLevels = rdg.GetParallelExecutionLevels();
    const Muyo::RenderGraphNode* node = executionLevels[0][0];
    REQUIRE(node->m_pRenderPass == pass2.get());
    REQUIRE(node->m_vOutputResources[0].m_pRenderResource == res.get());
    REQUIRE(node->m_vInputResources[0].m_nVersion == 0);
    REQUIRE(node->m_vOutputResources[0].m_nVersion == 1);
}

TEST_CASE("RenderDependencyGraph_ParallelQueues", "Parallele Queues")
{
    auto res1 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res1"});
    auto res2 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res2"});
    auto res3 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res3"});
    auto res4 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res4"});
    auto res5 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res5"});
    auto res6 = std::make_unique<Muyo::IRenderResource>(Muyo::IRenderResource{"res6"});

    auto pass1 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass1"});
    auto pass2 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass2"});
    auto pass3 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass3"});
    auto pass4 = std::make_unique<Muyo::IRenderPass>(Muyo::IRenderPass{"pass4"});

    Muyo::RenderDependencyGraph rdg;
    rdg.AddPass({res1.get()}, {res2.get(), res3.get()}, pass1.get());
    rdg.AddPass({res2.get()}, {res4.get()}, pass3.get());
    rdg.AddPass({res3.get()}, {res5.get()}, pass2.get());
    rdg.AddPass({res4.get(), res5.get()}, {res6.get()}, pass4.get());
    rdg.ConstructAdjList();

    // Dependency graph should be
    //                                                     
    //          Pass3  <---+                             
    //            |        |                             
    //            |        |                             
    //   Pass1 <--+     Pass4                            
    //            |        |                             
    //            |        |                             
    //          Pass2 <----+      
    //
    //
    // Pass 2 can be executed in async compute
    // Pass 1 -> submit on graphics queue, insert semaphore to signal pass2 and pass3
    // Pass 2 -> submit on async compute queue, wait for pass1 semaphore
    // Pass 3 -> submit on graphics queue, wait for pass1 semaphore
    // Pass 4 -> submit, wait for semaphore from both pass3 and pass2
    //
}
