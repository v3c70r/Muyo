#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
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
#include "SharedStructures.h"
#include "RenderGraph/DrawCommands.h"
#include "RenderResources/Geometry.h"
#include "PerObjResourceManager.h"
#include "Camera.h"
#include "RenderResources/RenderTargetResource.h"
#include "Scene/RayTracingSceneManager.h"
#include "VkExtFuncsLoader.h"
#include "catch2/catch_message.hpp"
#include "vulkan/vulkan_core.h"

static constexpr int WIDTH = 800;
static constexpr int HEIGHT = 600;

// The GPU-generated command layout must match the Vulkan indirect draw command exactly.
static_assert(sizeof(Muyo::DrawIndexedCommand) == sizeof(VkDrawIndexedIndirectCommand),
              "GPU draw command must match VkDrawIndexedIndirectCommand layout");
// PerObjData is shared with Slang. Its members must be 4-byte aligned scalars/arrays; a
// vec3/float3 member would be 16-byte aligned under std430 and silently desynchronise the
// shader from this C++ definition (which is what uploading relies on).
static_assert(sizeof(Muyo::PerSubmeshData) == 16, "PerSubmeshData must be tightly packed to match std430");
// PBRMaterial is read by GLSL (scalar layout) and Slang (std430) shaders; its members must be
// 4-byte aligned so both agree with the C++ definition used to upload the material buffer.
static_assert(sizeof(Muyo::PBRMaterial) == 96, "PBRMaterial layout changed; update shaders/shared/RenderGraph/Camera.h");
static_assert(sizeof(Muyo::PerObjData) == 64 + 4 + 12 + 32 * 16 + 32,
              "PerObjData layout changed; update shaders/shared/RenderGraph/Camera.h to match");
static_assert(sizeof(Muyo::PerViewData) == 256 + 16 + 16 + 16 + 16 + 96,
              "PerViewData layout changed; update shaders/shared/RenderGraph/Camera.h to match");

namespace Muyo::RenderGraph
{
// Copies the (already rendered) color target back to the host and returns how many
// pixels are not fully black. A value of 0 means nothing was actually drawn.
static uint32_t CountNonBlackPixels(RenderTarget* pTarget)
{
    const VkExtent2D extent = {WIDTH, HEIGHT};
    const size_t pixelSize = 8;  // R16G16B16A16_SFLOAT
    const size_t bufferSize = static_cast<size_t>(extent.width) * extent.height * pixelSize;

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);

    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            // The graph leaves color attachments in COLOR_ATTACHMENT_OPTIMAL.
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.image = pTarget->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pTarget->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback.buffer(), 1, &region);
        });

    void* pData = readback.Map();
    const uint16_t* pPixels = static_cast<const uint16_t*>(pData);
    uint32_t nNonBlack = 0;
    const size_t nPixels = static_cast<size_t>(extent.width) * extent.height;
    for (size_t i = 0; i < nPixels; ++i)
    {
        if (pPixels[i * 4 + 0] > 0 || pPixels[i * 4 + 1] > 0 || pPixels[i * 4 + 2] > 0)
        {
            ++nNonBlack;
        }
    }
    readback.Unmap();
    return nNonBlack;
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: Single quad node no descriptor sets", "[RenderGraphBuilder]")
{
    // Create a small quad directly (do NOT use MeshResourceManager — its singleton
    // state is shared with the mazda scene test and would corrupt the mesh buffers).
    std::vector<Vertex> quadVertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{ 1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}},
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 0.0f}},
    };
    std::vector<uint32_t> quadIndices = {0, 1, 2, 2, 3, 0};
    auto* pQuadVB = GetRenderResourceManager()->GetVertexBuffer<Vertex>("TestQuadVertexBuffer", quadVertices);
    auto* pQuadIB = GetRenderResourceManager()->GetIndexBuffer<uint32_t>("TestQuadIndexBuffer", quadIndices);
    const uint32_t nQuadIndexCount = static_cast<uint32_t>(quadIndices.size());

    RenderGraphBuilder builder(GetRenderDevice());

    // Graph-owned resources (allocated at Build()).
    builder.AddResource("TriangleOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_SAMPLED_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    // Externally owned quad buffers (created above, not from the mesh manager).
    builder.ImportResource("TestQuadVertexBuffer", pQuadVB);
    builder.ImportResource("TestQuadIndexBuffer", pQuadIB);

    RenderGraphNodeCreateInfo quadPassCreateInfo = {
        .nodeName = "QuadNode",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("TestQuadVertexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("TestQuadIndexBuffer"),
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
            [nQuadIndexCount](RenderGraphNodeContext& ctx)
        {
            // Render pass begin/end, pipeline, viewport/scissor and descriptor sets are handled by the graph.
            auto* pVertexBuffer = ctx.GetResource<VertexBuffer<Vertex>>("TestQuadVertexBuffer");
            auto* pIndexBuffer = ctx.GetResource<IndexBuffer>("TestQuadIndexBuffer");
            REQUIRE(pVertexBuffer != nullptr);
            REQUIRE(pIndexBuffer != nullptr);

            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, nQuadIndexCount, 1, 0, 0, 0);
        }};

    builder.AddNode(quadPassCreateInfo);
    builder.Build();

    {
        RenderDocScopedCapture capture("test_quad");
        builder.Execute();
    }

    // The quad must actually cover the viewport.
    auto* pQuadTarget = GetRenderResourceManager()->GetColorTarget("TriangleOutput");
    REQUIRE(pQuadTarget != nullptr);
    REQUIRE(CountNonBlackPixels(pQuadTarget) > 0);
}

