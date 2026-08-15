#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "Debug/RenderDoc.h"
#include "GraphicsTestEnv.h"
#include "MeshVertex.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderGraph/RenderGraphResourceDesc.h"
#include "Scene/Scene.h"
#include "RenderResources/Geometry.h"
#include "PerObjResourceManager.h"
#include "Camera.h"
#include "catch2/catch_message.hpp"
#include "vulkan/vulkan_core.h"

static constexpr int WIDTH = 800;
static constexpr int HEIGHT = 600;

namespace Muyo::RenderGraph
{

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: Single quad node no descriptor sets", "[RenderGraphBuilder]")
{
    // Prepare the shared quad geometry so we can draw something.
    GetMeshResourceManager()->PrepareSimpleMeshes();
    GetMeshResourceManager()->UploadMeshData();
    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    const Mesh& quad = GetMeshResourceManager()->GetQuad();

    RenderGraphBuilder builder(GetRenderDevice());

    // Graph-owned resources (allocated at Build()).
    builder.AddResource("TriangleOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_SAMPLED_BIT});
    // Externally owned mesh buffers (created by the mesh manager).
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);

    RenderGraphNodeCreateInfo quadPassCreateInfo = {
        .nodeName = "QuadNode",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("MeshVertexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("MeshIndexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("TriangleOutput"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"triangle.vert", "triangle_no_tex.frag.slang"},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1,
                                   .attachments = {{{
                                       .blendEnable = false,
                                   }}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute =
            [&quad](RenderGraphNodeContext& ctx)
        {
            // Render pass begin/end, pipeline, viewport/scissor and descriptor sets are handled by the graph.
            auto* pVertexBuffer = ctx.GetResource<VertexBuffer<Vertex>>("MeshVertexBuffer");
            auto* pIndexBuffer = ctx.GetResource<IndexBuffer>("MeshIndexBuffer");
            REQUIRE(pVertexBuffer != nullptr);
            REQUIRE(pIndexBuffer != nullptr);

            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, quad.m_nIndexCount, 1, quad.m_nIndexOffset, 0, 0);
        }};

    builder.AddNode(quadPassCreateInfo);
    builder.Build();

    {
        RenderDocScopedCapture capture("test_quad");
        builder.Execute();
    }
}

TEST_CASE_METHOD(GraphicsTestEnvMazdaScene, "RenderGraphBuilder: Mazda scene with descriptor sets", "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder(GetRenderDevice());

    // Graph-owned resources.
    builder.AddResource("TriangleOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_SAMPLED_BIT});
    builder.AddResource("PreViewData",
                        BufferResourceDesc{.count = 1,
                                           .stride = sizeof(PerViewData),
                                           .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    // Preallocated scratch buffer that the CPU node fills with actual draw commands.
    builder.AddResource("GBuffer draw commands",
                        BufferResourceDesc{.count = 1024,
                                           .stride = sizeof(VkDrawIndexedIndirectCommand),
                                           .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    // Imported resources (owned by the mesh / per-obj managers).
    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);
    builder.ImportResource("PerObjData", GetPerObjResourceManager()->GetPerObjResource());

    // Shared between the CPU node (producer) and the graphics node (consumer).
    uint32_t nDrawCommandCount = 0;

    // CPU node: builds draw commands and uploads the camera UBO on the host.
    RenderGraphNodeCreateInfo drawCommandPrepPass = {
        .nodeName = "DrawCmdPrep",
        .queueType = QueueType::CPU,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("GBuffer draw commands"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::DRAW_COMMAND_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("PreViewData"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER},
            },
        .execute =
            [this, &nDrawCommandCount](RenderGraphNodeContext& ctx)
        {
            // Camera per-view data.
            Arcball camera(glm::perspective(glm::radians(80.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F),
                           glm::lookAt(glm::vec3(0.0F, 0.0F, -2.0F), glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F)),
                           0.1F, 100.0F, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));

            PerViewData perView;
            perView.mProj = camera.GetProjMat();
            perView.mView = camera.GetViewMat();
            perView.mProjInv = glm::inverse(perView.mProj);
            perView.mViewInv = glm::inverse(perView.mView);
            perView.vScreenExtent = {WIDTH, HEIGHT};

            auto* pPreView = ctx.GetResource<BufferResource>("PreViewData");
            REQUIRE(pPreView != nullptr);
            pPreView->SetData(&perView, sizeof(perView));

            // Build indirect draw commands from the opaque scene nodes.
            std::vector<VkDrawIndexedIndirectCommand> drawCommands;
            const std::vector<const SceneNode*>& vpGeometryNodes = m_mDrawList.m_aDrawLists[DrawLists::DL_OPAQUE];
            for (const SceneNode* pGeometryNode : vpGeometryNodes)
            {
                const Geometry* pGeometry = static_cast<const GeometrySceneNode*>(pGeometryNode)->GetGeometry();
                uint32_t nSubmeshIndex = 0;
                for (const auto& pSubmesh : pGeometry->getSubmeshes())
                {
                    VkDrawIndexedIndirectCommand drawCommand;
                    const Mesh& mesh = GetMeshResourceManager()->GetMesh(pSubmesh->GetMeshIndex());

                    drawCommand.indexCount = mesh.m_nIndexCount;
                    drawCommand.instanceCount = 1;
                    drawCommand.firstIndex = mesh.m_nIndexOffset;
                    drawCommand.vertexOffset = 0;
                    drawCommand.firstInstance = PackSubmeshObjectIndex(pGeometryNode->GetPerObjId(), nSubmeshIndex++);

                    drawCommands.push_back(drawCommand);
                }
            }
            nDrawCommandCount = static_cast<uint32_t>(drawCommands.size());

            auto* pDrawCmdBuffer = ctx.GetResource<BufferResource>("GBuffer draw commands");
            REQUIRE(pDrawCmdBuffer != nullptr);
            pDrawCmdBuffer->SetData(drawCommands.data(), drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
        }};

    RenderGraphNodeCreateInfo cubePassCreateInfo = {
        .nodeName = "OpaquePass",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("MeshVertexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("MeshIndexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("PreViewData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .bindingSemantic = ResourceBindingSemantic::PER_VIEW},
                ResourceUse{.handle = ResourceHandle("PerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .bindingSemantic = ResourceBindingSemantic::PER_OBJ},
                ResourceUse{.handle = ResourceHandle("GBuffer draw commands"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::DRAW_COMMAND_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("TriangleOutput"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"forward.vert.slang", "forward.frag.slang"},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1,
                                   .attachments = {{{
                                       .blendEnable = false,
                                   }}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute =
            [this, &nDrawCommandCount](RenderGraphNodeContext& ctx)
        {
            const auto* pDrawCmdBuffer = ctx.GetResource<BufferResource>("GBuffer draw commands");
            REQUIRE(pDrawCmdBuffer != nullptr);

            const auto& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, meshManager.m_pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(ctx.commandBuffer, pDrawCmdBuffer->buffer(), 0, nDrawCommandCount,
                                     sizeof(VkDrawIndexedIndirectCommand));
        }};

    builder.AddNode(drawCommandPrepPass);
    builder.AddNode(cubePassCreateInfo);
    builder.AddDependency(cubePassCreateInfo.nodeName, drawCommandPrepPass.nodeName);
    builder.Build();

    {
        RenderDocScopedCapture capture("test_mazda_scene");
        builder.Execute();
    }
}
}  // namespace Muyo::RenderGraph
