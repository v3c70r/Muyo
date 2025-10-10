#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <utility>
#include <variant>

#include "DescriptorManager.h"
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphResourceDesc.h"
#include "RenderPassParameters.h"
#include "catch2/catch_message.hpp"
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
    REQUIRE_THROWS_AS(builder.AddDependency("NodeB", "NodeA"), std::runtime_error);
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

// TEST_CASE("RenderGraphBuilder: Custom Render Graph Parameter", "[RenderGraphBuilder]")
//{
//     class CustomRGParam : public RenderGraphParameters
//     {
//         const int m_customData = 42;
//         bool m_functionCalled = false;
//
//     public:
//         int GetCustomData() const { return m_customData; }
//         void OnGraphBuild() override
//         {
//             // Custom build logic
//             m_functionCalled = true;
//         }
//         bool WasFunctionCalled() const { return m_functionCalled; }
//     };
//
//     RenderGraphBuilder builder;
//     // Allocate parameters for a node
//     auto* nodeAParams = builder.AllocateRenderGraphNodeParameters<CustomRGParam>();
//     nodeAParams->m_vInputResources.emplace_back("InputResource");
//     nodeAParams->m_vOutputResources.emplace_back("OutputResources");
//     builder.AddNode("NodeA", nodeAParams);
//
//     auto* nodeBParams = builder.AllocateRenderGraphNodeParameters<CustomRGParam>();
//     nodeBParams->m_vInputResources.emplace_back("InputResource");
//     nodeBParams->m_vOutputResources.emplace_back("OutputResources");
//     builder.AddNode("NodeB", nodeBParams);
//
//     builder.AddDependency("NodeB", "NodeA");
//
//     builder.Build();
//
//     assert(nodeAParams->WasFunctionCalled());
//     assert(nodeBParams->WasFunctionCalled());
// }
TEST_CASE("Resource Desc", "[RenderGraphBuilderResourceDesc type]")
{
    std::vector<RenderGraph::ResourceDesc> resources{
        RenderGraph::IndexBufferDesc<uint8_t>{.name = "MyIndexBufferDesc", .count = 3000},
        RenderGraph::VertexBufferDesc<Vertex>{.name = "MyVertexBuffer", .count = 1000}};

    auto* pIdxBuffer = std::get_if<RenderGraph::IndexBufferDesc<uint8_t>>(&resources[0]);
    REQUIRE(pIdxBuffer != nullptr);

    auto* pVertBuffer = std::get_if<RenderGraph::VertexBufferDesc<Vertex>>(&resources[1]);
    REQUIRE(pVertBuffer != nullptr);
}

// RAII graphics environment
class GraphicsEnv
{
public:
    GraphicsEnv()
    {
        GetRenderDevice()->Initialize({}, {});
        GetRenderDevice()->CreateDevice({}, std::vector<const char*>(), nullptr, {});
        GetRenderDevice()->CreateCommandPools();
        GetMemoryAllocator()->Initalize(GetRenderDevice());
        GetRenderResourceManager()->Initialize();
        GetDescriptorManager()->createDescriptorPool();
        GetDescriptorManager()->createDescriptorSetLayouts();
    }
    ~GraphicsEnv()
    {
        GetDescriptorManager()->destroyDescriptorSetLayouts();
        GetDescriptorManager()->destroyDescriptorPool();
        GetRenderResourceManager()->Unintialize();
        GetRenderDevice()->DestroyCommandPools();
        GetMemoryAllocator()->Unintialize();
        GetRenderDevice()->DestroyDevice();
        GetRenderDevice()->Unintialize();
    }
};

TEST_CASE("Graphics Env", "[RenderGraphBuilderResourceDesc type]")
{
    REQUIRE_NOTHROW(GraphicsEnv());
}

TEST_CASE("Resource Allocatoin", "[RenderGraphBuilderResourceDesc type]")
{
    GraphicsEnv graphicsEnv;
    RenderGraph::IndexBufferDesc<uint16_t> indexBufferDesc{.name = "IndexBufferDesc", .count = 100};
    auto* pResource = Allocate(indexBufferDesc, GetRenderResourceManager());
    REQUIRE(pResource != nullptr);
}

TEST_CASE("Render Graph with Resource Allocation", "[RenderGraph]")
{
    GraphicsEnv graphicsEnv;
    class MyParam : public RenderGraphParameters
    {
    public:
        void OnGraphBuild() override
        {
            for (auto& inputResource : vInputResources)
            {
                std::visit(
                    [&](auto&& arg)
                    {
                        auto* pResource = Allocate(arg, GetRenderResourceManager());
                        WARN("Resource Type: " << typeid(pResource).name());
                        REQUIRE(pResource != nullptr);
                    },
                    inputResource.GetResourceDesc());
            }
        }
    };

    RenderGraphBuilder builder;
    auto* myParam = builder.AllocateRenderGraphNodeParameters<MyParam>();
    myParam->vInputResources.emplace_back("ParamIndexBuffer0",
                                          RenderGraph::IndexBufferDesc<uint16_t>{.name = "IndexBuffer", .count = 100});
    myParam->vInputResources.emplace_back("ParamIndexBuffer1",
                                          RenderGraph::IndexBufferDesc<uint16_t>{.name = "IndexBuffer", .count = 100});
    myParam->vOutputResources.emplace_back(
        "ParamVertexBuffer0", RenderGraph::VertexBufferDesc<Muyo::Vertex>{.name = "VertexBufferDesc", .count = 100});

    builder.AddNode("Pass0", myParam);
    builder.Build();
}

