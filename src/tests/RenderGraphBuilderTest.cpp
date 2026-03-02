#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "Debug/RenderDoc.h"
#include "GraphicsTestEnv.h"
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderGraph/RenderGraphResourceDesc.h"
#include "RenderPassParameters.h"
#include "ShaderReflectionFetcher.h"
#include "catch2/catch_message.hpp"
#include "vulkan/vulkan_core.h"

static constexpr int WIDTH = 800;
static constexpr int HEIGHT = 600;

namespace Muyo::RenderGraph
{

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: Single quad node no descriptor sets", "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder(GetRenderDevice());
    // Render graph level registry
    builder.AddRegistry(
                ResourceHandle("MeshVertexBuffer"),
                BufferDesc<Vertex> {
                .count = 1,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT ,
                .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU
                }
            );

    // Render pass level details
    RenderGraphNodeCreateInfo quadPassCreateInfo = {
        .nodeName = "QuadNode",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("MeshVertexBuffer"),
                            .io = Muyo::RenderGraph::ResourceIOType::READ,
                            .usage = Muyo::RenderGraph::ResourceUsage::INDEX_BUFFER,
                            .kind = Muyo::RenderGraph::ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("MeshIndexBuffer"),
                            .io = Muyo::RenderGraph::ResourceIOType::READ,
                            .usage = Muyo::RenderGraph::ResourceUsage::INDEX_BUFFER,
                            .kind = Muyo::RenderGraph::ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("TriangleOutput"),
                            .io = Muyo::RenderGraph::ResourceIOType::WRITE,
                            .usage = Muyo::RenderGraph::ResourceUsage::COLOR_ATTACHMENT,
                            .kind = Muyo::RenderGraph::ResourceKind::IMAGE},
            },
        .shaderNames = {"triangle.vert", "triangle_no_tex.frag.slang"},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1,
                                   .attachments = {{{
                                       .blendEnable = true,
                                   }}}}},
        .cpuCallback =
            [](auto &ctx)
        {
            RenderTarget *pTarget = ctx.resourceManager.GetRenderTarget("TriangleOutput", VkExtent2D(WIDTH, HEIGHT),
                                                                        VK_FORMAT_R16G16B16A16_SFLOAT);
            REQUIRE(pTarget != nullptr);
        },
        .gpuCallback =
            [](auto &ctx)
        {
            VkCommandBuffer cmdBuf = ctx.commandBuffer;

            auto *pTarget = ctx.resourceManager.template GetResource<RenderTarget>("TriangleOutput");
            REQUIRE(pTarget != nullptr);
            VkRenderingAttachmentInfo colorAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView = pTarget->getView();
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            colorAttachment.clearValue.color.float32[0] = 0.0F;
            colorAttachment.clearValue.color.float32[1] = 0.0F;
            colorAttachment.clearValue.color.float32[2] = 0.0F;
            colorAttachment.clearValue.color.float32[3] = 1.0F;

            VkRenderingInfo renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO};
            renderingInfo.renderArea = {.offset = {0, 0}, .extent = {WIDTH, HEIGHT}};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;

            ////const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();
            // const MeshVertexResources& meshManager =
            // GetMeshResourceManager()->GetMeshVertexResources(); VkDeviceSize
            // offset = 0; VkBuffer vertexBuffer =
            // meshManager.m_pVertexBuffer->buffer(); VkBuffer indexBuffer =
            // meshManager.m_pIndexBuffer->buffer(); uint32_t nIndexCount =
            // quadMesh.m_nIndexCount; uint32_t nIndexOffset =
            // quadMesh.m_nIndexOffset;

            // vkCmdBeginRendering(cmdBuf, &renderingInfo);
            // vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
            // ctx.pipeline); vkCmdBindVertexBuffers(cmdBuf, 0, 1,
            // &vertexBuffer, &offset); vkCmdBindIndexBuffer(cmdBuf,
            // indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // ViewportBuilder vpBuilder;
            // VkViewport viewport = vpBuilder.setWH({WIDTH, HEIGHT}).Build();
            // VkRect2D scissorRect;
            // scissorRect.offset = {.x = 0, .y = 0};
            // scissorRect.extent = {.width = WIDTH, .height = HEIGHT};
            // vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
            // vkCmdSetScissor(cmdBuf, 0, 1, &scissorRect);
            // vkCmdDrawIndexed(cmdBuf, nIndexCount, 1, nIndexOffset, 0, 0);
            // vkCmdEndRendering(cmdBuf);
        }};

    builder.AddNode(quadPassCreateInfo);
    builder.Build();
    {
        RenderDocScopedCapture capture("test_quad");
        builder.Execute();
    }
}

