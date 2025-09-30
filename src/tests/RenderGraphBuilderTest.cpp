#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <utility>
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphResourceDesc.h"

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

TEST_CASE("RenderGraphBuilder: Custom Render Graph Parameter", "[RenderGraphBuilder]")
{
    class CustomRGParam : public RenderGraphParameters
    {
        const int m_customData = 42;
        bool m_functionCalled = false;

    public:
        int GetCustomData() const { return m_customData; }
        void OnGraphBuild() override
        {
            // Custom build logic
            m_functionCalled = true;
        }
        bool WasFunctionCalled() const { return m_functionCalled; }
    };

    RenderGraphBuilder builder;
    // Allocate parameters for a node
    auto* nodeAParams = builder.AllocateRenderGraphNodeParameters<CustomRGParam>();
    nodeAParams->m_vInputResources.emplace_back("InputResource");
    nodeAParams->m_vOutputResources.emplace_back("OutputResources");
    builder.AddNode("NodeA", nodeAParams);

    auto* nodeBParams = builder.AllocateRenderGraphNodeParameters<CustomRGParam>();
    nodeBParams->m_vInputResources.emplace_back("InputResource");
    nodeBParams->m_vOutputResources.emplace_back("OutputResources");
    builder.AddNode("NodeB", nodeBParams);

    builder.AddDependency("NodeB", "NodeA");

    builder.Build();

    assert(nodeAParams->WasFunctionCalled());
    assert(nodeBParams->WasFunctionCalled());
}
TEST_CASE("Resource Desc", "[RenderGraphBuilderResourceDesc type]")
{
    std::vector<RenderGraph::ResourceDesc> resources
    {
        RenderGraph::IndexBufferDesc<uint8_t>{.name="MyIndexBufferDesc", .count=3000},
        RenderGraph::VertexBuffer<Vertex>{.name="MyVertexBuffer", .count=1000}
    };

    auto* pIdxBuffer = std::get_if<RenderGraph::IndexBufferDesc<uint8_t>>(&resources[0]);
    REQUIRE(pIdxBuffer != nullptr);

    auto* pVertBuffer = std::get_if<RenderGraph::VertexBuffer<Vertex>>(&resources[1]);
    REQUIRE(pVertBuffer != nullptr);

}

TEST_CASE("Resource Allocatoin", "[RenderGraphBuilderResourceDesc type]")
{
    GetRenderDevice()->Initialize({}, {});
    GetRenderDevice()->CreateDevice({}, std::vector<const char*>(), nullptr, {});
    GetRenderDevice()->CreateCommandPools();
    GetMemoryAllocator()->Initalize(GetRenderDevice());
    GetRenderResourceManager()->Initialize();

    RenderGraph::IndexBufferDesc<uint16_t> indexBufferDesc{.name = "IndexBufferDesc", .count = 100};
    auto* pResource = Allocate(indexBufferDesc, GetRenderResourceManager());
    REQUIRE(pResource != nullptr);

    GetRenderResourceManager()->Unintialize();
    GetRenderDevice()->DestroyCommandPools();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
}
}  // namespace Muyo
