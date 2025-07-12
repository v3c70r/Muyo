#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include "RenderGraph/RenderGraphBuilder.h"

namespace Muyo
{

TEST_CASE("RenderGraphBuilder: AddNode and GetExecutionOrder", "[RenderGraphBuilder]")
{ 


    RenderGraphBuilder builder;

    // Allocate parameters for nodes
    auto* nodeAParams = builder.AllocateRenderGraphNodeParameters<MyRGParam>();
    auto* nodeBParams = builder.AllocateRenderGraphNodeParameters<MyRGParam>();
    auto* nodeCParams = builder.AllocateRenderGraphNodeParameters<MyRGParam>();

    // Add nodes
    builder.AddNode("NodeA", nodeAParams);
    builder.AddNode("NodeB", nodeBParams);
    builder.AddNode("NodeC", nodeCParams);

    // Add dependencies
    builder.AddDependency("NodeA", "NodeB");
    builder.AddDependency("NodeB", "NodeC");

    // Build the graph
    REQUIRE_NOTHROW(builder.Build());

    // Get execution order
    auto executionOrder = builder.GetExecutionOrder();

    // Verify execution order
    REQUIRE(executionOrder.size() == 3);
    REQUIRE(executionOrder[0] == "NodeA");
    REQUIRE(executionOrder[1] == "NodeB");
    REQUIRE(executionOrder[2] == "NodeC");
}

TEST_CASE("RenderGraphBuilder: Detect Cycle", "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder;

    // Allocate parameters for nodes
    auto* nodeAParams = builder.AllocateRenderGraphNodeParameters<RenderGraphParameters>();
    auto* nodeBParams = builder.AllocateRenderGraphNodeParameters<RenderGraphParameters>();

    // Add nodes
    builder.AddNode("NodeA", nodeAParams);
    builder.AddNode("NodeB", nodeBParams);

    // Add cyclic dependency
    builder.AddDependency("NodeA", "NodeB");
    REQUIRE_THROWS_AS( builder.AddDependency("NodeB", "NodeA"), std::runtime_error);

}

TEST_CASE("RenderGraphBuilder: Duplicate Node", "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder;

    // Allocate parameters for a node
    auto* nodeAParams = builder.AllocateRenderGraphNodeParameters<RenderGraphParameters>();

    // Add a node
    builder.AddNode("NodeA", nodeAParams);

    // Attempt to add a duplicate node and expect an exception
    REQUIRE_THROWS_AS(builder.AddNode("NodeA", nodeAParams), std::runtime_error);
}

TEST_CASE("RenderGraphBuilder: Missing Node Dependency", "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder;

    // Allocate parameters for a node
    auto* nodeAParams = builder.AllocateRenderGraphNodeParameters<RenderGraphParameters>();

    // Add a node
    builder.AddNode("NodeA", nodeAParams);

    // Attempt to add a dependency to a non-existent node and expect an exception
    REQUIRE_THROWS_AS(builder.AddDependency("NodeA", "NodeB"), std::runtime_error);
}

}  // namespace Muyo