TEST_CASE_METHOD(GraphicsTestEnvMazdaScene, "RenderGraphBuilder: A cube with descriptor sets", "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder(GetRenderDevice());
    RenderGraphNodeCreateInfo drawCommandPrepPass = {
      .nodeName = "DrawCmdPrep",
      .queueType = QueueType::CPU,
      .resourceUses = {ResourceUse{
          .handle = ResourceHandle("OpaqueDrawCmdBuf"),
          .io = Muyo::RenderGraph::ResourceIOType::WRITE,
          .usage = Muyo::RenderGraph::ResourceUsage::DRAW_COMMAND_BUFFER,
          .kind = Muyo::RenderGraph::ResourceKind::BUFFER}},
      .cpuCallback =
          [](auto &ctx) {

          },
      .gpuCallback = [](auto &ctx) {}};

    RenderGraphNodeCreateInfo cubePassCreateInfo = {
        .nodeName = "CubeNode",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("MeshVertexBuffer"),
                            .io = Muyo::RenderGraph::ResourceIOType::READ,
                            .usage = Muyo::RenderGraph::ResourceUsage::VERTEX_BUFFER,
                            .kind = Muyo::RenderGraph::ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("MeshIndexBuffer"),
                            .io = Muyo::RenderGraph::ResourceIOType::READ,
                            .usage = Muyo::RenderGraph::ResourceUsage::INDEX_BUFFER,
                            .kind = Muyo::RenderGraph::ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("PreViewData"),
                            .io = Muyo::RenderGraph::ResourceIOType::READ,
                            .usage = Muyo::RenderGraph::ResourceUsage::UNIFORM_BUFFER,
                            .kind = Muyo::RenderGraph::ResourceKind::BUFFER,
                            .bindingSemantic = Muyo::RenderGraph::ResourceBindingSemantic::PER_VIEW},
                ResourceUse{.handle = ResourceHandle("TriangleOutput"),
                            .io = Muyo::RenderGraph::ResourceIOType::WRITE,
                            .usage = Muyo::RenderGraph::ResourceUsage::COLOR_ATTACHMENT,
                            .kind = Muyo::RenderGraph::ResourceKind::IMAGE},
            },
        .shaderNames = {"forward.vert.slang", "forward.frag.slang"},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1,
                                   .attachments = {{{
                                       .blendEnable = true,
                                   }}}}},
        .cpuCallback =
            [](auto &ctx)
        {
            RenderTarget *pTarget = ctx.resourceManager.GetRenderTarget("TriangleOutput", VkExtent2D(WIDTH, HEIGHT),
                                                                        VK_FORMAT_R16G16B16A16_SFLOAT);

            REQUIRE(pTarget != nullptr);

            UniformBuffer<PerViewData> *pUniformBuffer =
                GetRenderResourceManager()->GetUniformBuffer<PerViewData>("PerViewData");

            // Update camera uniform buffer
            Arcball camera(glm::perspective(glm::radians(80.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT),
                                            0.1F, 100.0F),
                           glm::lookAt(glm::vec3(0.0F, 0.0F, -2.0F),  // Eye
                                       glm::vec3(0.0F, 0.0F, 0.0F),   // Center
                                       glm::vec3(0.0F, 1.0F, 0.0F)),  // Up
                           0.1F,                                      // near
                           100.0F,                                    // far
                           static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
            camera.UpdatePerViewDataUBO(pUniformBuffer);
        },
        .gpuCallback =
            [this](RenderGraphNodeGpuContext &ctx)
        {
            // construct draw commands
            std::vector<VkDrawIndexedIndirectCommand> drawCommands;
            std::vector<const SceneNode *> &vpGeometryNodes = m_mDrawList.m_aDrawLists[DrawLists::DL_OPAQUE];

            for (const SceneNode *pGeometryNode : vpGeometryNodes)
            {
                const Geometry *pGeometry = static_cast<const GeometrySceneNode *>(pGeometryNode)->GetGeometry();
                uint32_t nSubmeshIndex = 0;
                for (const auto &pSubmesh : pGeometry->getSubmeshes())
                {
                    VkDrawIndexedIndirectCommand drawCommand;
                    const Mesh &mesh = GetMeshResourceManager()->GetMesh(pSubmesh->GetMeshIndex());

                    drawCommand.indexCount = mesh.m_nIndexCount;
                    drawCommand.instanceCount = 1;
                    drawCommand.firstIndex = mesh.m_nIndexOffset;
                    drawCommand.vertexOffset = 0;
                    drawCommand.firstInstance = PackSubmeshObjectIndex(pGeometryNode->GetPerObjId(), nSubmeshIndex++);

                    drawCommands.push_back(drawCommand);
                }
            }

            const DrawCommandBuffer<VkDrawIndexedIndirectCommand> *pDrawCommandBuffer =
                ctx.resourceManager.GetDrawCommandBuffer("GBuffer draw commands", drawCommands);

            VkCommandBuffer cmdBuf = ctx.commandBuffer;

            auto *pTarget = ctx.resourceManager.template GetResource<RenderTarget>("TriangleOutput");
            REQUIRE(pTarget != nullptr);
            VkRenderingAttachmentInfo colorAttachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView = pTarget->getView();
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            colorAttachment.clearValue.color.float32[0] = 0.0F;
            colorAttachment.clearValue.color.float32[1] = 0.0F;
            colorAttachment.clearValue.color.float32[2] = 0.0F;
            colorAttachment.clearValue.color.float32[3] = 1.0F;

            VkRenderingInfo renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO};
            renderingInfo.renderArea = {.offset = {0, 0}, .extent = {WIDTH, HEIGHT}};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;

            const MeshVertexResources &meshManager = GetMeshResourceManager()->GetMeshVertexResources();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
            VkBuffer indexBuffer = meshManager.m_pIndexBuffer->buffer();

            vkCmdBeginRendering(cmdBuf, &renderingInfo);
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline);

            ctx.descriptorSetManager.BindResourceToDescriptorSet(
                ctx.resourceManager.template GetResource<UniformBuffer<PerViewData>>("PerViewData"),
                ResourceBindingSemantic::PER_VIEW, 0);
            ctx.descriptorSetManager.BindResourceToDescriptorSet(ctx.perObjResourceManager.GetPerObjResource(),
                                                                 ResourceBindingSemantic::PER_OBJ, 0);

            std::vector<VkDescriptorSet> descSets = {
                ctx.descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::PER_VIEW),
                ctx.descriptorSetManager.GetDescriptorSet(ResourceBindingSemantic::PER_OBJ)};
            vkCmdBindDescriptorSets(cmdBuf, ctx.bindingPoint, ctx.pipelineLayout, 0,
                                    static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);
            vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            ViewportBuilder vpBuilder;
            VkViewport viewport = vpBuilder.setWH({WIDTH, HEIGHT}).Build();
            VkRect2D scissorRect;
            scissorRect.offset = {.x = 0, .y = 0};
            scissorRect.extent = {.width = WIDTH, .height = HEIGHT};
            vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
            vkCmdSetScissor(cmdBuf, 0, 1, &scissorRect);
            vkCmdDrawIndexedIndirect(cmdBuf, pDrawCommandBuffer->buffer(), 0, pDrawCommandBuffer->GetDrawCommandCount(),
                                     pDrawCommandBuffer->GetStride());
            // vkCmdDrawIndexed(cmdBuf, nIndexCount, 1, nIndexOffset, 0, 0);
            vkCmdEndRendering(cmdBuf);
        }};

    builder.AddNode(drawCommandPrepPass);
    builder.AddNode(cubePassCreateInfo);
    builder.AddDependency(cubePassCreateInfo.nodeName, drawCommandPrepPass.nodeName);
    builder.Build();

    {
        RenderDocScopedCapture capture("test_quad");
        builder.Execute();
    }
}
}  // namespace Muyo::RenderGraph