// Result of a single GPU-driven frame: how many draw sources the CPU uploaded, how many
// survived GPU frustum culling, and how many pixels were actually shaded.
struct GPUCullingResult
{
    uint32_t nSourceCount = 0;
    uint32_t nVisibleCount = 0;
    uint32_t nNonBlackPixels = 0;
};

// Builds and runs the GPU-driven graph once for the given camera transform.
//
// The CPU only flattens the scene into DrawSource metadata; the compute pass transforms each
// object's AABB, culls it against the frustum and compacts the surviving draw commands. The
// graphics pass then issues vkCmdDrawIndexedIndirectCount using the GPU-written count.
static GPUCullingResult RunGPUCullingScenario(const DrawLists& drawList, const glm::mat4& view, const glm::mat4& proj)
{
    RenderGraphBuilder builder(GetRenderDevice());

    // Graph-owned resources.
    builder.AddResource("TriangleOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_SAMPLED_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("PreViewData",
                        BufferResourceDesc{.count = 1,
                                           .stride = sizeof(PerViewData),
                                           .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    // Scene metadata: one entry per submesh that *may* be drawn. This is the only input the
    // CPU produces for the GPU-driven path; culling and command building happen on the GPU.
    builder.AddResource("DrawSources",
                        BufferResourceDesc{.count = 1024,
                                           .stride = sizeof(DrawSource),
                                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    // GPU-written, indirect-drawn command list (compacted by the compute pass).
    builder.AddResource("GBuffer draw commands",
                        BufferResourceDesc{.count = 1024,
                                           .stride = sizeof(VkDrawIndexedIndirectCommand),
                                           .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_GPU_ONLY});
    // GPU-written atomic counter consumed by vkCmdDrawIndexedIndirectCount (and read back for the test).
    builder.AddResource("DrawCount",
                        BufferResourceDesc{.count = 1,
                                           .stride = sizeof(uint32_t),
                                           .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_GPU_ONLY});

    // Imported resources (owned by the mesh / per-obj managers).
    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);
    builder.ImportResource("PerObjData", GetPerObjResourceManager()->GetPerObjResource());

    // Number of source entries the CPU uploaded.
    uint32_t nSourceCount = 0;

    // CPU node: flattens the scene into DrawSource metadata and uploads the camera UBO.
    // It deliberately does NOT build the draw commands: that happens on the GPU below.
    RenderGraphNodeCreateInfo drawCommandPrepPass = {
        .nodeName = "DrawCmdPrep",
        .queueType = QueueType::CPU,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("DrawSources"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("PreViewData"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER},
            },
        .execute =
            [&nSourceCount, &drawList, view, proj](RenderGraphNodeContext& ctx)
        {
            PerViewData perView;
            perView.mProj = proj;
            perView.mView = view;
            perView.mProjInv = glm::inverse(perView.mProj);
            perView.mViewInv = glm::inverse(perView.mView);
            perView.vScreenExtent = {WIDTH, HEIGHT};
            // World-space frustum planes consumed by the GPU culling pass.
            ExtractFrustumPlanes(perView.mProj * perView.mView, perView.vFrustumPlanes);

            auto* pPreView = ctx.GetResource<BufferResource>("PreViewData");
            REQUIRE(pPreView != nullptr);
            pPreView->SetData(&perView, sizeof(perView));

            // Flatten the opaque scene nodes into per-submesh DrawSource metadata.
            std::vector<DrawSource> drawSources;
            const std::vector<const SceneNode*>& vpGeometryNodes = drawList.m_aDrawLists[DrawLists::DL_OPAQUE];
            for (const SceneNode* pGeometryNode : vpGeometryNodes)
            {
                const Geometry* pGeometry = static_cast<const GeometrySceneNode*>(pGeometryNode)->GetGeometry();
                uint32_t nSubmeshIndex = 0;
                for (const auto& pSubmesh : pGeometry->getSubmeshes())
                {
                    const Mesh& mesh = GetMeshResourceManager()->GetMesh(pSubmesh->GetMeshIndex());

                    DrawSource source{};
                    source.indexCount = mesh.m_nIndexCount;
                    source.firstIndex = mesh.m_nIndexOffset;
                    source.vertexOffset = 0;
                    source.perObjId = static_cast<uint32_t>(pGeometryNode->GetPerObjId());
                    source.submeshIndex = nSubmeshIndex++;

                    drawSources.push_back(source);
                }
            }
            nSourceCount = static_cast<uint32_t>(drawSources.size());
            REQUIRE(nSourceCount > 0);

            auto* pDrawSources = ctx.GetResource<BufferResource>("DrawSources");
            REQUIRE(pDrawSources != nullptr);
            pDrawSources->SetData(drawSources.data(), drawSources.size() * sizeof(DrawSource));
        }};

    // Compute node: frustum-culls the scene and generates draw commands entirely on the GPU.
    // Descriptors are bound by explicit set/binding taken from the shader reflection, so the
    // pass is free to use its own resource set instead of the built-in semantic sets.
    RenderGraphNodeCreateInfo cullingPass = {
        .nodeName = "DrawCmdGenerationPass",
        .queueType = QueueType::COMPUTE,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("DrawSources"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}},
                ResourceUse{.handle = ResourceHandle("GBuffer draw commands"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::DRAW_COMMAND_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 1}},
                ResourceUse{.handle = ResourceHandle("DrawCount"),
                            .io = ResourceIOType::READ_WRITE,
                            .usage = ResourceUsage::DRAW_COMMAND_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 2}},
                ResourceUse{.handle = ResourceHandle("PreViewData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 3}},
                ResourceUse{.handle = ResourceHandle("PerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 4}},
            },
        .shaderNames = {"prepareDrawCmdBuffer.comp.slang"},
        .execute =
            [&nSourceCount](RenderGraphNodeContext& ctx)
        {
            const auto* pDrawCount = ctx.GetResource<BufferResource>("DrawCount");
            const auto* pDrawCmdBuffer = ctx.GetResource<BufferResource>("GBuffer draw commands");
            REQUIRE(pDrawCount != nullptr);
            REQUIRE(pDrawCmdBuffer != nullptr);

            // Reset the atomic counter to zero before the dispatch.
            vkCmdFillBuffer(ctx.commandBuffer, pDrawCount->buffer(), 0, sizeof(uint32_t), 0);
            VkMemoryBarrier fillBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(ctx.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

            const uint32_t maxDrawCommands =
                static_cast<uint32_t>(pDrawCmdBuffer->GetSize() / sizeof(VkDrawIndexedIndirectCommand));
            PrepareDrawCmdParams params{};
            params.sourceCount = nSourceCount;
            params.maxDrawCommands = maxDrawCommands;
            vkCmdPushConstants(ctx.commandBuffer, ctx.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params),
                               &params);

            const uint32_t nThreads = 256;
            const uint32_t nGroups = (nSourceCount + nThreads - 1) / nThreads;
            vkCmdDispatch(ctx.commandBuffer, nGroups, 1, 1);
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
                ResourceUse{.handle = ResourceHandle("DrawCount"),
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
            [&nSourceCount](RenderGraphNodeContext& ctx)
        {
            const auto* pDrawCmdBuffer = ctx.GetResource<BufferResource>("GBuffer draw commands");
            const auto* pDrawCount = ctx.GetResource<BufferResource>("DrawCount");
            REQUIRE(pDrawCmdBuffer != nullptr);
            REQUIRE(pDrawCount != nullptr);

            const auto& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, meshManager.m_pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);

            // The draw count is produced on the GPU; the CPU never touches it.
            vkCmdDrawIndexedIndirectCount(ctx.commandBuffer, pDrawCmdBuffer->buffer(), 0, pDrawCount->buffer(), 0,
                                          nSourceCount, sizeof(VkDrawIndexedIndirectCommand));
        }};

    builder.AddNode(drawCommandPrepPass);
    builder.AddNode(cullingPass);
    builder.AddNode(cubePassCreateInfo);
    builder.AddDependency(drawCommandPrepPass.nodeName, cullingPass.nodeName);
    builder.AddDependency(cullingPass.nodeName, cubePassCreateInfo.nodeName);
    builder.Build();
    builder.Execute();

    // Read back the GPU-generated draw count.
    auto* pDrawCount = GetRenderResourceManager()->GetResource<BufferResource>("DrawCount");
    REQUIRE(pDrawCount != nullptr);

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(uint32_t));
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkBufferCopy copyRegion{};
            copyRegion.size = sizeof(uint32_t);
            vkCmdCopyBuffer(cmdBuf, pDrawCount->buffer(), readback.buffer(), 1, &copyRegion);
        });
    const uint32_t nVisibleCount = *static_cast<const uint32_t*>(readback.Map());
    readback.Unmap();

    GPUCullingResult result;
    result.nSourceCount = nSourceCount;
    result.nVisibleCount = nVisibleCount;

    auto* pSceneTarget = GetRenderResourceManager()->GetColorTarget("TriangleOutput");
    REQUIRE(pSceneTarget != nullptr);
    result.nNonBlackPixels = CountNonBlackPixels(pSceneTarget);
    return result;
}

TEST_CASE_METHOD(GraphicsTestEnvMazdaScene, "RenderGraphBuilder: GPU frustum culling", "[RenderGraphBuilder]")
{
    const glm::mat4 proj = glm::perspective(glm::radians(80.0F),
                                            static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);

    // The Mazda model has its length along +Y and its height along +Z (the importer applies a
    // model-correction rotation), and sits around (0.94, -0.17, 0.54). Look down at it from
    // above so the whole car is in frame.
    const glm::vec3 carCenter(0.94F, -0.17F, 0.54F);
    const glm::vec3 up(0.0F, 1.0F, 0.0F);
    const glm::vec3 topDownEye = carCenter + glm::vec3(0.0F, 0.0F, 3.5F);

    // Scenario 1: top-down camera looking at the car. Most of the car must survive culling and
    // actually be shaded.
    uint32_t nVisibleWhenFacing = 0;
    {
        const glm::mat4 view = glm::lookAt(topDownEye, carCenter, up);

        RenderDocScopedCapture capture("test_gpu_frustum_culling_visible");
        const GPUCullingResult result = RunGPUCullingScenario(m_mDrawList, view, proj);
        nVisibleWhenFacing = result.nVisibleCount;

        INFO("CPU draw sources = " << result.nSourceCount << ", GPU-visible = " << result.nVisibleCount
                                   << ", non-black pixels = " << result.nNonBlackPixels);

        REQUIRE(result.nVisibleCount > 0);
        REQUIRE(result.nNonBlackPixels > 0);
        // Culling must never add draws.
        REQUIRE(result.nVisibleCount <= result.nSourceCount);
    }

    // Scenario 2: same camera position, but rotated so the car is behind it. Frustum culling must
    // reject dramatically more objects and nothing may be rendered.
    {
        const glm::mat4 view = glm::lookAt(topDownEye, topDownEye + glm::vec3(0.0F, 0.0F, 1.0F), up);

        RenderDocScopedCapture capture("test_gpu_frustum_culling_away");
        const GPUCullingResult result = RunGPUCullingScenario(m_mDrawList, view, proj);

        INFO("CPU draw sources = " << result.nSourceCount << ", GPU-visible = " << result.nVisibleCount
                                   << ", non-black pixels = " << result.nNonBlackPixels);

        REQUIRE(result.nSourceCount > 0);
        // Behind the camera the frustum keeps far fewer objects (large bounds may still clip).
        REQUIRE(result.nVisibleCount < nVisibleWhenFacing);
        // Nothing is actually in front of the camera, so nothing is shaded.
        REQUIRE(result.nNonBlackPixels == 0);
    }

    // Scenario 3: same top-down view but with a reduced far plane, so the far half of the car is
    // clipped by the frustum. Some objects must survive and still draw, others must be culled.
    {
        const glm::mat4 clippedProj = glm::perspective(glm::radians(80.0F),
                                                       static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 3.6F);
        const glm::mat4 view = glm::lookAt(topDownEye, carCenter, up);

        RenderDocScopedCapture capture("test_gpu_frustum_culling_partial");
        const GPUCullingResult result = RunGPUCullingScenario(m_mDrawList, view, clippedProj);

        INFO("CPU draw sources = " << result.nSourceCount << ", GPU-visible = " << result.nVisibleCount
                                   << ", non-black pixels = " << result.nNonBlackPixels);

        // Partial culling: some geometry is kept, some is rejected, and the kept geometry draws.
        REQUIRE(result.nVisibleCount > 0);
        REQUIRE(result.nVisibleCount < result.nSourceCount);
        REQUIRE(result.nNonBlackPixels > 0);
    }
}

#ifdef FEATURE_RAY_TRACING
// Copy an image back to the host and decode the R32G32B32A32_SFLOAT pixels.
static std::vector<glm::vec4> ReadTargetFloats(RenderTarget* pTarget, VkImageLayout oldLayout, VkAccessFlags srcAccess,
                                               VkPipelineStageFlags srcStage)
{
    const VkExtent2D extent = {WIDTH, HEIGHT};
    const size_t pixelSize = 16;
    const size_t bufferSize = static_cast<size_t>(extent.width) * extent.height * pixelSize;

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.oldLayout = oldLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.image = pTarget->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmdBuf, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &barrier);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pTarget->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback.buffer(), 1, &region);
        });

    const float* pPixels = static_cast<const float*>(readback.Map());
    const size_t nPixels = static_cast<size_t>(extent.width) * extent.height;
    std::vector<glm::vec4> result(nPixels);
    for (size_t i = 0; i < nPixels; ++i)
    {
        result[i] = glm::vec4(pPixels[i * 4 + 0], pPixels[i * 4 + 1], pPixels[i * 4 + 2], pPixels[i * 4 + 3]);
    }
    readback.Unmap();
    return result;
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: ray tracing matches rasterization",
                 "[RenderGraphBuilder][RayTracing]")
{
    // Use the shared simple quad mesh from the MeshResourceManager.
    GetMeshResourceManager()->PrepareSimpleMeshes();
    GetMeshResourceManager()->UploadMeshData();
    const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();

    // Place it with a rotation + translation. Both the raster pass (through PerObjData) and the
    // TLAS instance transform use this same matrix, so the two must agree.
    const glm::mat4 model = glm::translate(glm::mat4(1.0F), glm::vec3(0.4F, -0.2F, 5.0F)) *
                            glm::rotate(glm::mat4(1.0F), glm::radians(35.0F), glm::vec3(0.0F, 0.0F, 1.0F));

    // BLAS/TLAS built by the scene manager, sharing the MeshResourceManager buffers.
    RayTracingSceneManager rtSceneManager;
    AccelerationStructure* pTLAS = rtSceneManager.BuildSceneFromMeshes({{&quadMesh, model}});
    REQUIRE(pTLAS != nullptr);

    // Frame the quad with the existing camera class.
    const glm::mat4 proj = glm::perspective(glm::radians(60.0F),
                                            static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0F, 0.0F, 10.0F), glm::vec3(0.0F, 0.0F, 5.0F),
                                       glm::vec3(0.0F, 1.0F, 0.0F));
    Arcball camera(proj, view, 0.1F, 100.0F, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));

    RenderGraphBuilder builder(GetRenderDevice());

    builder.AddResource("RTParityCamera",
                        BufferResourceDesc{.count = 1,
                                           .stride = sizeof(PerViewData),
                                           .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    builder.AddResource("RTParityRasterOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTParityDepth",
                        ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
    builder.AddResource("RTParityRayOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTParityPerObjData",
                        BufferResourceDesc{.count = 1,
                                           .stride = sizeof(PerObjData),
                                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);
    builder.ImportResource("RTParityTLAS", pTLAS);

    RenderGraphNodeCreateInfo cameraPass = {
        .nodeName = "RTParityCameraPrep",
        .queueType = QueueType::CPU,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("RTParityCamera"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("RTParityPerObjData"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER},
            },
        .execute =
            [&camera, model](RenderGraphNodeContext& ctx)
        {
            PerViewData perView;
            perView.mProj = camera.GetProjMat();
            perView.mView = camera.GetViewMat();
            perView.mProjInv = glm::inverse(perView.mProj);
            perView.mViewInv = glm::inverse(perView.mView);
            perView.vScreenExtent = {WIDTH, HEIGHT};

            auto* pCamera = ctx.GetResource<BufferResource>("RTParityCamera");
            REQUIRE(pCamera != nullptr);
            pCamera->SetData(&perView, sizeof(perView));

            PerObjData objData{};
            objData.mWorldMatrix = model;
            objData.nSubmeshCount = 1;
            objData.vSubmeshDatas[0].nMaterialIndex = 0;
            auto* pPerObj = ctx.GetResource<BufferResource>("RTParityPerObjData");
            REQUIRE(pPerObj != nullptr);
            pPerObj->SetData(&objData, sizeof(objData));
        }};

    RenderGraphNodeCreateInfo rasterPass = {
        .nodeName = "RTParityRasterPass",
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
                ResourceUse{.handle = ResourceHandle("RTParityCamera"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .bindingSemantic = ResourceBindingSemantic::PER_VIEW},
                ResourceUse{.handle = ResourceHandle("RTParityPerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .bindingSemantic = ResourceBindingSemantic::PER_OBJ},
                ResourceUse{.handle = ResourceHandle("RTParityRasterOutput"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
                ResourceUse{.handle = ResourceHandle("RTParityDepth"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::DEPTH_STENCIL_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"forward.vert.slang", "forward.frag.slang"},
        .psoDesc = {.rasterState = {.cullMode = CullMode::NONE},
                    .depthStencilState = {.depthTestEnable = true, .depthWriteEnable = true, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute =
            [&quadMesh](RenderGraphNodeContext& ctx)
        {
            const auto& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, meshManager.m_pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, quadMesh.m_nIndexCount, 1, quadMesh.m_nIndexOffset, 0,
                             PackSubmeshObjectIndex(0, 0));
        }};

    RenderGraphNodeCreateInfo rayTracingPass = {
        .nodeName = "RTParityRayTracingPass",
        .queueType = QueueType::RAY_TRACING,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("RTParityCamera"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}},
                ResourceUse{.handle = ResourceHandle("RTParityTLAS"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::ACCEL_STRUCTURE,
                            .kind = ResourceKind::ACCELERATION_STRUCTURE,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 1}},
                ResourceUse{.handle = ResourceHandle("RTParityRayOutput"),
                            .io = ResourceIOType::READ_WRITE,
                            .usage = ResourceUsage::STORAGE_IMAGE,
                            .kind = ResourceKind::IMAGE,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 2}},
            },
        .rtShaderNames = {"testPrimary.rgen.slang", "testPrimary.rmiss.slang", "testPrimary.rchit.slang"},
        .execute = [](RenderGraphNodeContext&) {}};

    builder.AddNode(cameraPass);
    builder.AddNode(rasterPass);
    builder.AddNode(rayTracingPass);
    builder.AddDependency(cameraPass.nodeName, rasterPass.nodeName);
    builder.AddDependency(cameraPass.nodeName, rayTracingPass.nodeName);
    builder.Build();
    {
        RenderDocScopedCapture capture("test_ray_tracing");
        builder.Execute();
    }

    auto* pRasterOutput = GetRenderResourceManager()->GetColorTarget("RTParityRasterOutput");
    auto* pRayOutput = GetRenderResourceManager()->GetColorTarget("RTParityRayOutput");
    REQUIRE(pRasterOutput != nullptr);
    REQUIRE(pRayOutput != nullptr);

    const std::vector<glm::vec4> rasterPixels =
        ReadTargetFloats(pRasterOutput, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    const std::vector<glm::vec4> rayPixels =
        ReadTargetFloats(pRayOutput, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    const size_t nPixels = rasterPixels.size();
    uint32_t nRasterGeometry = 0;
    uint32_t nRayGeometry = 0;
    uint32_t nBothGeometry = 0;
    uint32_t nMatching = 0;
    const float kTolerance = 1e-3F;
    for (size_t i = 0; i < nPixels; ++i)
    {
        const glm::vec3 r = glm::vec3(rasterPixels[i]);
        const glm::vec3 t = glm::vec3(rayPixels[i]);
        const bool bRasterHit = glm::length(r) > kTolerance;
        const bool bRayHit = glm::length(t) > kTolerance;
        if (bRasterHit) ++nRasterGeometry;
        if (bRayHit) ++nRayGeometry;
        if (bRasterHit && bRayHit)
        {
            ++nBothGeometry;
            if (glm::length(r - t) < kTolerance) ++nMatching;
        }
    }

    INFO("raster geometry px = " << nRasterGeometry << ", ray geometry px = " << nRayGeometry
                                 << ", overlap = " << nBothGeometry << ", matching = " << nMatching);

    // The quad must be visible in both results and they must agree.
    REQUIRE(nRasterGeometry > 0);
    REQUIRE(nRayGeometry > 0);
    // Allow a single edge pixel of difference between raster sample coverage and ray hits.
    const float fCoverage = static_cast<float>(nBothGeometry) /
                            static_cast<float>(std::max(nRasterGeometry, nRayGeometry));
    const float fMatch = static_cast<float>(nMatching) / static_cast<float>(std::max(nBothGeometry, 1u));
    REQUIRE(fCoverage > 0.999F);
    REQUIRE(fMatch > 0.999F);
}
TEST_CASE_METHOD(GraphicsTestEnvMazdaScene, "RenderGraphBuilder: ray tracing matches rasterization (Mazda scene)",
                 "[RenderGraphBuilder][RayTracing]")
{
    // Acceleration structures for the whole scene, built by the shared scene manager which
    // reuses the MeshResourceManager vertex/index buffers.
    RayTracingSceneManager rtSceneManager;
    rtSceneManager.BuildScene(m_mDrawList.m_aDrawLists[DrawLists::DL_OPAQUE]);
    AccelerationStructure* pTLAS = GetRenderResourceManager()->GetResource<AccelerationStructure>("TLAS");
    REQUIRE(pTLAS != nullptr);

    // Frame the car with the existing camera class.
    const glm::vec3 carCenter(0.94F, -0.17F, 0.54F);
    const glm::mat4 proj = glm::perspective(glm::radians(80.0F),
                                            static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);
    const glm::mat4 view =
        glm::lookAt(carCenter + glm::vec3(0.0F, 0.0F, 3.5F), carCenter, glm::vec3(0.0F, 1.0F, 0.0F));
    Arcball camera(proj, view, 0.1F, 100.0F, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));

    RenderGraphBuilder builder(GetRenderDevice());

    builder.AddResource("RTMazdaCamera",
                        BufferResourceDesc{.count = 1,
                                           .stride = sizeof(PerViewData),
                                           .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    builder.AddResource("RTMazdaRasterOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTMazdaDepth",
                        ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
    builder.AddResource("RTMazdaRayOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTMazdaDrawCommands",
                        BufferResourceDesc{.count = 1024,
                                           .stride = sizeof(VkDrawIndexedIndirectCommand),
                                           .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);
    builder.ImportResource("PerObjData", GetPerObjResourceManager()->GetPerObjResource());
    builder.ImportResource("RTMazdaTLAS", pTLAS);

    uint32_t nDrawCommandCount = 0;

    RenderGraphNodeCreateInfo cameraPass = {
        .nodeName = "RTMazdaCameraPrep",
        .queueType = QueueType::CPU,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("RTMazdaCamera"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("RTMazdaDrawCommands"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::DRAW_COMMAND_BUFFER,
                            .kind = ResourceKind::BUFFER},
            },
        .execute =
            [this, &camera, &nDrawCommandCount](RenderGraphNodeContext& ctx)
        {
            PerViewData perView;
            perView.mProj = camera.GetProjMat();
            perView.mView = camera.GetViewMat();
            perView.mProjInv = glm::inverse(perView.mProj);
            perView.mViewInv = glm::inverse(perView.mView);
            perView.vScreenExtent = {WIDTH, HEIGHT};

            auto* pCamera = ctx.GetResource<BufferResource>("RTMazdaCamera");
            REQUIRE(pCamera != nullptr);
            pCamera->SetData(&perView, sizeof(perView));

            std::vector<VkDrawIndexedIndirectCommand> drawCommands;
            for (const SceneNode* pGeometryNode : m_mDrawList.m_aDrawLists[DrawLists::DL_OPAQUE])
            {
                const Geometry* pGeometry = static_cast<const GeometrySceneNode*>(pGeometryNode)->GetGeometry();
                uint32_t nSubmeshIndex = 0;
                for (const auto& pSubmesh : pGeometry->getSubmeshes())
                {
                    const Mesh& mesh = GetMeshResourceManager()->GetMesh(pSubmesh->GetMeshIndex());
                    VkDrawIndexedIndirectCommand cmd{};
                    cmd.indexCount = mesh.m_nIndexCount;
                    cmd.instanceCount = 1;
                    cmd.firstIndex = mesh.m_nIndexOffset;
                    cmd.vertexOffset = 0;
                    cmd.firstInstance = PackSubmeshObjectIndex(pGeometryNode->GetPerObjId(), nSubmeshIndex++);
                    drawCommands.push_back(cmd);
                }
            }
            nDrawCommandCount = static_cast<uint32_t>(drawCommands.size());
            REQUIRE(nDrawCommandCount > 0);

            auto* pCommands = ctx.GetResource<BufferResource>("RTMazdaDrawCommands");
            REQUIRE(pCommands != nullptr);
            pCommands->SetData(drawCommands.data(), drawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
        }};

    RenderGraphNodeCreateInfo rasterPass = {
        .nodeName = "RTMazdaRasterPass",
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
                ResourceUse{.handle = ResourceHandle("RTMazdaCamera"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .bindingSemantic = ResourceBindingSemantic::PER_VIEW},
                ResourceUse{.handle = ResourceHandle("PerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .bindingSemantic = ResourceBindingSemantic::PER_OBJ},
                ResourceUse{.handle = ResourceHandle("RTMazdaDrawCommands"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::DRAW_COMMAND_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("RTMazdaRasterOutput"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
                ResourceUse{.handle = ResourceHandle("RTMazdaDepth"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::DEPTH_STENCIL_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"forward.vert.slang", "forward.frag.slang"},
        .psoDesc = {.rasterState = {.cullMode = CullMode::NONE},
                    .depthStencilState = {.depthTestEnable = true, .depthWriteEnable = true, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute =
            [&nDrawCommandCount](RenderGraphNodeContext& ctx)
        {
            const auto& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
            const auto* pCommands = ctx.GetResource<BufferResource>("RTMazdaDrawCommands");
            REQUIRE(pCommands != nullptr);
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, meshManager.m_pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(ctx.commandBuffer, pCommands->buffer(), 0, nDrawCommandCount,
                                     sizeof(VkDrawIndexedIndirectCommand));
        }};

    RenderGraphNodeCreateInfo rayTracingPass = {
        .nodeName = "RTMazdaRayTracingPass",
        .queueType = QueueType::RAY_TRACING,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("RTMazdaCamera"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}},
                ResourceUse{.handle = ResourceHandle("RTMazdaTLAS"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::ACCEL_STRUCTURE,
                            .kind = ResourceKind::ACCELERATION_STRUCTURE,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 1}},
                ResourceUse{.handle = ResourceHandle("RTMazdaRayOutput"),
                            .io = ResourceIOType::READ_WRITE,
                            .usage = ResourceUsage::STORAGE_IMAGE,
                            .kind = ResourceKind::IMAGE,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 2}},
            },
        .rtShaderNames = {"testPrimary.rgen.slang", "testPrimary.rmiss.slang", "testPrimary.rchit.slang"},
        .execute = [](RenderGraphNodeContext&) {}};

    builder.AddNode(cameraPass);
    builder.AddNode(rasterPass);
    builder.AddNode(rayTracingPass);
    builder.AddDependency(cameraPass.nodeName, rasterPass.nodeName);
    builder.AddDependency(cameraPass.nodeName, rayTracingPass.nodeName);
    builder.Build();
    {
        RenderDocScopedCapture capture("test_ray_tracing_mazda");
        builder.Execute();
    }

    auto* pRasterOutput = GetRenderResourceManager()->GetColorTarget("RTMazdaRasterOutput");
    auto* pRayOutput = GetRenderResourceManager()->GetColorTarget("RTMazdaRayOutput");
    REQUIRE(pRasterOutput != nullptr);
    REQUIRE(pRayOutput != nullptr);

    const std::vector<glm::vec4> rasterPixels =
        ReadTargetFloats(pRasterOutput, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    const std::vector<glm::vec4> rayPixels =
        ReadTargetFloats(pRayOutput, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    const size_t nPixels = rasterPixels.size();
    uint32_t nRasterGeometry = 0;
    uint32_t nRayGeometry = 0;
    uint32_t nBothGeometry = 0;
    uint32_t nMatching = 0;
    const float kTolerance = 1e-3F;
    for (size_t i = 0; i < nPixels; ++i)
    {
        const glm::vec3 r = glm::vec3(rasterPixels[i]);
        const glm::vec3 t = glm::vec3(rayPixels[i]);
        const bool bRasterHit = glm::length(r) > kTolerance;
        const bool bRayHit = glm::length(t) > kTolerance;
        if (bRasterHit) ++nRasterGeometry;
        if (bRayHit) ++nRayGeometry;
        if (bRasterHit && bRayHit)
        {
            ++nBothGeometry;
            if (glm::length(r - t) < kTolerance) ++nMatching;
        }
    }

    INFO("raster geometry px = " << nRasterGeometry << ", ray geometry px = " << nRayGeometry
                                 << ", overlap = " << nBothGeometry << ", matching = " << nMatching);

    // The scene must be visible in both results and they must agree on the front-most surface.
    REQUIRE(nRasterGeometry > 0);
    REQUIRE(nRayGeometry > 0);
    // Allow a few edge pixels of difference between raster sample coverage and ray hits.
    const float fCoverage = static_cast<float>(nBothGeometry) /
                            static_cast<float>(std::max(nRasterGeometry, nRayGeometry));
    const float fMatch = static_cast<float>(nMatching) / static_cast<float>(std::max(nBothGeometry, 1u));
    REQUIRE(fCoverage > 0.999F);
    REQUIRE(fMatch > 0.999F);
}
#endif  // FEATURE_RAY_TRACING
}  // namespace Muyo::RenderGraph
