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

namespace Muyo::RenderGraph
{
RenderGraphNodeCreateInfo quadPassCreateInfo = {
    .nodeName = "NodeA",
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
        [](auto& ctx)
    {
        RenderTarget* pTarget =
            ctx.resourceManager.GetRenderTarget("TriangleOutput", VkExtent2D(800, 600), VK_FORMAT_R16G16B16A16_SFLOAT);
        REQUIRE(pTarget != nullptr);
    },
    .gpuCallback =
        [](auto& ctx)
    {
        VkCommandBuffer cmdBuf = ctx.commandBuffer;

        auto* pTarget = ctx.resourceManager.template GetResource<RenderTarget>("TriangleOutput");
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
        renderingInfo.renderArea = {.offset = {0, 0}, .extent = {800, 600}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();
        const MeshVertexResources& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
        VkDeviceSize offset = 0;
        VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
        VkBuffer indexBuffer = meshManager.m_pIndexBuffer->buffer();
        uint32_t nIndexCount = quadMesh.m_nIndexCount;
        uint32_t nIndexOffset = quadMesh.m_nIndexOffset;

        vkCmdBeginRendering(cmdBuf, &renderingInfo);
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline);
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        ViewportBuilder vpBuilder;
        VkViewport viewport = vpBuilder.setWH({800, 600}).Build();
        VkRect2D scissorRect;
        scissorRect.offset = {.x = 0, .y = 0};
        scissorRect.extent = {.width = 800, .height = 600};
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissorRect);
        vkCmdDrawIndexed(cmdBuf, nIndexCount, 1, nIndexOffset, 0, 0);
        vkCmdEndRendering(cmdBuf);
    }};
TEST_CASE("RenderGraphBuilder: Single quad node", "[RenderGraphBuilder]")
{
    GraphicsTestEnv testEnv;
    RenderGraphBuilder builder(GetRenderDevice());

    builder.AddNode(quadPassCreateInfo);
    builder.Build();
    {
        RenderDocScopedCapture capture("test_quad");
        builder.Execute();
    }
}
}  // namespace Muyo::RenderGraph
