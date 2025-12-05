#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphResourceDesc.h"
#include "RenderPassParameters.h"
#include "GraphicsTestEnv.h"
#include "catch2/catch_message.hpp"
namespace Muyo::RenderGraph
{
//TEST_CASE("RenderGraphBuilder: AddNode and GetExecutionOrder", "[RenderGraphBuilder]")
//{
    //RenderGraphBuilder builder;

    //// Add nodes
    //builder.AddNode("NodeA", nodeAParams);
    //builder.AddNode("NodeB", nodeBParams);
    //builder.AddNode("NodeC", nodeCParams);

    //// Add dependencies
    //builder.AddDependency("NodeA", "NodeB");
    //builder.AddDependency("NodeB", "NodeC");

    //// Build the graph
    //REQUIRE_NOTHROW(builder.Build());

    //// Get execution order
    //auto executionOrder = builder.GetExecutionOrder();

    //// Verify execution order
    //REQUIRE(executionOrder.size() == 3);
    //REQUIRE(executionOrder[0] == "NodeA");
    //REQUIRE(executionOrder[1] == "NodeB");
    //REQUIRE(executionOrder[2] == "NodeC");
//}

TEST_CASE("RenderGraphBuilder: AddNode and GetExecutionOrder", "[RenderGraphBuilder]")
{
    GraphicsTestEnv testEnv;
    RenderGraphBuilder builder;
    // Allocate parameters for nodes
    auto* nodeAParams = builder.AllocateRenderGraphNodeParameters<RenderGraphNodeParameters>(
        {.vDescriptorSets = {{.bindings = {StorageBufferDesc<uint8_t>{.name = "NodeA_InputBuffer", .count = 256}}}},
         .pushConstant = std::nullopt},  // Input pipeline layout desc
        {.vDescriptorSets = {{.bindings = {StorageBufferDesc<uint8_t>{.name = "NodeA_OutputBuffer", .count = 256}}}},
         .pushConstant = std::nullopt}  // Output pipeline layout desc
    );

    builder.AddNode("NodeA", nodeAParams);

}



/*
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



TEST_CASE("Graphics Env", "[RenderGraphBuilderResourceDesc type]")
{
    REQUIRE_NOTHROW(GraphicsTestEnv());
}

TEST_CASE("Resource Allocatoin", "[RenderGraphBuilderResourceDesc type]")
{
    GraphicsTestEnv graphicsEnv;
    RenderGraph::IndexBufferDesc<uint16_t> indexBufferDesc{.name = "IndexBufferDesc", .count = 100};
    auto* pResource = Allocate(indexBufferDesc, GetRenderResourceManager());
    REQUIRE(pResource != nullptr);
}

TEST_CASE("Render Graph with Resource Allocation", "[RenderGraph]")
{
    GraphicsTestEnv graphicsEnv;
    class MyParam : public RenderGraphParameters
    {
    public:
        void OnGraphBuild() override
        {
            for (auto& inputResource : inputResources)
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
    myParam->inputResources.emplace_back("ParamIndexBuffer0",
                                          RenderGraph::IndexBufferDesc<uint16_t>{.name = "IndexBuffer", .count = 100});
    myParam->inputResources.emplace_back("ParamIndexBuffer1",
                                          RenderGraph::IndexBufferDesc<uint16_t>{.name = "IndexBuffer", .count = 100});
    myParam->vOutputResources.emplace_back(
        "ParamVertexBuffer0", RenderGraph::VertexBufferDesc<Muyo::Vertex>{.name = "VertexBufferDesc", .count = 100});

    builder.AddNode("Pass0", myParam);
    builder.Build();
}


// Lets build a simple headless compute graph
TEST_CASE("Simple Headless Compute Graph", "[RenderGraph]")
{
    class CopyBufferParam : public RenderGraphParameters
    {
        Muyo::RenderPassParameters m_renderPassParameters;
        VkPipeline m_pipeline = VK_NULL_HANDLE;
        std::vector<uint8_t> m_inputData;
        std::vector<uint8_t> m_outputData;

    public:
        CopyBufferParam()
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
            for (auto& inputResource : inputResources)
            {
                std::visit(
                    [&](auto&& arg)
                    {
                        auto* pResource = Allocate(arg, GetRenderResourceManager());
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
                    // resource barriers to prepare input and output buffers
                    VkBuffer inputBuffer =
                        GetRenderResourceManager()->GetResource<Muyo::StorageBuffer<uint8_t>>("InputBuffer")->buffer();
                    VkBuffer outputBuffer =
                        GetRenderResourceManager()->GetResource<Muyo::StorageBuffer<uint8_t>>("OutputBuffer")->buffer();
                    std::array<VkBufferMemoryBarrier, 2> bufferBarriers = {};
                    bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarriers[0].srcAccessMask = 0;
                    bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[0].buffer = inputBuffer;
                    bufferBarriers[0].offset = 0;
                    bufferBarriers[0].size = VK_WHOLE_SIZE;
                    bufferBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarriers[1].srcAccessMask = 0;
                    bufferBarriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    bufferBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[1].buffer = outputBuffer;
                    bufferBarriers[1].offset = 0;
                    bufferBarriers[1].size = VK_WHOLE_SIZE;
                    vkCmdPipelineBarrier(commandBuffer,
                                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,     // srcStageMask
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                                         0,                                     // dependencyFlags
                                         0, nullptr,                            // memoryBarriers
                                         static_cast<uint32_t>(bufferBarriers.size()),
                                         bufferBarriers.data(),  // bufferMemoryBarriers
                                         0, nullptr              // imageMemoryBarriers
                    );

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

                    // Insert a barrier to ensure compute shader writes are finished before readback
                    VkBufferMemoryBarrier bufferBarrier = {};
                    bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    bufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarrier.buffer = outputBuffer;
                    bufferBarrier.offset = 0;
                    bufferBarrier.size = VK_WHOLE_SIZE;

                    vkCmdPipelineBarrier(commandBuffer,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                                         VK_PIPELINE_STAGE_HOST_BIT,            // dstStageMask
                                         0,                                     // dependencyFlags
                                         0, nullptr,                            // memoryBarriers
                                         1, &bufferBarrier,                     // bufferMemoryBarriers
                                         0, nullptr                             // imageMemoryBarriers
                    );
                });

            auto* pStorageBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<uint8_t>>("OutputBuffer");
            void* pGpuSource = pStorageBuffer->Map();
            memcpy(m_outputData.data(), pGpuSource, m_outputData.size());
            pStorageBuffer->Unmap();

            CHECK(std::equal(m_inputData.begin(), m_inputData.end(), m_outputData.begin()));
        };
        void DestroyResources() { vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr); }
    };
    GraphicsTestEnv graphicsEnv;

    RenderGraphBuilder builder;

    auto* computeParam = builder.AllocateRenderGraphNodeParameters<CopyBufferParam>();
    computeParam->inputResources.emplace_back(
        "InputBuffer", RenderGraph::StorageBufferDesc<uint8_t>{.name = "InputBuffer", .count = 1024, .allowReadback=false});
    computeParam->vOutputResources.emplace_back(
        "OutputBuffer", RenderGraph::StorageBufferDesc<uint8_t>{.name = "OutputBuffer", .count = 1024, .allowReadback=true});

    builder.AddNode("ComputePass", computeParam);
    builder.Build();
    builder.Execute();

    computeParam->DestroyResources();
}

TEST_CASE("Simple Headless Compute Graph with multiple passes", "[RenderGraph]")
{
    class UploadAndCopy : public RenderGraphParameters
    {
        Muyo::RenderPassParameters m_renderPassParameters;
        VkPipeline m_pipeline = VK_NULL_HANDLE;
        std::vector<uint8_t> m_inputData;
        std::vector<uint8_t> m_outputData;

    public:
        UploadAndCopy()
        {
            // Fill input data with random values
            m_inputData.resize(1024);
            for (auto& byte : m_inputData)
            {
                byte = static_cast<uint8_t>(rand() % 256);
            }

            m_outputData.resize(1024, 0);
        }
        const std::vector<uint8_t>& GetOutputData() const { return m_outputData; }
        void OnGraphBuild() override
        {
            // Allocate resources
            for (auto& inputResource : inputResources)
            {
                std::visit(
                    [&](auto&& arg)
                    {
                        auto* pResource = Allocate(arg, GetRenderResourceManager());
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
                    // resource barriers to prepare input and output buffers
                    VkBuffer inputBuffer =
                        GetRenderResourceManager()->GetResource<Muyo::StorageBuffer<uint8_t>>("InputBuffer")->buffer();
                    VkBuffer outputBuffer = GetRenderResourceManager()
                                                ->GetResource<Muyo::StorageBuffer<uint8_t>>("IntermediateBuffer")
                                                ->buffer();
                    std::array<VkBufferMemoryBarrier, 2> bufferBarriers = {};
                    bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarriers[0].srcAccessMask = 0;
                    bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[0].buffer = inputBuffer;
                    bufferBarriers[0].offset = 0;
                    bufferBarriers[0].size = VK_WHOLE_SIZE;
                    bufferBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarriers[1].srcAccessMask = 0;
                    bufferBarriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    bufferBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[1].buffer = outputBuffer;
                    bufferBarriers[1].offset = 0;
                    bufferBarriers[1].size = VK_WHOLE_SIZE;
                    vkCmdPipelineBarrier(commandBuffer,
                                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,     // srcStageMask
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                                         0,                                     // dependencyFlags
                                         0, nullptr,                            // memoryBarriers
                                         static_cast<uint32_t>(bufferBarriers.size()),
                                         bufferBarriers.data(),  // bufferMemoryBarriers
                                         0, nullptr              // imageMemoryBarriers
                    );

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
        };
        void DestroyResources() { vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr); }
    };

    class CopyAndDownload : public RenderGraphParameters
    {
        Muyo::RenderPassParameters m_renderPassParameters;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

    public:
        void OnGraphBuild() override
        {
            // Allocate resources
            for (auto& inputResource : inputResources)
            {
                std::visit(
                    [&](auto&& arg)
                    {
                        auto* pResource = Allocate(arg, GetRenderResourceManager());
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
                    // resource barriers to prepare input and output buffers
                    VkBuffer inputBuffer = GetRenderResourceManager()
                                               ->GetResource<Muyo::StorageBuffer<uint8_t>>("IntermediateBuffer")
                                               ->buffer();
                    VkBuffer outputBuffer =
                        GetRenderResourceManager()->GetResource<Muyo::StorageBuffer<uint8_t>>("OutputBuffer")->buffer();
                    std::array<VkBufferMemoryBarrier, 2> bufferBarriers = {};
                    bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarriers[0].srcAccessMask = 0;
                    bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[0].buffer = inputBuffer;
                    bufferBarriers[0].offset = 0;
                    bufferBarriers[0].size = VK_WHOLE_SIZE;
                    bufferBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarriers[1].srcAccessMask = 0;
                    bufferBarriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    bufferBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarriers[1].buffer = outputBuffer;
                    bufferBarriers[1].offset = 0;
                    bufferBarriers[1].size = VK_WHOLE_SIZE;
                    vkCmdPipelineBarrier(commandBuffer,
                                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,     // srcStageMask
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // dstStageMask
                                         0,                                     // dependencyFlags
                                         0, nullptr,                            // memoryBarriers
                                         static_cast<uint32_t>(bufferBarriers.size()),
                                         bufferBarriers.data(),  // bufferMemoryBarriers
                                         0, nullptr              // imageMemoryBarriers
                    );

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

                    // Insert a barrier to ensure compute shader writes are finished before readback
                    VkBufferMemoryBarrier bufferBarrier = {};
                    bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    bufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufferBarrier.buffer = outputBuffer;
                    bufferBarrier.offset = 0;
                    bufferBarrier.size = VK_WHOLE_SIZE;

                    vkCmdPipelineBarrier(commandBuffer,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  // srcStageMask
                                         VK_PIPELINE_STAGE_HOST_BIT,            // dstStageMask
                                         0,                                     // dependencyFlags
                                         0, nullptr,                            // memoryBarriers
                                         1, &bufferBarrier,                     // bufferMemoryBarriers
                                         0, nullptr                             // imageMemoryBarriers
                    );
                });
        };
        void DestroyResources() { vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr); }
    };

    GraphicsTestEnv graphicsEnv;

    RenderGraphBuilder builder;

    auto* computeParam0 = builder.AllocateRenderGraphNodeParameters<UploadAndCopy>();
    computeParam0->inputResources.emplace_back(
        "InputBuffer",
        RenderGraph::StorageBufferDesc<uint8_t>{.name = "InputBuffer", .count = 1024, .allowReadback = false});
    computeParam0->vOutputResources.emplace_back(
        "IntermediateBuffer",
        RenderGraph::StorageBufferDesc<uint8_t>{.name = "IntermediateBuffer", .count = 1024, .allowReadback = false});

    auto* computeParam1 = builder.AllocateRenderGraphNodeParameters<CopyAndDownload>();
    computeParam1->inputResources.emplace_back(
        "IntermediateBuffer",
        RenderGraph::StorageBufferDesc<uint8_t>{.name = "IntermediateBuffer", .count = 1024, .allowReadback = false});
    computeParam1->vOutputResources.emplace_back(
        "OutputBuffer",
        RenderGraph::StorageBufferDesc<uint8_t>{.name = "OutputBuffer", .count = 1024, .allowReadback = true});

    builder.AddNode("ComputePass0", computeParam0);
    builder.AddNode("ComputePass1", computeParam1);
    builder.AddDependency("ComputePass1", "ComputePass0");
    builder.Build();
    builder.Execute();

    const auto& inputData = computeParam0->GetOutputData();
    std::vector<uint8_t> outputData(inputData.size(), 0);

    // Check input data and output data
    auto* pStorageBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<uint8_t>>("OutputBuffer");
    void* pGpuSource = pStorageBuffer->Map();
    memcpy(outputData.data(), pGpuSource, outputData.size());
    pStorageBuffer->Unmap();
    CHECK(std::equal(inputData.begin(), inputData.end(), outputData.begin()));

    computeParam0->DestroyResources();
    computeParam1->DestroyResources();
}
*/
}// namespace Muyo