// Lets build a simple headless compute graph
TEST_CASE("Simple Headless Compute Graph", "[RenderGraph]")
{
    class ComputeParam : public RenderGraphParameters
    {
        Muyo::RenderPassParameters m_renderPassParameters;
        VkPipeline m_pipeline = VK_NULL_HANDLE;
        std::vector<uint8_t> m_inputData;
        std::vector<uint8_t> m_outputData;

    public:
        ComputeParam()
        {
            // Fill input data with random values
            m_inputData.resize(1024);
            for (auto& byte : m_inputData)
            {
                byte = static_cast<uint8_t>(rand() % 256);
            }

            m_outputData.resize(1024, 0);
        }
        void OnGraphBuild() override
        {
            // Allocate resources
            for (auto& inputResource : vInputResources)
            {
                std::visit(
                    [&](auto&& arg)
                    {
                        auto* pResource = Allocate(arg, GetRenderResourceManager());
                        pResource->SetDebugName(std::string(inputResource.GetName()));
                        pResource->SetData(m_inputData.data(), m_inputData.size());
                        m_renderPassParameters.AddParameter(pResource, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                            VK_SHADER_STAGE_COMPUTE_BIT);
                    },
                    inputResource.GetResourceDesc());
            }
            for (auto& outputResource : vOutputResources)
            {
                std::visit(
                    [&](auto&& arg)
                    {
                        auto* pResource = Allocate(arg, GetRenderResourceManager());
                        pResource->SetDebugName(std::string(outputResource.GetName()));
                        m_renderPassParameters.AddParameter(pResource, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                            VK_SHADER_STAGE_COMPUTE_BIT, 1);
                    },
                    outputResource.GetResourceDesc());
            }
            m_renderPassParameters.AddPushConstantParameter<uint32_t>(VK_SHADER_STAGE_COMPUTE_BIT);
            m_renderPassParameters.Finalize("Compute Pass test");

            {
                VkShaderModule shader = CreateShaderModule(ReadSpv("shaders/copyBuffer.comp.slang.spv"));
                ComputePipelineBuilder builder;

                VkComputePipelineCreateInfo createInfo =
                    builder.AddShaderModule(shader)
                        .SetPipelineLayout(m_renderPassParameters.GetPipelineLayout())
                        .Build();

                VK_ASSERT(vkCreateComputePipelines(GetRenderDevice()->GetDevice(), VK_NULL_HANDLE, 1, &createInfo,
                                                   nullptr, &m_pipeline));
                vkDestroyShaderModule(GetRenderDevice()->GetDevice(), shader, nullptr);
            }
        };

        // Execute one time command buffer
        void OnGraphExecute() override
        {
            GetRenderDevice()->ExecuteImmediateCommand(
                [&](VkCommandBuffer commandBuffer)
                {
                    std::array<VkDescriptorSet, 2> vDescSets = {
                        m_renderPassParameters.AllocateDescriptorSet("", 0),
                        m_renderPassParameters.AllocateDescriptorSet("", 1),
                    };
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                            m_renderPassParameters.GetPipelineLayout(), 0,
                                            static_cast<uint32_t>(vDescSets.size()), vDescSets.data(), 0, nullptr);
                    uint32_t pushConstData = 1024;
                    vkCmdPushConstants(commandBuffer, m_renderPassParameters.GetPipelineLayout(),
                                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &pushConstData);
                    vkCmdDispatch(commandBuffer, 1024, 1, 1);
                });
            auto* pStorageBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<uint8_t>>("OutputBuffer");
            void* pGpuSource = pStorageBuffer->Map();
            memcpy(m_outputData.data(), pGpuSource, m_outputData.size());
            pStorageBuffer->Unmap();

            // Validate output data
            //CHECK(std::equal(m_inputData.begin(), m_inputData.end(), m_outputData.begin()));
        };
        void DestroyResources() { vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr); }
    };
    GraphicsEnv graphicsEnv;

    RenderGraphBuilder builder;

    auto* computeParam = builder.AllocateRenderGraphNodeParameters<ComputeParam>();
    computeParam->vInputResources.emplace_back(
        "InputBuffer", RenderGraph::StorageBufferDesc<uint8_t>{.name = "InputBuffer", .count = 1024});
    computeParam->vOutputResources.emplace_back(
        "OutputBuffer", RenderGraph::StorageBufferDesc<uint8_t>{.name = "OutputBuffer", .count = 1024});

    builder.AddNode("ComputePass", computeParam);
    builder.Build();
    builder.Execute();

    computeParam->DestroyResources();
}
}// namespace Muyo
