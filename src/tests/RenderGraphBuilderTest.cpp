#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "Camera.h"
#include "Debug/RenderDoc.h"
#include "GraphicsTestEnv.h"
#include "MeshVertex.h"
#include "PerObjResourceManager.h"
#include "RenderGraph/CompiledRenderGraph.h"
#include "RenderGraph/DrawCommands.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphExecutor.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderGraph/RenderGraphResourceDesc.h"
#include "RenderGraphSemanticBindings.h"
#include "RenderResources/Geometry.h"
#include "RenderResources/RenderTargetResource.h"
#include "Scene/RayTracingSceneManager.h"
#include "Scene/Scene.h"
#include "SharedStructures.h"
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
static_assert(sizeof(Muyo::PBRMaterial) == 96,
              "PBRMaterial layout changed; update shaders/shared/RenderGraph/Camera.h");
static_assert(sizeof(Muyo::PerObjData) == 64 + 4 + 12 + 32 * 16 + 32,
              "PerObjData layout changed; update shaders/shared/RenderGraph/Camera.h to match");
static_assert(sizeof(Muyo::PerViewData) == 256 + 16 + 16 + 16 + 16 + 96,
              "PerViewData layout changed; update shaders/shared/RenderGraph/Camera.h to match");

namespace Muyo::RenderGraph
{
// Copies the (already rendered) color target back to the host and returns how many
// pixels are not fully black. A value of 0 means nothing was actually drawn.
static uint32_t CountNonBlackPixels(const ImageResource* pTarget)
{
    const VkExtent2D extent = {WIDTH, HEIGHT};
    const size_t pixelSize = 8;  // R16G16B16A16_SFLOAT
    const size_t bufferSize = static_cast<size_t>(extent.width) * extent.height * pixelSize;

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);

    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            // The graph leaves color attachments in COLOR_ATTACHMENT_OPTIMAL.
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.image = pTarget->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pTarget->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer(),
                                   1, &region);
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

// Decode an IEEE-754 half float (R16G16B16A16_SFLOAT stores each channel as one).
static float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (h >> 15) & 1u;
    const uint32_t exponent = (h >> 10) & 0x1Fu;
    const uint32_t mantissa = h & 0x3FFu;
    uint32_t f;
    if (exponent == 0)
    {
        f = sign << 31;
    }
    else if (exponent == 31)
    {
        f = (sign << 31) | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        f = (sign << 31) | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float out;
    memcpy(&out, &f, 4);
    return out;
}

// Copies the color target back and counts pixels whose channels are all at least the given minimum.
static uint32_t CountPixelsMatching(const ImageResource* pTarget, const glm::vec3& vMin)
{
    const VkExtent2D extent = {WIDTH, HEIGHT};
    const size_t pixelSize = 8;  // R16G16B16A16_SFLOAT
    const size_t bufferSize = static_cast<size_t>(extent.width) * extent.height * pixelSize;

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.image = pTarget->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pTarget->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer(),
                                   1, &region);
        });

    const uint16_t* pPixels = static_cast<const uint16_t*>(readback.Map());
    uint32_t nCount = 0;
    const size_t nPixels = static_cast<size_t>(extent.width) * extent.height;
    for (size_t i = 0; i < nPixels; ++i)
    {
        if (HalfToFloat(pPixels[i * 4 + 0]) >= vMin.r && HalfToFloat(pPixels[i * 4 + 1]) >= vMin.g &&
            HalfToFloat(pPixels[i * 4 + 2]) >= vMin.b)
        {
            ++nCount;
        }
    }
    readback.Unmap();
    return nCount;
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: Single quad node no descriptor sets", "[RenderGraphBuilder]")
{
    // Create a small quad directly (do NOT use MeshResourceManager — its singleton
    // state is shared with the mazda scene test and would corrupt the mesh buffers).
    std::vector<Vertex> quadVertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 0.0f}},
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
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
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
        .execute = [nQuadIndexCount](RenderGraphNodeContext& ctx)
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
    auto* pQuadTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("TriangleOutput"));
    REQUIRE(pQuadTarget != nullptr);
    REQUIRE(CountNonBlackPixels(pQuadTarget) > 0);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: executing the same builder twice", "[RenderGraphBuilder]")
{
    // This test dates from the binary-semaphore design, where the executor created a set of handover
    // semaphores per submission and destroyed the previous set at the top of Submit() - safe only
    // because Execute() drained the queues first, so the test pinned that draining. A1.4 removed the
    // objects: a slot owns no semaphores, handovers are timeline values, and reclamation is a value
    // comparison against a counter. The stale version kept passing after that change while asserting
    // nothing it claimed, which is why the comment is rewritten rather than left alone.
    //
    // What it is worth testing now is the contract that survived: two executions of one builder both
    // produce the right output, so the second reuses the slot without disturbing the first. The
    // validation layer is the authority on the aliasing half of that, since a reused timeline value or
    // a recycled command buffer still in flight would be reported there and not by pixels.
    //
    // The cross-queue half needs two segments on *different* families, so the graph below pairs an
    // async compute node with a graphics node sharing one resource. On a device without a dedicated
    // compute family both segments resolve to one family and no transfer is recorded - the WARN says
    // so rather than letting a green run imply the handover was exercised. The executor-level test
    // below covers that path on any device, because it keys on segment indices and not on families.
    if (!GetRenderDevice()->IsComputeQueueDedicated())
    {
        WARN(
            "no dedicated compute queue family on this device: both segments resolve to one family, "
            "so this test records no cross-queue transfer");
    }
    std::vector<Vertex> quadVertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 0.0f}},
    };
    std::vector<uint32_t> quadIndices = {0, 1, 2, 2, 3, 0};
    auto* pQuadVB = GetRenderResourceManager()->GetVertexBuffer<Vertex>("TwiceQuadVertexBuffer", quadVertices);
    auto* pQuadIB = GetRenderResourceManager()->GetIndexBuffer<uint32_t>("TwiceQuadIndexBuffer", quadIndices);
    const uint32_t nQuadIndexCount = static_cast<uint32_t>(quadIndices.size());

    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("TwiceOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.ImportResource("TwiceQuadVertexBuffer", pQuadVB);
    builder.ImportResource("TwiceQuadIndexBuffer", pQuadIB);

    RenderGraphNodeCreateInfo quadPass = {
        .nodeName = "TwiceQuadNode",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("TwiceQuadVertexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("TwiceQuadIndexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("TwiceOutput"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"triangle.vert", "triangle_no_tex.frag.slang"},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute = [nQuadIndexCount](RenderGraphNodeContext& ctx)
        {
            auto* pVertexBuffer = ctx.GetResource<VertexBuffer<Vertex>>("TwiceQuadVertexBuffer");
            auto* pIndexBuffer = ctx.GetResource<IndexBuffer>("TwiceQuadIndexBuffer");
            REQUIRE(pVertexBuffer != nullptr);
            REQUIRE(pIndexBuffer != nullptr);

            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, nQuadIndexCount, 1, 0, 0, 0);
        }};

    // Async compute node filling the same image: with a dedicated compute family this gives the plan
    // a compute segment handing a resource to the graphics segment, i.e. one transfer and one
    // handover semaphore.
    RenderGraphNodeCreateInfo fillPass = {
        .nodeName = "TwiceFill",
        .queueType = QueueType::COMPUTE,
        .async = true,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("TwiceOutput"),
                                     .io = ResourceIOType::READ_WRITE,
                                     .usage = ResourceUsage::STORAGE_IMAGE,
                                     .kind = ResourceKind::IMAGE,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
        .shaderNames = {"testFillColor.comp.slang"},
        .execute = [](RenderGraphNodeContext& ctx)
        { vkCmdDispatch(ctx.commandBuffer, (WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1); }};

    builder.AddNode(fillPass);
    builder.AddNode(quadPass);
    // Order the segments so the compute pass is the producer and the graphics pass the consumer.
    builder.AddDependency(fillPass.nodeName, quadPass.nodeName);
    builder.Build();

    auto* pTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("TwiceOutput"));
    REQUIRE(pTarget != nullptr);

    builder.Execute();
    REQUIRE(CountNonBlackPixels(pTarget) > 0);

    // Second execution on the same builder: it reuses the slot without disturbing the first, which is
    // what the timeline conversion made cheap - no per-submission objects to destroy, and reclamation
    // is the counter reaching the values the first execution recorded.
    builder.Execute();
    REQUIRE(CountNonBlackPixels(pTarget) > 0);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: Execute(info) chains executions through semaphores",
                 "[RenderGraphBuilder][AsyncCompute]")
{
    // Every other test calls Execute() with no argument, so RenderGraphExecuteInfo had no coverage at
    // all: nothing passed a waitSemaphore or a signalSemaphore. In particular the *join* had never
    // executed anywhere on any machine - the extra empty submission that folds several segments' done
    // semaphores into the caller's single outward one. That join is the path a windowed frame loop
    // needs, because a multi-segment frame cannot signal the caller from one terminal submit.
    if (!GetRenderDevice()->IsComputeQueueDedicated())
    {
        WARN("no dedicated compute queue family: the plan still has two segments here, but no handover");
    }

    std::vector<Vertex> quadVertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 0.0f}},
    };
    std::vector<uint32_t> quadIndices = {0, 1, 2, 2, 3, 0};
    auto* pQuadVB = GetRenderResourceManager()->GetVertexBuffer<Vertex>("ChainQuadVertexBuffer", quadVertices);
    auto* pQuadIB = GetRenderResourceManager()->GetIndexBuffer<uint32_t>("ChainQuadIndexBuffer", quadIndices);
    const uint32_t nQuadIndexCount = static_cast<uint32_t>(quadIndices.size());

    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("ChainOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.ImportResource("ChainQuadVertexBuffer", pQuadVB);
    builder.ImportResource("ChainQuadIndexBuffer", pQuadIB);

    RenderGraphNodeCreateInfo quadPass = {
        .nodeName = "ChainQuadNode",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("ChainQuadVertexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("ChainQuadIndexBuffer"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("ChainOutput"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"triangle.vert", "triangle_no_tex.frag.slang"},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute = [nQuadIndexCount](RenderGraphNodeContext& ctx)
        {
            auto* pVertexBuffer = ctx.GetResource<VertexBuffer<Vertex>>("ChainQuadVertexBuffer");
            auto* pIndexBuffer = ctx.GetResource<IndexBuffer>("ChainQuadIndexBuffer");
            REQUIRE(pVertexBuffer != nullptr);
            REQUIRE(pIndexBuffer != nullptr);

            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, nQuadIndexCount, 1, 0, 0, 0);
        }};

    RenderGraphNodeCreateInfo fillPass = {
        .nodeName = "ChainFill",
        .queueType = QueueType::COMPUTE,
        .async = true,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("ChainOutput"),
                                     .io = ResourceIOType::READ_WRITE,
                                     .usage = ResourceUsage::STORAGE_IMAGE,
                                     .kind = ResourceKind::IMAGE,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
        .shaderNames = {"testFillColor.comp.slang"},
        .execute = [](RenderGraphNodeContext& ctx)
        { vkCmdDispatch(ctx.commandBuffer, (WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1); }};

    builder.AddNode(fillPass);
    builder.AddNode(quadPass);
    builder.AddDependency(fillPass.nodeName, quadPass.nodeName);
    builder.Build();

    auto* pTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("ChainOutput"));
    REQUIRE(pTarget != nullptr);

    VkDevice device = GetRenderDevice()->GetDevice();
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkSemaphore semaphoreInitial = VK_NULL_HANDLE;
    VkSemaphore semaphoreChain = VK_NULL_HANDLE;
    VkSemaphore semaphoreFinished = VK_NULL_HANDLE;
    VkFence fencePresignal = VK_NULL_HANDLE;
    VkFence fenceFinished = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphoreInitial));
    VK_ASSERT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphoreChain));
    VK_ASSERT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphoreFinished));
    VK_ASSERT(vkCreateFence(device, &fenceInfo, nullptr, &fencePresignal));
    VK_ASSERT(vkCreateFence(device, &fenceInfo, nullptr, &fenceFinished));

    // An empty submission is the only way to observe a *binary* semaphore from the host: it cannot be
    // waited on directly, so the signal (or the wait) is routed through a queue. This is the shape
    // A1.4 replaces with timeline values, which the host can wait on.
    std::vector<VkCommandBuffer> pendingCommandBuffers;
    const auto submitEmpty = [&](VkSemaphore waitOn, VkSemaphore signalOn, VkFence fence)
    {
        VkCommandBuffer cmdBuf = GetRenderDevice()->AllocateImmediateCommandBuffer();
        pendingCommandBuffers.push_back(cmdBuf);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_ASSERT(vkBeginCommandBuffer(cmdBuf, &beginInfo));
        VK_ASSERT(vkEndCommandBuffer(cmdBuf));

        VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        waitInfo.semaphore = waitOn;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signalInfo.semaphore = signalOn;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmdInfo.commandBuffer = cmdBuf;

        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdInfo;
        if (waitOn != VK_NULL_HANDLE)
        {
            submitInfo.waitSemaphoreInfoCount = 1;
            submitInfo.pWaitSemaphoreInfos = &waitInfo;
        }
        if (signalOn != VK_NULL_HANDLE)
        {
            submitInfo.signalSemaphoreInfoCount = 1;
            submitInfo.pSignalSemaphoreInfos = &signalInfo;
        }
        VK_ASSERT(vkQueueSubmit2(GetRenderDevice()->GetImmediateQueue(), 1, &submitInfo, fence));
    };

    // Pre-signal: execution 1 must have something to wait on. Waiting the fence here is what makes it
    // ordered ahead of that execution.
    submitEmpty(VK_NULL_HANDLE, semaphoreInitial, fencePresignal);
    VK_ASSERT(vkWaitForFences(device, 1, &fencePresignal, VK_TRUE, UINT64_MAX));

    // Execution 1 waits on the pre-signalled semaphore and signals the chain semaphore. With more than
    // one segment the join is what signals it, not a terminal queue submit.
    builder.Execute(RenderGraphExecuteInfo{.waitSemaphore = semaphoreInitial, .signalSemaphore = semaphoreChain});

    // Execution 2 waits on that chain semaphore. It also covers the backpressure path: the builder
    // has one in-flight slot, so acquiring one here blocks until execution 1's fence signalled.
    builder.Execute(RenderGraphExecuteInfo{.waitSemaphore = semaphoreChain, .signalSemaphore = semaphoreFinished});

    // If the join did not signal the caller's semaphore this submission never completes, so the wait
    // is bounded rather than UINT64_MAX: unbounded, the failure mode is an infinite hang that only a
    // wall-clock timeout outside the process can catch. Bounding it turns a broken join into a
    // VK_TIMEOUT at this line, naming the contract that broke. (With layers active the validator
    // usually gets there first and rejects the unsignallable wait at submit time,
    // VUID-vkQueueSubmit2-semaphore-03873.)
    submitEmpty(semaphoreFinished, VK_NULL_HANDLE, fenceFinished);
    // VK_ASSERT reports the VkResult, so a broken join surfaces here as VK_TIMEOUT rather than as a
    // bare bool comparison.
    VK_ASSERT(vkWaitForFences(device, 1, &fenceFinished, VK_TRUE, 5'000'000'000));

    REQUIRE(CountNonBlackPixels(pTarget) > 0);

    // Freed only now: for a non-blocking submission the caller owns completion, and freeing a command
    // buffer the driver may still be reading trips VUID-vkFreeCommandBuffers-pCommandBuffers-00047.
    for (VkCommandBuffer cmdBuf : pendingCommandBuffers)
    {
        GetRenderDevice()->FreeImmediateCommandBuffer(cmdBuf);
    }
    vkDestroyFence(device, fencePresignal, nullptr);
    vkDestroyFence(device, fenceFinished, nullptr);
    vkDestroySemaphore(device, semaphoreInitial, nullptr);
    vkDestroySemaphore(device, semaphoreChain, nullptr);
    vkDestroySemaphore(device, semaphoreFinished, nullptr);
}

namespace
{
/// Begin and end every command buffer of a slot, so it can be submitted with no work in it.
/// @param slot Slot to record into.
void RecordEmptyCommandBuffers(const RenderGraphExecutor::ExecutionSlot& slot)
{
    for (VkCommandBuffer cmdBuf : slot.segmentCommandBuffers)
    {
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_ASSERT(vkBeginCommandBuffer(cmdBuf, &beginInfo));
        VK_ASSERT(vkEndCommandBuffer(cmdBuf));
    }
}
}  // namespace

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: specialization constants reach the compiled pipeline",
                 "[RenderGraphBuilder]")
{
    // #62. The graph reflected a shader's specialization constants and never applied them, so every
    // constant kept the value baked into its SPIR-V. That is invisible to the renderer, whose shaders do
    // not use them, and fatal to a consumer that does: llama.cpp's compute shaders make the workgroup
    // size and the operand types constants, so the baked defaults read out of bounds or compute the
    // wrong shape - a wrong number rather than an error.
    //
    // The shader stores both constants straight into a storage buffer, so the test distinguishes a
    // value supplied at pipeline creation from the default (1234 and 1). Revert the wiring in
    // CompileRenderGraphNode and this reads the defaults instead.
    //
    // Built from explicit services rather than the device-only constructor, so the injected path is the
    // one an end-to-end case covers. The remaining cases take the transitional constructor, which fills
    // the same three services from the engine's globals - both paths are therefore exercised, and #53's
    // claim that no internal site reaches for a global is closed by grepping the directory.
    RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
                                                   .resourceManager = GetRenderResourceManager(),
                                                   .descriptorManager = GetDescriptorManager()});
    builder.AddResource(
        "SpecOut", BufferResourceDesc{.count = 2,
                                      .stride = sizeof(uint32_t),
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    RenderGraphNodeCreateInfo node{
        .nodeName = "SpecConstantNode",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("SpecOut"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::STORAGE_BUFFER,
                                     .kind = ResourceKind::BUFFER,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
        .shaderNames = {"testSpecConstant.comp.slang"},
        .specializationConstants = {{.id = 0, .value = 7}, {.id = 1, .value = 3}},
        .execute = [](RenderGraphNodeContext& ctx) { vkCmdDispatch(ctx.commandBuffer, 1, 1, 1); }};

    builder.AddNode(node);
    builder.Build();
    builder.Execute();

    // Reachable from outside because AddResource registers the created resource with the resource
    // manager under the handle name; ResolveResource is private to the builder.
    // Through the graph rather than the resource manager: a buffer transient's memory belongs to an
    // aliasing block the graph owns, so the manager deliberately does not hold it.
    const auto* pOut = dynamic_cast<const BufferResource*>(builder.ResolveResource("SpecOut"));
    REQUIRE(pOut != nullptr);

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, 2 * sizeof(uint32_t));
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = pOut->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);

            VkBufferCopy region{};
            region.size = 2 * sizeof(uint32_t);
            vkCmdCopyBuffer(cmdBuf, pOut->buffer(), readback.buffer(), 1, &region);
        });

    const uint32_t* pValues = static_cast<const uint32_t*>(readback.Map());
    INFO("constant 0 = " << pValues[0] << ", constant 1 = " << pValues[1]);
    CHECK(pValues[0] == 7);
    CHECK(pValues[1] == 3);
    readback.Unmap();
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a failed Build() keeps its message and leaves the device clean",
                 "[RenderGraphBuilder]")
{
    // #55. A Build() precondition failure used to reach the caller as a process abort with its message
    // erased: the throw unwound out of a partially compiled node, the objects that compilation had
    // already created were never destroyed, and device teardown then reported
    // VUID-vkDestroyDevice-device-05137 for the leaked ones. The debug callback asserts on ERROR, so the
    // run stopped there - before anything had reported the exception.
    //
    // This case is the one that leaks rather than the one that merely annoys, and the ordering is the
    // reason: the pipeline layout is created before the specialization constants are validated against
    // reflection, so an id no shader declares throws *after* the device already holds objects for this
    // node. A declaration error caught earlier - ValidateTransientLifetimes runs before anything is
    // created - cannot reach this path, which is why the other Build() throw tests pass today.
    //
    // Both halves of the acceptance are this one case. The CHECK_THROWS_WITH is the first: the message
    // names the node and the constant, and the process is alive to read it. The second is the teardown
    // the fixture performs after this case returns - were the node's objects still alive, destroying the
    // device would raise the leaked-object VUID and abort the run instead of failing a case.
    RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
                                                   .resourceManager = GetRenderResourceManager(),
                                                   .descriptorManager = GetDescriptorManager()});
    builder.AddResource(
        "SpecOut", BufferResourceDesc{.count = 2,
                                      .stride = sizeof(uint32_t),
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    RenderGraphNodeCreateInfo node{
        .nodeName = "BadSpecConstantNode",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("SpecOut"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::STORAGE_BUFFER,
                                     .kind = ResourceKind::BUFFER,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
        .shaderNames = {"testSpecConstant.comp.slang"},
        // The shader declares ids 0 and 1. 42 is a typo, and it has to be reported as one.
        .specializationConstants = {{.id = 42, .value = 7}},
        .execute = [](RenderGraphNodeContext& ctx) { vkCmdDispatch(ctx.commandBuffer, 1, 1, 1); }};
    builder.AddNode(node);

    CHECK_THROWS_WITH(builder.Build(), Catch::Matchers::ContainsSubstring(
                                           "Node 'BadSpecConstantNode' supplies specialization constant 42"));

    // A second builder against the same device, because "the failure cost the graph, not the device" is
    // the part a long-running service depends on.
    RenderGraphBuilder recovered(RenderGraphServices{.device = GetRenderDevice(),
                                                     .resourceManager = GetRenderResourceManager(),
                                                     .descriptorManager = GetDescriptorManager()});
    recovered.AddResource("RecoveredOut", BufferResourceDesc{.count = 1,
                                                             .stride = sizeof(uint32_t),
                                                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                             .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    recovered.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "RecoveredNode",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("RecoveredOut"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::STORAGE_BUFFER,
                                     .kind = ResourceKind::BUFFER,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
        .shaderNames = {"testSpecConstant.comp.slang"},
        .specializationConstants = {{.id = 0, .value = 5}, {.id = 1, .value = 1}},
        .execute = [](RenderGraphNodeContext& ctx) { vkCmdDispatch(ctx.commandBuffer, 1, 1, 1); }});
    recovered.Build();
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: transients that are never live at once share one offset",
                 "[RenderGraphBuilder]")
{
    // #16's observable, on a graph built for it rather than borrowed. Two host-written buffers, used
    // strictly one after the other, so their live ranges do not overlap and the planner has nothing to be
    // careful about - which is what makes the saving real rather than incidental.
    RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
                                                   .resourceManager = GetRenderResourceManager(),
                                                   .descriptorManager = GetDescriptorManager()});
    for (const char* name : {"AliasFirst", "AliasSecond"})
    {
        builder.AddResource(name, BufferResourceDesc{.count = 4,
                                                     .stride = sizeof(uint32_t),
                                                     .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                     .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    }

    const auto addWriter = [&builder](const char* nodeName, const char* handle, uint32_t value)
    {
        builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = nodeName,
                                                  .queueType = QueueType::CPU,
                                                  .resourceUses = {ResourceUse{.handle = ResourceHandle(handle),
                                                                               .io = ResourceIOType::WRITE,
                                                                               .usage = ResourceUsage::UNIFORM_BUFFER,
                                                                               .kind = ResourceKind::BUFFER}},
                                                  .execute = [handle, value](RenderGraphNodeContext& ctx)
                                                  {
                                                      const auto* pBuffer = ctx.GetResource<BufferResource>(handle);
                                                      REQUIRE(pBuffer != nullptr);
                                                      auto* pWritable = const_cast<BufferResource*>(pBuffer);
                                                      // Through the mapping (a) gives an aliased buffer: the storage is
                                                      // the block's, so there is no allocation of the resource's own to
                                                      // write into.
                                                      std::memcpy(pWritable->Map(), &value, sizeof(value));
                                                      pWritable->Unmap();
                                                  }});
    };
    addWriter("AliasWriteA", "AliasFirst", 0xA1A1A1A1u);
    addWriter("AliasWriteB", "AliasSecond", 0xB2B2B2B2u);

    // Explicit, because the graph takes its order from declared edges and infers none: without this the
    // two writers have no relationship and their live ranges could not be told apart.
    builder.AddDependency("AliasWriteA", "AliasWriteB");
    builder.Build();

    const TransientMemoryPlan& plan = builder.GetTransientMemoryPlan();
    INFO("aliasing: " << plan.blocks.size() << " blocks, " << plan.reservedBytes << " bytes reserved for "
                      << plan.individualBytes << " bytes of transients, " << plan.SlotCount() << " offsets");
    REQUIRE(plan.blocks.size() == 1);
    REQUIRE(plan.SlotCount() == 1);
    REQUIRE(plan.blocks.front().slots.front().members.size() == 2);
    CHECK(plan.reservedBytes < plan.individualBytes);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a transient mapping must not outlive its node",
                 "[RenderGraphBuilder]")
{
    // #102. A graph-owned transient is mapped through its aliasing block, so the pointer is valid only
    // while nothing else is bound to those bytes - the live range promises that for the node whose use
    // declared the resource and nothing beyond it. A callback that maps and does not unmap breaks the
    // invariant silently: the next node in the slot writes through the same bytes. The check runs at
    // every node boundary and names both the node and the resource. With it removed, Execute() returns
    // normally and the misuse is invisible, which is the test's teeth.
    const auto buildLeakyGraph = [](QueueType queueType)
    {
        auto builder = std::make_unique<RenderGraphBuilder>(GetRenderDevice());
        builder->AddResource("Leaky", BufferResourceDesc{.count = 4,
                                                         .stride = sizeof(uint32_t),
                                                         .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                         .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
        builder->AddNode(RenderGraphNodeCreateInfo{.nodeName = "LeavesItMapped",
                                                   .queueType = queueType,
                                                   .resourceUses = {ResourceUse{.handle = ResourceHandle("Leaky"),
                                                                                .io = ResourceIOType::WRITE,
                                                                                .usage = ResourceUsage::UNIFORM_BUFFER,
                                                                                .kind = ResourceKind::BUFFER}},
                                                   .execute = [](RenderGraphNodeContext& ctx)
                                                   {
                                                       auto* pBuffer = const_cast<BufferResource*>(
                                                           ctx.GetResource<BufferResource>("Leaky"));
                                                       REQUIRE(pBuffer != nullptr);
                                                       void* pMapped = pBuffer->Map();
                                                       REQUIRE(pMapped != nullptr);
                                                       std::memset(pMapped, 0, 4 * sizeof(uint32_t));
                                                       // Deliberately no Unmap: the node-boundary check is what must
                                                       // catch this.
                                                   }});
        builder->Build();
        return builder;
    };

    // Both call sites are covered: the host-side CPU loop and the GPU recording loop. The GPU check runs
    // after the render pass is closed, so its throw cannot leave one open.
    SECTION("host-side CPU node")
    {
        auto builder = buildLeakyGraph(QueueType::CPU);
        CHECK_THROWS_WITH(builder->Execute(), Catch::Matchers::ContainsSubstring("'LeavesItMapped'") &&
                                                  Catch::Matchers::ContainsSubstring("'Leaky'"));
    }
    SECTION("GPU node recording callback")
    {
        auto builder = buildLeakyGraph(QueueType::COMPUTE);
        CHECK_THROWS_WITH(builder->Execute(), Catch::Matchers::ContainsSubstring("'LeavesItMapped'") &&
                                                  Catch::Matchers::ContainsSubstring("'Leaky'"));
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: images that are never live at once share one offset",
                 "[RenderGraphBuilder]")
{
    // The image half of the buffer case above, and the observable #16's image slice adds. Two images with
    // identical create info, written strictly one after the other, have disjoint live ranges and must land
    // in one slot with two members. An image's aliasing class *is* its create info, so identical
    // declarations are one class; before this slice images took the per-resource path, so this graph
    // reserved two images' worth of bytes and SlotCount() was zero.
    //
    // Unlike the buffer case, this one also executes: the failing test this slice fixed made its targets
    // Persistent, so nothing else in the suite drives the whole aliased-image path - create unbound, query,
    // plan, bind, then create the view. Validation is on, so a wrong order (the view before the bind) or a
    // leaked image shows up as a validation error rather than silently.
    RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
                                                   .resourceManager = GetRenderResourceManager(),
                                                   .descriptorManager = GetDescriptorManager()});
    for (const char* name : {"ImageAliasFirst", "ImageAliasSecond"})
    {
        builder.AddResource(name, ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                                    .extent = {WIDTH, HEIGHT},
                                                    .usage = VK_IMAGE_USAGE_STORAGE_BIT});
    }

    const auto addWriter = [&builder](const char* nodeName, const char* handle)
    {
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = nodeName,
            .queueType = QueueType::COMPUTE,
            .resourceUses = {ResourceUse{.handle = ResourceHandle(handle),
                                         // READ_WRITE, not WRITE: STORAGE_IMAGE+WRITE has no barrier policy
                                         // today, and the callback below only writes, so the pair is
                                         // immaterial to the live range this case is about.
                                         .io = ResourceIOType::READ_WRITE,
                                         .usage = ResourceUsage::STORAGE_IMAGE,
                                         .kind = ResourceKind::IMAGE,
                                         .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
            .shaderNames = {"testFillColor.comp.slang"},
            .execute = [](RenderGraphNodeContext& ctx) { vkCmdDispatch(ctx.commandBuffer, 1, 1, 1); }});
    };
    addWriter("ImageAliasWriteA", "ImageAliasFirst");
    addWriter("ImageAliasWriteB", "ImageAliasSecond");

    // Explicit, as in the buffer case: the graph's order comes from declared edges and infers none.
    builder.AddDependency("ImageAliasWriteA", "ImageAliasWriteB");
    builder.Build();

    const TransientMemoryPlan& plan = builder.GetTransientMemoryPlan();
    INFO("image aliasing: " << plan.blocks.size() << " blocks, " << plan.reservedBytes << " bytes reserved for "
                            << plan.individualBytes << " bytes of transients, " << plan.SlotCount() << " offsets");
    REQUIRE(plan.blocks.size() == 1);
    REQUIRE(plan.blocks.front().classKey.isImage);
    REQUIRE(plan.SlotCount() == 1);
    REQUIRE(plan.blocks.front().slots.front().members.size() == 2);
    CHECK(plan.reservedBytes < plan.individualBytes);

    builder.Execute();
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: an async and a graphics transient never share an offset",
                 "[RenderGraphBuilder][AsyncCompute]")
{
    // #106. Two same-class transients, one written on the async compute queue and one on the graphics
    // queue, with disjoint live ranges. Within one queue family the graph's barriers order the hand-over;
    // across families nothing does, because the executor waits only on a shared handle and these are two.
    // The planner must refuse the offset then - and is correct to allow it when the device has a single
    // family, where there is no cross-queue path at all.
    RenderGraphBuilder builder(GetRenderDevice());
    for (const char* name : {"AsyncTarget", "GraphicsTarget"})
    {
        builder.AddResource(name, ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                                    .extent = {WIDTH, HEIGHT},
                                                    .usage = VK_IMAGE_USAGE_STORAGE_BIT});
    }

    const auto addWriter = [&builder](const char* nodeName, const char* handle, bool bAsync)
    {
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = nodeName,
            .queueType = QueueType::COMPUTE,
            .async = bAsync,
            .resourceUses = {ResourceUse{.handle = ResourceHandle(handle),
                                         .io = ResourceIOType::READ_WRITE,
                                         .usage = ResourceUsage::STORAGE_IMAGE,
                                         .kind = ResourceKind::IMAGE,
                                         .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
            .shaderNames = {"testFillColor.comp.slang"},
            .execute = [](RenderGraphNodeContext&) {}});
    };
    addWriter("AsyncWriter", "AsyncTarget", true);
    addWriter("GraphicsWriter", "GraphicsTarget", false);
    builder.AddDependency("AsyncWriter", "GraphicsWriter");
    builder.Build();

    // The axis this case needs is the queue topology, so it states the device it ran on and asserts what
    // that topology permits rather than a fixed number.
    const bool bDedicatedComputeFamily =
        GetRenderDevice()->GetComputeQueueFamily() != GetRenderDevice()->GetGraphicsQueueFamily();
    const TransientMemoryPlan& plan = builder.GetTransientMemoryPlan();
    INFO("queue families: graphics=" << GetRenderDevice()->GetGraphicsQueueFamily() << " compute="
                                     << GetRenderDevice()->GetComputeQueueFamily() << "; slots=" << plan.SlotCount());
    // One family: same domain, so the saving is real and correct. Two families: no ordering exists across
    // them, so separate offsets are the only safe answer.
    REQUIRE(plan.SlotCount() == (bDedicatedComputeFamily ? 2u : 1u));
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a buffer range binds only that byte range",
                 "[RenderGraphBuilder]")
{
    // #81. The node binds a byte range partway into the buffer, and this shader writes gOut[0] and
    // gOut[1] - so with the range honoured the values land on the first two elements *of the range*.
    // Without it the binding starts at zero: the shader still runs and still writes, which is why the
    // test is on the values rather than on a missing feature. It fails as wrong numbers, which is the
    // failure mode the range exists to prevent.
    //
    // The offset is derived from the device rather than written down. It has to be a multiple of
    // minStorageBufferOffsetAlignment, which is 4 bytes on AMD and 16 on an NVIDIA or llvmpipe device -
    // hardcoding 8 made this test fail on every stricter one, and fail *masked*: the Build() throw
    // unwound into the fixture's teardown, whose leaked-object validation error aborted with the
    // exception text erased (review C1). Deriving it keeps the case rather than skipping it, which
    // would drop coverage on exactly the devices where the alignment rule matters.
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(GetRenderDevice()->GetPhysicalDevice(), &deviceProperties);
    const uint32_t nAlignment = deviceProperties.limits.minStorageBufferOffsetAlignment;
    const uint32_t nOffsetElements =
        std::max<uint32_t>(nAlignment, sizeof(uint32_t)) / static_cast<uint32_t>(sizeof(uint32_t));
    const uint32_t nElementCount = nOffsetElements + 2;
    INFO("minStorageBufferOffsetAlignment = " << nAlignment << " bytes, so the range starts at element "
                                              << nOffsetElements);

    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource(
        "RangedOut", BufferResourceDesc{.count = nElementCount,
                                        .stride = sizeof(uint32_t),
                                        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    RenderGraphNodeCreateInfo node{
        .nodeName = "RangedNode",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{
            .handle = ResourceHandle("RangedOut"),
            .io = ResourceIOType::WRITE,
            .usage = ResourceUsage::STORAGE_BUFFER,
            .kind = ResourceKind::BUFFER,
            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0},
            .bufferRange = BufferRange{.offset = nOffsetElements * sizeof(uint32_t), .size = 2 * sizeof(uint32_t)}}},
        .shaderNames = {"testSpecConstant.comp.slang"},
        .execute = [](RenderGraphNodeContext& ctx) { vkCmdDispatch(ctx.commandBuffer, 1, 1, 1); }};

    builder.AddNode(node);
    builder.Build();
    builder.Execute();

    // Through the graph rather than the resource manager: a buffer transient's memory belongs to an
    // aliasing block the graph owns, so the manager deliberately does not hold it.
    const auto* pOut = dynamic_cast<const BufferResource*>(builder.ResolveResource("RangedOut"));
    REQUIRE(pOut != nullptr);

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
                            nElementCount * sizeof(uint32_t));
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = pOut->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);

            VkBufferCopy region{};
            region.size = nElementCount * sizeof(uint32_t);
            vkCmdCopyBuffer(cmdBuf, pOut->buffer(), readback.buffer(), 1, &region);
        });

    const uint32_t* pValues = static_cast<const uint32_t*>(readback.Map());
    INFO("element " << nOffsetElements << " = " << pValues[nOffsetElements] << ", element " << (nOffsetElements + 1)
                    << " = " << pValues[nOffsetElements + 1] << " (the shader's baked defaults are 1234 and 1)");
    CHECK(pValues[nOffsetElements] == 1234);
    CHECK(pValues[nOffsetElements + 1] == 1);
    readback.Unmap();
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: growing an imported buffer after Build keeps its descriptor",
                 "[RenderGraphBuilder]")
{
    // #13. Descriptor sets are written at Build(), so a SetData that reallocated between Build() and
    // Execute() left the bound VkBuffer freed while the descriptor still named it - a stale handle read
    // as freed memory. A buffer now keeps its allocation and only reallocates past its capacity, so a
    // grow inside it leaves the handle the descriptor names untouched.
    const std::array<uint32_t, 4> initial{1, 2, 3, 4};
    BufferResource source(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    source.SetData(initial.data(), sizeof(initial));
    const VkBuffer bufferBefore = source.buffer();

    RenderGraphBuilder builder(GetRenderDevice());
    builder.ImportResource("Source", &source);
    builder.AddResource(
        "Copied", BufferResourceDesc{.count = 4,
                                     .stride = sizeof(uint32_t),
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    uint32_t nByteCount = 4 * sizeof(uint32_t);
    builder.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "CopiesSource",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("Source"),
                                     .io = ResourceIOType::READ,
                                     .usage = ResourceUsage::STORAGE_BUFFER,
                                     .kind = ResourceKind::BUFFER,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}},
                         ResourceUse{.handle = ResourceHandle("Copied"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::STORAGE_BUFFER,
                                     .kind = ResourceKind::BUFFER,
                                     .descriptorBinding = DescriptorBinding{.set = 1, .binding = 0}}},
        .shaderNames = {"copyBuffer.comp.slang"},
        .execute = [&nByteCount](RenderGraphNodeContext& ctx)
        {
            vkCmdPushConstants(ctx.commandBuffer, ctx.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(nByteCount), &nByteCount);
            vkCmdDispatch(ctx.commandBuffer, 1, 1, 1);
        }});

    // Build writes the descriptor set against source's current handle.
    builder.Build();

    // Grow after the descriptor was written, within the allocation. The handle must not change, or the
    // descriptor written above would name a destroyed buffer.
    const std::array<uint32_t, 8> grown{11, 22, 33, 44, 55, 66, 77, 88};
    INFO("capacity before the grow = " << source.GetCapacity() << " bytes, request = " << sizeof(grown));
    source.SetData(grown.data(), sizeof(grown));
    CHECK(source.buffer() == bufferBefore);

    builder.Execute();

    const auto* pCopied = dynamic_cast<const BufferResource*>(builder.ResolveResource("Copied"));
    REQUIRE(pCopied != nullptr);

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, 4 * sizeof(uint32_t));
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = pCopied->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);

            VkBufferCopy region{};
            region.size = 4 * sizeof(uint32_t);
            vkCmdCopyBuffer(cmdBuf, pCopied->buffer(), readback.buffer(), 1, &region);
        });

    const uint32_t* pValues = static_cast<const uint32_t*>(readback.Map());
    INFO("copied values: " << pValues[0] << ", " << pValues[1] << ", " << pValues[2] << ", " << pValues[3]);
    for (uint32_t i = 0; i < 4; ++i)
    {
        CHECK(pValues[i] == grown[i]);
    }
    readback.Unmap();
}

TEST_CASE("RenderGraphBuilder: a missing service is named and never dereferenced", "[RenderGraphBuilder]")
{
    // #53. The graph takes its device, resource allocator and descriptor allocator by construction, and a
    // missing one has to be a message rather than a null dereference inside a member initialiser - which
    // is why m_services is declared before the members that dereference it, and why this throws rather
    // than asserting. Device-independent: every rejection happens before anything touches Vulkan.
    CHECK_THROWS_WITH(RenderGraphBuilder(RenderGraphServices{}),
                      Catch::Matchers::ContainsSubstring("services.device is null"));
    CHECK_THROWS_WITH(RenderGraphBuilder(RenderGraphServices{.device = GetRenderDevice()}),
                      Catch::Matchers::ContainsSubstring("services.resourceManager is null"));
    CHECK_THROWS_WITH(RenderGraphBuilder(RenderGraphServices{.device = GetRenderDevice(),
                                                             .resourceManager = GetRenderResourceManager()}),
                      Catch::Matchers::ContainsSubstring("services.descriptorManager is null"));
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: reading a transient nothing writes fails at Build",
                 "[RenderGraphBuilder]")
{
    // #16. A transient's content is valid from its first write to its last read *within one execution*,
    // and nothing enforced that: the read took whatever the memory held, because the first reader's
    // barrier moves the image out of VK_IMAGE_LAYOUT_UNDEFINED, which discards contents rather than
    // preserving them. The message names the node, the resource, and the escape - the mistake is usually
    // a missing Persistent. Revert the check and this Build() succeeds, which is the bug.
    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("NeverWritten", BufferResourceDesc{.count = 4,
                                                           .stride = sizeof(uint32_t),
                                                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    builder.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "ReadsIt",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("NeverWritten"),
                                     .io = ResourceIOType::READ,
                                     .usage = ResourceUsage::STORAGE_BUFFER,
                                     .kind = ResourceKind::BUFFER,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
        .shaderNames = {"copyBuffer.comp.slang"},
        .execute = [](RenderGraphNodeContext&) {}});

    CHECK_THROWS_WITH(builder.Build(), Catch::Matchers::ContainsSubstring("'ReadsIt'") &&
                                           Catch::Matchers::ContainsSubstring("'NeverWritten'") &&
                                           Catch::Matchers::ContainsSubstring("Persistent"));
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a descriptor binding the shader does not declare fails at Build",
                 "[RenderGraphBuilder]")
{
    // #55. A use whose descriptorBinding matches no reflected binding used to be skipped silently while the
    // set was built, leaving a descriptor that was never written and surfacing at dispatch as a validation
    // error naming a handle. It is a declaration error and has to name the node and the binding at Build.
    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("Out", BufferResourceDesc{.count = 4,
                                                  .stride = sizeof(uint32_t),
                                                  .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                  .lifetime = ResourceLifetime::Persistent});

    // copyBuffer.comp.slang declares gInput at set 0 binding 0 and gOutput at binding 0 set 1; binding at
    // set 0 binding 7 is a mistake, not a descriptor to write.
    builder.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "WrongBinding",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("Out"),
                                     .io = ResourceIOType::READ,
                                     .usage = ResourceUsage::STORAGE_BUFFER,
                                     .kind = ResourceKind::BUFFER,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 7}}},
        .shaderNames = {"copyBuffer.comp.slang"},
        .execute = [](RenderGraphNodeContext&) {}});

    CHECK_THROWS_WITH(builder.Build(), Catch::Matchers::ContainsSubstring("WrongBinding") &&
                                           Catch::Matchers::ContainsSubstring("binding 7"));
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: an async node keeps its own compute segment on any topology",
                 "[RenderGraphBuilder]")
{
    // #133. `async` is a request, not a guarantee: it is accepted on every device. Without a dedicated
    // compute family the node still gets its own compute segment, but on the graphics family, so the plan
    // serialises it. `GetExecutionPlan()` exposes that shape and `IsComputeQueueDedicated()` is the query an
    // app uses to choose. Build() warns once when the request cannot be honoured.
    RenderGraphBuilder builder(GetRenderDevice());
    const auto makeNode = [](const char* name, bool bAsync)
    {
        return RenderGraphNodeCreateInfo{.nodeName = name,
                                         .queueType = QueueType::COMPUTE,
                                         .async = bAsync,
                                         .shaderNames = {"copyBuffer.comp.slang"},
                                         .execute = [](RenderGraphNodeContext&) {}};
    };
    builder.AddNode(makeNode("Async", true));
    builder.AddNode(makeNode("Sync", false));

    CHECK_NOTHROW(builder.Build());
    const RenderGraphExecutionPlan& plan = builder.GetExecutionPlan();
    INFO("dedicated compute family: " << GetRenderDevice()->IsComputeQueueDedicated()
                                      << " (segments=" << plan.GetSegmentCount() << ")");

    // One segment per queue key: the async node gets a compute segment whether or not a dedicated family
    // exists, and the non-async node resolves to graphics. Nothing is handed between them here, so the
    // transfer count is zero on every topology.
    CHECK(plan.GetSegmentCount() == 2);
    CHECK(plan.GetTransferCount() == 0);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a node's shape and its attachment extents are checked",
                 "[RenderGraphBuilder]")
{
    // #132. A wrong shader count compiles a pipeline with a stage missing, which fails far away at draw;
    // the attachments of one node render to one framebuffer, so their extents have to agree.
    SECTION("attachments with different extents are rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Colour", ImageResourceDesc{.format = VK_FORMAT_R8G8B8A8_UNORM,
                                                        .extent = {16, 16},
                                                        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT});
        builder.AddResource("Depth", ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                                       .extent = {32, 32},
                                                       .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
        CHECK_THROWS_WITH(builder.AddNode(RenderGraphNodeCreateInfo{
                              .nodeName = "Mismatched",
                              .queueType = QueueType::GRAPHICS,
                              .resourceUses = {ResourceUse{.handle = ResourceHandle("Colour"),
                                                           .io = ResourceIOType::WRITE,
                                                           .usage = ResourceUsage::COLOR_ATTACHMENT,
                                                           .kind = ResourceKind::IMAGE},
                                               ResourceUse{.handle = ResourceHandle("Depth"),
                                                           .io = ResourceIOType::WRITE,
                                                           .usage = ResourceUsage::DEPTH_STENCIL_ATTACHMENT,
                                                           .kind = ResourceKind::IMAGE}},
                              .shaderNames = {"forward.vert.slang", "forward.frag.slang"},
                              .execute = [](RenderGraphNodeContext&) {}}),
                          Catch::Matchers::ContainsSubstring("'Mismatched'") &&
                              Catch::Matchers::ContainsSubstring("16x16") &&
                              Catch::Matchers::ContainsSubstring("32x32"));
    }
    SECTION("a graphics node with one shader is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        CHECK_THROWS_WITH(builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "HalfPipeline",
                                                                    .queueType = QueueType::GRAPHICS,
                                                                    .shaderNames = {"forward.vert.slang"},
                                                                    .execute = [](RenderGraphNodeContext&) {}}),
                          Catch::Matchers::ContainsSubstring("'HalfPipeline'") &&
                              Catch::Matchers::ContainsSubstring("vertex and a fragment"));
    }
    SECTION("a compute node with two shaders is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        CHECK_THROWS_WITH(
            builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "TooMany",
                                                      .queueType = QueueType::COMPUTE,
                                                      .shaderNames = {"copyBuffer.comp.slang", "copyBuffer.comp.slang"},
                                                      .execute = [](RenderGraphNodeContext&) {}}),
            Catch::Matchers::ContainsSubstring("'TooMany'") && Catch::Matchers::ContainsSubstring("exactly one"));
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a binding's type and occupancy are checked at Build",
                 "[RenderGraphBuilder]")
{
    // #131. The descriptor type comes from reflection when the write is built, so a resource whose kind
    // does not fit it would be written as the shader's type and only show at draw; two uses at one binding
    // would double-write it. Both are declaration errors and are rejected at Build.
    const auto image = ImageResourceDesc{.format = VK_FORMAT_R8G8B8A8_UNORM,
                                         .extent = {16, 16},
                                         .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                         .lifetime = ResourceLifetime::Persistent};

    SECTION("a resource whose kind does not fit the reflected type is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Image", image);
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = "WrongType",
            .queueType = QueueType::COMPUTE,
            .resourceUses = {ResourceUse{.handle = ResourceHandle("Image"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::SAMPLED,
                                         .kind = ResourceKind::IMAGE,
                                         .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
            .shaderNames = {"copyBuffer.comp.slang"},
            .execute = [](RenderGraphNodeContext&) {}});

        // copyBuffer's gInput at set 0 binding 0 is a ByteAddressBuffer, i.e. a storage buffer.
        CHECK_THROWS_WITH(builder.Build(), Catch::Matchers::ContainsSubstring("'WrongType'") &&
                                               Catch::Matchers::ContainsSubstring("binds image"));
    }
    SECTION("two uses at one binding are rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        const auto storageBuffer = BufferResourceDesc{.count = 4,
                                                      .stride = 4,
                                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                      .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                      .lifetime = ResourceLifetime::Persistent};
        builder.AddResource("First", storageBuffer);
        builder.AddResource("Second", storageBuffer);
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = "DoubleBind",
            .queueType = QueueType::COMPUTE,
            .resourceUses = {ResourceUse{.handle = ResourceHandle("First"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::STORAGE_BUFFER,
                                         .kind = ResourceKind::BUFFER,
                                         .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}},
                             ResourceUse{.handle = ResourceHandle("Second"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::STORAGE_BUFFER,
                                         .kind = ResourceKind::BUFFER,
                                         .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
            .shaderNames = {"copyBuffer.comp.slang"},
            .execute = [](RenderGraphNodeContext&) {}});

        CHECK_THROWS_WITH(builder.Build(), Catch::Matchers::ContainsSubstring("'DoubleBind'") &&
                                               Catch::Matchers::ContainsSubstring("binding 0") &&
                                               Catch::Matchers::ContainsSubstring("one resource"));
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a use's kind and usage must agree with its resource",
                 "[RenderGraphBuilder]")
{
    // #128. A use selects a barrier policy; it also promises an allocation. A sampled-only image bound as
    // STORAGE_IMAGE, or a BUFFER use on an image handle, used to reach the driver as an invalid usage. It
    // is a declaration error, rejected at AddNode with both the node and the resource named.
    const auto image = [](VkImageUsageFlags usage)
    { return ImageResourceDesc{.format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {16, 16}, .usage = usage}; };
    const auto buffer = [](VkBufferUsageFlags usage)
    {
        return BufferResourceDesc{
            .count = 4, .stride = 4, .usage = usage, .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU};
    };
    const auto node = [](const char* name, ResourceUse use)
    {
        return RenderGraphNodeCreateInfo{.nodeName = name,
                                         .queueType = QueueType::COMPUTE,
                                         .resourceUses = {use},
                                         .shaderNames = {"copyBuffer.comp.slang"},
                                         .execute = [](RenderGraphNodeContext&) {}};
    };

    SECTION("a declared image missing the role's allocation bit is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Image", image(VK_IMAGE_USAGE_SAMPLED_BIT));
        CHECK_THROWS_WITH(builder.AddNode(node("BadUsage", ResourceUse{.handle = ResourceHandle("Image"),
                                                                       .io = ResourceIOType::READ_WRITE,
                                                                       .usage = ResourceUsage::STORAGE_IMAGE,
                                                                       .kind = ResourceKind::IMAGE})),
                          Catch::Matchers::ContainsSubstring("'BadUsage'") &&
                              Catch::Matchers::ContainsSubstring("'Image'") &&
                              Catch::Matchers::ContainsSubstring("VK_IMAGE_USAGE_STORAGE_BIT"));
    }
    SECTION("a declared buffer missing the role's allocation bit is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Buffer", buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        CHECK_THROWS_WITH(builder.AddNode(node("BadBufferUsage", ResourceUse{.handle = ResourceHandle("Buffer"),
                                                                             .io = ResourceIOType::READ,
                                                                             .usage = ResourceUsage::UNIFORM_BUFFER,
                                                                             .kind = ResourceKind::BUFFER})),
                          Catch::Matchers::ContainsSubstring("'BadBufferUsage'") &&
                              Catch::Matchers::ContainsSubstring("VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT"));
    }
    SECTION("an image carrying the bit is accepted")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Image", image(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT));
        CHECK_NOTHROW(builder.AddNode(node("GoodUsage", ResourceUse{.handle = ResourceHandle("Image"),
                                                                    .io = ResourceIOType::READ_WRITE,
                                                                    .usage = ResourceUsage::STORAGE_IMAGE,
                                                                    .kind = ResourceKind::IMAGE})));
    }
    SECTION("an image role on a buffer is rejected even though the kind matches")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Buffer", buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        CHECK_THROWS_WITH(builder.AddNode(node("ImageRoleOnBuffer", ResourceUse{.handle = ResourceHandle("Buffer"),
                                                                                .io = ResourceIOType::READ,
                                                                                .usage = ResourceUsage::SAMPLED,
                                                                                .kind = ResourceKind::BUFFER})),
                          Catch::Matchers::ContainsSubstring("'ImageRoleOnBuffer'") &&
                              Catch::Matchers::ContainsSubstring("not a buffer usage"));
    }
    SECTION("a kind that does not match the resource is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Image", image(VK_IMAGE_USAGE_SAMPLED_BIT));
        CHECK_THROWS_WITH(builder.AddNode(node("WrongKind", ResourceUse{.handle = ResourceHandle("Image"),
                                                                        .io = ResourceIOType::READ,
                                                                        .usage = ResourceUsage::TRANSFER_SOURCE,
                                                                        .kind = ResourceKind::BUFFER})),
                          Catch::Matchers::ContainsSubstring("'WrongKind'") &&
                              Catch::Matchers::ContainsSubstring("as a buffer") &&
                              Catch::Matchers::ContainsSubstring("an image"));
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: an invalid resource declaration is rejected at AddResource",
                 "[RenderGraphBuilder]")
{
    // #127. A desc is input: a zero size, an undefined format or a zero usage set cannot become a valid
    // allocation, so it is rejected where it was declared rather than inside the allocator or at a driver
    // VUID that names a handle.
    RenderGraphBuilder builder(GetRenderDevice());
    const auto buffer = [](uint64_t count, uint64_t stride, VkBufferUsageFlags usage)
    {
        return BufferResourceDesc{
            .count = count, .stride = stride, .usage = usage, .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU};
    };
    const auto image = [](VkFormat format, VkExtent2D extent, uint32_t mips, uint32_t layers, VkImageUsageFlags usage)
    { return ImageResourceDesc{.format = format, .extent = extent, .mips = mips, .layers = layers, .usage = usage}; };

    SECTION("buffer count 0")
    {
        CHECK_THROWS_WITH(builder.AddResource("B", buffer(0, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)),
                          Catch::Matchers::ContainsSubstring("'B'") && Catch::Matchers::ContainsSubstring("count 0"));
    }
    SECTION("buffer stride 0")
    {
        CHECK_THROWS_WITH(builder.AddResource("B", buffer(4, 0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)),
                          Catch::Matchers::ContainsSubstring("'B'") && Catch::Matchers::ContainsSubstring("stride 0"));
    }
    SECTION("image zero extent")
    {
        CHECK_THROWS_WITH(
            builder.AddResource("I", image(VK_FORMAT_R8G8B8A8_UNORM, {0, 16}, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT)),
            Catch::Matchers::ContainsSubstring("'I'") && Catch::Matchers::ContainsSubstring("zero extent"));
    }
    SECTION("image undefined format")
    {
        CHECK_THROWS_WITH(
            builder.AddResource("I", image(VK_FORMAT_UNDEFINED, {16, 16}, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT)),
            Catch::Matchers::ContainsSubstring("'I'") && Catch::Matchers::ContainsSubstring("VK_FORMAT_UNDEFINED"));
    }
    SECTION("image zero mips or layers")
    {
        CHECK_THROWS_WITH(
            builder.AddResource("I", image(VK_FORMAT_R8G8B8A8_UNORM, {16, 16}, 0, 1, VK_IMAGE_USAGE_SAMPLED_BIT)),
            Catch::Matchers::ContainsSubstring("'I'") && Catch::Matchers::ContainsSubstring("mips"));
        CHECK_THROWS_WITH(
            builder.AddResource("I", image(VK_FORMAT_R8G8B8A8_UNORM, {16, 16}, 1, 0, VK_IMAGE_USAGE_SAMPLED_BIT)),
            Catch::Matchers::ContainsSubstring("'I'") && Catch::Matchers::ContainsSubstring("layers"));
    }
    SECTION("zero usage")
    {
        CHECK_THROWS_WITH(builder.AddResource("B", buffer(4, 4, 0)),
                          Catch::Matchers::ContainsSubstring("'B'") && Catch::Matchers::ContainsSubstring("usage 0"));
        CHECK_THROWS_WITH(builder.AddResource("I", image(VK_FORMAT_R8G8B8A8_UNORM, {16, 16}, 1, 1, 0)),
                          Catch::Matchers::ContainsSubstring("'I'") && Catch::Matchers::ContainsSubstring("usage 0"));
    }
    SECTION("a valid declaration is accepted")
    {
        CHECK_NOTHROW(builder.AddResource("B", buffer(4, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)));
        CHECK_NOTHROW(
            builder.AddResource("I", image(VK_FORMAT_R8G8B8A8_UNORM, {16, 16}, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT)));
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: an image role must fit its format", "[RenderGraphBuilder]")
{
    // #143. Extents are checked separately (#132); this is the format half. A depth role on a colour
    // format and the reverse are declaration errors, and so is a storage image on a format the device
    // cannot store to.
    const auto attachmentNode = [](const char* name, const char* handle, ResourceUsage usage)
    {
        return RenderGraphNodeCreateInfo{
            .nodeName = name,
            .queueType = QueueType::GRAPHICS,
            .resourceUses = {ResourceUse{
                .handle = ResourceHandle(handle),
                .io = usage == ResourceUsage::STORAGE_IMAGE ? ResourceIOType::READ_WRITE : ResourceIOType::WRITE,
                .usage = usage,
                .kind = ResourceKind::IMAGE}},
            .shaderNames = {"forward.vert.slang", "forward.frag.slang"},
            .execute = [](RenderGraphNodeContext&) {}};
    };

    SECTION("a depth attachment on a colour format is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Colour", ImageResourceDesc{.format = VK_FORMAT_R8G8B8A8_UNORM,
                                                        .extent = {16, 16},
                                                        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                                        .lifetime = ResourceLifetime::Persistent});
        CHECK_THROWS_WITH(
            builder.AddNode(attachmentNode("DepthOnColour", "Colour", ResourceUsage::DEPTH_STENCIL_ATTACHMENT)),
            Catch::Matchers::ContainsSubstring("'DepthOnColour'") && Catch::Matchers::ContainsSubstring("'Colour'") &&
                Catch::Matchers::ContainsSubstring("is a colour format"));
    }
    SECTION("a colour attachment on a depth format is rejected")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("Depth", ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                                       .extent = {16, 16},
                                                       .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                       .lifetime = ResourceLifetime::Persistent});
        CHECK_THROWS_WITH(builder.AddNode(attachmentNode("ColourOnDepth", "Depth", ResourceUsage::COLOR_ATTACHMENT)),
                          Catch::Matchers::ContainsSubstring("'ColourOnDepth'") &&
                              Catch::Matchers::ContainsSubstring("'Depth'") &&
                              Catch::Matchers::ContainsSubstring("is a depth format"));
    }
    SECTION("a storage image on a format without storage support is rejected")
    {
        // Query for a format this device cannot store to rather than hardcoding one: `D32_SFLOAT` has
        // `STORAGE_IMAGE_BIT` on NVIDIA and not on llvmpipe, so a fixed choice asserts the author's device
        // (AGENTS.md section 4 - the review caught exactly that). `REQUIRE` keeps the test honest on a
        // device where no candidate qualifies, instead of silently passing on a different check.
        const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM,
                                       VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_B8G8R8A8_UNORM};
        VkFormat noStorageFormat = VK_FORMAT_UNDEFINED;
        for (VkFormat format : candidates)
        {
            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(GetRenderDevice()->GetPhysicalDevice(), format, &formatProperties);
            if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
            {
                noStorageFormat = format;
                break;
            }
        }

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(GetRenderDevice()->GetPhysicalDevice(), &deviceProperties);
        INFO("device: " << deviceProperties.deviceName);
        INFO("no-storage format chosen: " << static_cast<int>(noStorageFormat));
        REQUIRE(noStorageFormat != VK_FORMAT_UNDEFINED);

        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("NoStorage", ImageResourceDesc{.format = noStorageFormat,
                                                           .extent = {16, 16},
                                                           .usage = VK_IMAGE_USAGE_STORAGE_BIT,
                                                           .lifetime = ResourceLifetime::Persistent});
        CHECK_THROWS_WITH(builder.AddNode(attachmentNode("StorageOnDepth", "NoStorage", ResourceUsage::STORAGE_IMAGE)),
                          Catch::Matchers::ContainsSubstring("'StorageOnDepth'") &&
                              Catch::Matchers::ContainsSubstring("'NoStorage'") &&
                              Catch::Matchers::ContainsSubstring("no storage support"));
    }
}

TEST_CASE("BufferResource: capacity arithmetic does not truncate at 4 GiB", "[RenderGraphBuilder]")
{
    // #124. `RoundUpCapacity` returned `uint32_t`, so a 4 GiB request rounded to 256 while `SetData`
    // memcpy'd the full size into the undersized allocation. Capacity is `size_t` end to end now, so the
    // arithmetic keeps the request. A device is not needed: this is the pure helper.
    struct CapacityProbe : BufferResource
    {
        using BufferResource::RoundUpCapacity;
    };
    const size_t nFourGiB = size_t{1} << 32;
    CHECK(CapacityProbe::RoundUpCapacity(nFourGiB) == nFourGiB);
    CHECK(CapacityProbe::RoundUpCapacity(nFourGiB + 1) == nFourGiB + 256);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: Persistent and imported resources may be read unwritten",
                 "[RenderGraphBuilder]")
{
    // The two exempt cases, which are the reason the check cannot simply demand a writer. A Persistent
    // resource carries content from an earlier execution and an imported one from outside the graph, so
    // demanding a write for either would reject exactly the graphs that declare them correctly.
    SECTION("Persistent declared with AddResource")
    {
        RenderGraphBuilder builder(GetRenderDevice());
        builder.AddResource("History", BufferResourceDesc{.count = 4,
                                                          .stride = sizeof(uint32_t),
                                                          .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                          .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                          .lifetime = ResourceLifetime::Persistent});
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = "ReadsHistory",
            .queueType = QueueType::COMPUTE,
            .resourceUses = {ResourceUse{.handle = ResourceHandle("History"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::STORAGE_BUFFER,
                                         .kind = ResourceKind::BUFFER,
                                         .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
            .shaderNames = {"copyBuffer.comp.slang"},
            .execute = [](RenderGraphNodeContext&) {}});

        CHECK_NOTHROW(builder.Build());
    }

    SECTION("imported, so the graph never allocates it")
    {
        BufferResource imported(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, 4 * sizeof(uint32_t));
        RenderGraphBuilder builder(GetRenderDevice());
        builder.ImportResource("Outside", &imported);
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = "ReadsOutside",
            .queueType = QueueType::COMPUTE,
            .resourceUses = {ResourceUse{.handle = ResourceHandle("Outside"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::STORAGE_BUFFER,
                                         .kind = ResourceKind::BUFFER,
                                         .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
            .shaderNames = {"copyBuffer.comp.slang"},
            .execute = [](RenderGraphNodeContext&) {}});

        CHECK_NOTHROW(builder.Build());
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a declared resource cannot ask to be Imported",
                 "[RenderGraphBuilder]")
{
    // AddResource is the call that makes the graph allocate, so a desc saying Imported contradicts it.
    // Reported at the line that made the mistake rather than at Build, and the message names the call
    // that was meant - the trap is expecting Imported to mean "do not allocate".
    RenderGraphBuilder builder(GetRenderDevice());
    CHECK_THROWS_WITH(
        builder.AddResource("Contradiction", BufferResourceDesc{.count = 4,
                                                                .stride = sizeof(uint32_t),
                                                                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                .lifetime = ResourceLifetime::Imported}),
        Catch::Matchers::ContainsSubstring("AddResource") && Catch::Matchers::ContainsSubstring("ImportResource"));
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: a transfer round trip through the graph", "[RenderGraphBuilder]")
{
    // #18. A node that only moves data had no role to declare: the usage table covered render, compute
    // and ray-tracing access, so a copy had to borrow the usage of whatever the data *is* - and a buffer
    // written by a transfer had no policy at all (`STORAGE_BUFFER` with `WRITE` is a *host* write here,
    // which is the trap muyo-llm found). TRANSFER_SOURCE and TRANSFER_DEST are those roles, and the layout
    // transitions follow from them: an image is moved into TRANSFER_SRC/DST_OPTIMAL and on out again.
    //
    // The path is compute -> buffer -> image -> host, so both new policies and three transitions are on
    // it, and the callbacks pass the layout the use declared to vkCmdCopy* - so a transition the graph
    // failed to emit is a validation error rather than a quiet success. The only barrier the test records
    // itself is the transfer-to-host edge at the end, which is the documented boundary.
    constexpr uint32_t kExtent = 8;
    constexpr VkDeviceSize kImageBytes = static_cast<VkDeviceSize>(kExtent) * kExtent * 4;

    RenderGraphBuilder builder(GetRenderDevice());
    const ImageResourceDesc imageDesc{
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {kExtent, kExtent},
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    builder.AddResource("TransferImageIn", imageDesc);
    builder.AddResource("TransferImageOut", imageDesc);
    builder.AddResource("TransferMid",
                        BufferResourceDesc{.count = 1,
                                           .stride = kImageBytes,
                                           .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_GPU_ONLY});
    // TRANSFER_SRC as well as DST: the readback copies out of it, and an aliased transient cannot be
    // mapped through the resource - its memory belongs to the block. A buffer used as a copy source has to
    // say so, which is the declaration-based design doing its job rather than a new restriction.
    builder.AddResource("TransferReadback",
                        BufferResourceDesc{.count = 1,
                                           .stride = kImageBytes,
                                           .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    // 1. Compute writes a known colour into the first image. Existing usage, existing shader; it is here
    //    so the transfer reads something whose value the test knows.
    builder.AddNode({.nodeName = "WriteGreen",
                     .queueType = QueueType::COMPUTE,
                     .resourceUses = {ResourceUse{.handle = ResourceHandle("TransferImageIn"),
                                                  .io = ResourceIOType::READ_WRITE,
                                                  .usage = ResourceUsage::STORAGE_IMAGE,
                                                  .kind = ResourceKind::IMAGE,
                                                  .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
                     .shaderNames = {"testFillColor.comp.slang"},
                     .execute = [](RenderGraphNodeContext& ctx) { vkCmdDispatch(ctx.commandBuffer, 1, 1, 1); }});

    // 2. Image -> buffer: the first new policy on the read side, the second on the write side.
    builder.AddNode({.nodeName = "ImageToBuffer",
                     .queueType = QueueType::COPY,
                     .resourceUses = {ResourceUse{.handle = ResourceHandle("TransferImageIn"),
                                                  .io = ResourceIOType::READ,
                                                  .usage = ResourceUsage::TRANSFER_SOURCE,
                                                  .kind = ResourceKind::IMAGE},
                                      ResourceUse{.handle = ResourceHandle("TransferMid"),
                                                  .io = ResourceIOType::TRANSFER_WRITE,
                                                  .usage = ResourceUsage::TRANSFER_DEST,
                                                  .kind = ResourceKind::BUFFER}},
                     .execute = [](RenderGraphNodeContext& ctx)
                     {
                         const auto* pSrc = ctx.GetResource<ImageResource>("TransferImageIn");
                         const auto* pDst = ctx.GetResource<BufferResource>("TransferMid");
                         VkBufferImageCopy region{};
                         region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                         region.imageExtent = {kExtent, kExtent, 1};
                         // The layout is the one the use declared, so a missing transition is a validation error.
                         vkCmdCopyImageToBuffer(ctx.commandBuffer, pSrc->getImage(),
                                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDst->buffer(), 1, &region);
                     }});

    // 3. Buffer -> second image, which is the image-side TRANSFER_DEST: the graph has to move it out of
    //    UNDEFINED into TRANSFER_DST_OPTIMAL even though nothing has touched it yet.
    builder.AddNode({.nodeName = "BufferToImage",
                     .queueType = QueueType::COPY,
                     .resourceUses = {ResourceUse{.handle = ResourceHandle("TransferMid"),
                                                  .io = ResourceIOType::READ,
                                                  .usage = ResourceUsage::TRANSFER_SOURCE,
                                                  .kind = ResourceKind::BUFFER},
                                      ResourceUse{.handle = ResourceHandle("TransferImageOut"),
                                                  .io = ResourceIOType::TRANSFER_WRITE,
                                                  .usage = ResourceUsage::TRANSFER_DEST,
                                                  .kind = ResourceKind::IMAGE}},
                     .execute = [](RenderGraphNodeContext& ctx)
                     {
                         const auto* pSrc = ctx.GetResource<BufferResource>("TransferMid");
                         const auto* pDst = ctx.GetResource<ImageResource>("TransferImageOut");
                         VkBufferImageCopy region{};
                         region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                         region.imageExtent = {kExtent, kExtent, 1};
                         vkCmdCopyBufferToImage(ctx.commandBuffer, pSrc->buffer(), pDst->getImage(),
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                     }});

    // 4. Back out to a host-visible buffer, which is where the value can be checked.
    builder.AddNode({.nodeName = "MergeToReadback",
                     .queueType = QueueType::COPY,
                     .resourceUses = {ResourceUse{.handle = ResourceHandle("TransferImageOut"),
                                                  .io = ResourceIOType::READ,
                                                  .usage = ResourceUsage::TRANSFER_SOURCE,
                                                  .kind = ResourceKind::IMAGE},
                                      ResourceUse{.handle = ResourceHandle("TransferReadback"),
                                                  .io = ResourceIOType::TRANSFER_WRITE,
                                                  .usage = ResourceUsage::TRANSFER_DEST,
                                                  .kind = ResourceKind::BUFFER}},
                     .execute = [](RenderGraphNodeContext& ctx)
                     {
                         const auto* pSrc = ctx.GetResource<ImageResource>("TransferImageOut");
                         const auto* pDst = ctx.GetResource<BufferResource>("TransferReadback");
                         VkBufferImageCopy region{};
                         region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                         region.imageExtent = {kExtent, kExtent, 1};
                         vkCmdCopyImageToBuffer(ctx.commandBuffer, pSrc->getImage(),
                                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDst->buffer(), 1, &region);
                     }});

    // The graph takes its order from explicit dependencies - it does not infer an edge from one node
    // writing what the next reads - so the chain is declared here. Without it the topological sort is
    // free to run the copies in any order, and the transient check below is what catches that.
    builder.AddDependency("WriteGreen", "ImageToBuffer");
    builder.AddDependency("ImageToBuffer", "BufferToImage");
    builder.AddDependency("BufferToImage", "MergeToReadback");

    builder.Build();
    builder.Execute();

    // Through the graph rather than the resource manager: a buffer transient's memory belongs to an
    // aliasing block the graph owns, so the manager deliberately does not hold it.
    const auto* pReadback = dynamic_cast<const BufferResource*>(builder.ResolveResource("TransferReadback"));
    REQUIRE(pReadback != nullptr);

    // The graph's transient cannot be mapped through the resource, and that is a consequence of aliasing
    // rather than an accident: an aliased buffer has no allocation of its own, so `BufferResource::Map`
    // has nothing to map. Copy it out the way the other cases do - which also makes this case about
    // content rather than about the transient happening to be host-visible.
    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, 4);
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            // The graph does not order the last node against whatever reads it next (finding F4 in the
            // consumer's notes), so the transfer-to-transfer edge is the caller's.
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = pReadback->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);

            VkBufferCopy copyRegion{};
            copyRegion.size = 4;
            vkCmdCopyBuffer(cmdBuf, pReadback->buffer(), readback.buffer(), 1, &copyRegion);

            // The transfer-to-host edge, still the caller's.
            barrier = VkBufferMemoryBarrier2{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = readback.buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);
        });

    const uint8_t* pPixels = static_cast<const uint8_t*>(readback.Map());
    INFO("first pixel: r=" << static_cast<int>(pPixels[0]) << " g=" << static_cast<int>(pPixels[1])
                           << " b=" << static_cast<int>(pPixels[2]) << " a=" << static_cast<int>(pPixels[3])
                           << " (the shader writes opaque green)");
    CHECK(pPixels[0] == 0);
    CHECK(pPixels[1] == 255);
    CHECK(pPixels[2] == 0);
    CHECK(pPixels[3] == 255);
    readback.Unmap();
}

TEST_CASE("BufferRange: an unusable range is rejected with the reason named", "[RenderGraphBuilder]")
{
    // Device-independent on purpose. The validator is a free function that takes the device's alignment
    // as an argument, so the rule is tested here rather than only on a device whose
    // minStorageBufferOffsetAlignment happens to be interesting - this one's is 4 bytes, and a driver
    // is allowed to demand far more.
    //
    // The messages carry the numbers because of what these checks are for: an out-of-range binding is
    // the failure whose absence reads or writes the wrong bytes instead of reporting anything.
    CHECK_THROWS_WITH(ValidateBufferRange("Node", "Buf", BufferRange{.offset = 0, .size = 0}, 64, 4),
                      Catch::Matchers::ContainsSubstring("zero-sized") &&
                          Catch::Matchers::ContainsSubstring("'Node'") && Catch::Matchers::ContainsSubstring("'Buf'"));
    CHECK_THROWS_WITH(ValidateBufferRange("Node", "Buf", BufferRange{.offset = 32, .size = 64}, 64, 4),
                      Catch::Matchers::ContainsSubstring("[32, 96)") && Catch::Matchers::ContainsSubstring("64 bytes"));
    CHECK_THROWS_WITH(ValidateBufferRange("Node", "Buf", BufferRange{.offset = 68, .size = 4}, 64, 4),
                      Catch::Matchers::ContainsSubstring("outside the buffer's 64 bytes"));
    // The alignment is the device's number, so the message names it rather than a constant of ours.
    CHECK_THROWS_WITH(ValidateBufferRange("Node", "Buf", BufferRange{.offset = 6, .size = 4}, 64, 4),
                      Catch::Matchers::ContainsSubstring("6") &&
                          Catch::Matchers::ContainsSubstring("4-byte descriptor offset alignment"));

    // The cases that must pass, including the two boundaries: exactly the whole buffer, and an offset
    // whose only sin is being odd when the device requires no alignment at all.
    CHECK_NOTHROW(ValidateBufferRange("Node", "Buf", BufferRange{.offset = 0, .size = 64}, 64, 4));
    CHECK_NOTHROW(ValidateBufferRange("Node", "Buf", BufferRange{.offset = 8, .size = 56}, 64, 4));
    CHECK_NOTHROW(ValidateBufferRange("Node", "Buf", BufferRange{.offset = 63, .size = 1}, 64, 1));
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: specialization constants reach a graphics pipeline",
                 "[RenderGraphBuilder]")
{
    // The graphics counterpart of the compute test above, and a different code path: that stage info is
    // built inline in the graph, while this one goes through CreatePipelineFromPSODesc and
    // PipelineStateBuilder, where a single map is applied to both the vertex and fragment stages and the
    // per-stage ignoring of undeclared ids is what makes that legal. No graphics pipeline in the suite
    // passed a non-null map before this, so the threading was verified by reading only.
    std::vector<Vertex> quadVertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 0.0f}},
    };
    std::vector<uint32_t> quadIndices = {0, 1, 2, 2, 3, 0};
    auto* pQuadVB = GetRenderResourceManager()->GetVertexBuffer<Vertex>("SpecQuadVertexBuffer", quadVertices);
    auto* pQuadIB = GetRenderResourceManager()->GetIndexBuffer<uint32_t>("SpecQuadIndexBuffer", quadIndices);
    const uint32_t nQuadIndexCount = static_cast<uint32_t>(quadIndices.size());

    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("SpecOutput", ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                                        .extent = {WIDTH, HEIGHT},
                                                        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.ImportResource("SpecQuadVertexBuffer", pQuadVB);
    builder.ImportResource("SpecQuadIndexBuffer", pQuadIB);

    RenderGraphNodeCreateInfo quadPass = {
        .nodeName = "SpecQuadNode",
        .queueType = QueueType::GRAPHICS,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("SpecQuadVertexBuffer"),
                                     .io = ResourceIOType::READ,
                                     .usage = ResourceUsage::VERTEX_BUFFER,
                                     .kind = ResourceKind::BUFFER},
                         ResourceUse{.handle = ResourceHandle("SpecQuadIndexBuffer"),
                                     .io = ResourceIOType::READ,
                                     .usage = ResourceUsage::INDEX_BUFFER,
                                     .kind = ResourceKind::BUFFER},
                         ResourceUse{.handle = ResourceHandle("SpecOutput"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::COLOR_ATTACHMENT,
                                     .kind = ResourceKind::IMAGE}},
        .shaderNames = {"triangle.vert", "testSpecConstant.frag.slang"},
        // 0.25f as its raw bits, which is what a float specialization constant is: the shader reads the
        // width it declared out of this value, and that is the whole reason SpecializationValue has no
        // width of its own.
        .specializationConstants = {{.id = 0, .value = 0x3E800000ULL}},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false, .stencilEnable = false},
                    .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute = [nQuadIndexCount](RenderGraphNodeContext& ctx)
        {
            auto* pVertexBuffer = ctx.GetResource<VertexBuffer<Vertex>>("SpecQuadVertexBuffer");
            auto* pIndexBuffer = ctx.GetResource<IndexBuffer>("SpecQuadIndexBuffer");
            REQUIRE(pVertexBuffer != nullptr);
            REQUIRE(pIndexBuffer != nullptr);

            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, nQuadIndexCount, 1, 0, 0, 0);
        }};

    builder.AddNode(quadPass);
    builder.Build();
    builder.Execute();

    auto* pTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("SpecOutput"));
    REQUIRE(pTarget != nullptr);
    // The shader's baked default is 0.0, so red at all means the supplied value reached the pipeline.
    const uint32_t nRedPixels = CountPixelsMatching(pTarget, {0.2F, 0.0F, 0.0F});
    INFO("pixels at the supplied red: " << nRedPixels);
    REQUIRE(nRedPixels > 0);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphExecutor: a multi-segment handover completes through timeline values",
                 "[RenderGraphBuilder]")
{
    // Covers the path no graph-level test can reach on a single-family device:
    // RenderGraphBuilder::RebuildExecutionPlan records a transfer only when two segments resolve to
    // *different queue families*, so the double-execute test above records none. Submit() keys on
    // (producer, consumer) segment indices and knows nothing about families, so a hand-built plan with
    // one transfer exercises signal -> wait across segments on any device.
    //
    // Under the binary design this test also pinned the create/destroy lifetime of the handover
    // semaphores, because those were per submission - and it kept passing after A1.4 removed them,
    // asserting none of what it named. What is worth pinning now is that the cross-segment dependency
    // still completes with the slot reused across executions, which is the reclamation path: one slot
    // means the second AcquireSlot waits for the first execution's values.
    RenderGraphExecutionPlan plan;
    plan.segments = {{QueueType::GRAPHICS, 0, 1}, {QueueType::GRAPHICS, 1, 2}};
    plan.transfers = {{0, 1, ResourceHandle("SyntheticHandover"), 0, 1}};
    REQUIRE(plan.GetSegmentCount() == 2);
    REQUIRE(plan.GetTransferCount() == 1);

    RenderGraphExecutor executor(GetRenderDevice(), 1);

    // Three executions, not two: each hands out fresh values on the queue timeline, so a reuse that
    // waited on a stale value - or an implementation that reset the counter - shows up as a hang or a
    // validation error by the third.
    for (int run = 0; run < 3; ++run)
    {
        const RenderGraphExecutor::ExecutionSlot slot = executor.AcquireSlot(plan);
        RecordEmptyCommandBuffers(slot);
        executor.Submit(plan, slot, RenderGraphExecuteInfo{});
        executor.WaitIdle();
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphExecutor: a timeline wait holds execution until its value is signalled",
                 "[RenderGraphBuilder]")
{
    // The gap #38 documented rather than closed: it could pin the *signal* direction and not the wait
    // direction, because a binary semaphore is not host-waitable, so "this execution started before it
    // was allowed to" was unobservable in either direction. A timeline is host-waitable, and this uses
    // that to pin the wait - which is also the mechanism a long-running client chains executions with,
    // since its work outlives the call that issued it.
    //
    // Shape: submit with a wait on a value the host has not signalled. The outward fence must stay
    // unsignalled. Signal the value and it must signal. A dropped wait fails the first check.
    RenderGraphExecutionPlan plan;
    plan.segments = {{QueueType::GRAPHICS, 0, 1}};
    RenderGraphExecutor executor(GetRenderDevice(), 1);

    VkDevice device = GetRenderDevice()->GetDevice();
    // The semaphore is caller-owned: the executor's own timelines stay private, so chaining across
    // executions is expressed with a timeline the caller owns and the values it chooses.
    VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphoreInfo.pNext = &typeInfo;
    VkSemaphore gate = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &gate));

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence done = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateFence(device, &fenceInfo, nullptr, &done));

    const RenderGraphExecutor::ExecutionSlot slot = executor.AcquireSlot(plan);
    RecordEmptyCommandBuffers(slot);
    executor.Submit(plan, slot, RenderGraphExecuteInfo{.waitSemaphore = gate, .waitValue = 1, .signalFence = done});

    // The gate has not been passed, so the submission cannot complete *however fast the GPU is*.
    //
    // The wait budget matters: a zero timeout is satisfied by ordinary submit latency, so it returns
    // VK_TIMEOUT whether or not the wait was honoured, and the test passed with the wait removed. 100 ms
    // is several orders of magnitude more than an empty submission takes, so with the wait dropped the
    // fence signals well within it and this check fails - which is the mutation that has to fail.
    CHECK(vkWaitForFences(device, 1, &done, VK_TRUE, 100'000'000) == VK_TIMEOUT);

    VkSemaphoreSignalInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    signalInfo.semaphore = gate;
    signalInfo.value = 1;
    VK_ASSERT(vkSignalSemaphore(device, &signalInfo));

    // Bounded, so a wait that never completes reports a timeout at this line instead of stopping the
    // suite responding. CHECK rather than VK_ASSERT: a failure here is a test result, not an
    // unrecoverable runtime error, and aborting would take the rest of the suite with it.
    const VkResult gateResult = vkWaitForFences(device, 1, &done, VK_TRUE, 5'000'000'000);
    CHECK(gateResult == VK_SUCCESS);
    if (gateResult == VK_SUCCESS)
    {
        vkDestroyFence(device, done, nullptr);
        vkDestroySemaphore(device, gate, nullptr);
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphExecutor: signalFence completes single- and multi-segment plans",
                 "[RenderGraphBuilder]")
{
    // A1.4 exposes the caller-facing fence, which A1.3b could not offer: a submit signals at most one
    // fence, and the slot's own fence occupied it. Reclamation is a counter comparison now, so the
    // fence is free for the caller - and a caller with only this question no longer has to submit
    // something of its own just to observe completion.
    //
    // The two cases take different paths through Submit(): one segment carries the outward fence
    // itself, several need the empty join submission to fold the touched queues into one signal. The
    // multi-segment case is the join path that had never executed anywhere before #38.
    RenderGraphExecutionPlan single;
    single.segments = {{QueueType::GRAPHICS, 0, 1}};

    RenderGraphExecutionPlan multi;
    multi.segments = {{QueueType::GRAPHICS, 0, 1}, {QueueType::GRAPHICS, 1, 2}};
    multi.transfers = {{0, 1, ResourceHandle("SyntheticHandover"), 0, 1}};

    RenderGraphExecutor executor(GetRenderDevice(), 1);
    VkDevice device = GetRenderDevice()->GetDevice();

    for (const RenderGraphExecutionPlan& plan : {single, multi})
    {
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence done = VK_NULL_HANDLE;
        VK_ASSERT(vkCreateFence(device, &fenceInfo, nullptr, &done));

        const RenderGraphExecutor::ExecutionSlot slot = executor.AcquireSlot(plan);
        RecordEmptyCommandBuffers(slot);
        executor.Submit(plan, slot, RenderGraphExecuteInfo{.signalFence = done});

        // Bounded: an unsignalled fence would otherwise hang the run, and the failure this test is
        // meant to catch - the executor taking the fence and not signalling it - is exactly that.
        const VkResult fenceResult = vkWaitForFences(device, 1, &done, VK_TRUE, 5'000'000'000);
        CHECK(fenceResult == VK_SUCCESS);
        if (fenceResult == VK_SUCCESS) vkDestroyFence(device, done, nullptr);
    }
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: an unreadable shader fails loudly", "[RenderGraphBuilder]")
{
    // LoadShader returns nullopt on exactly two paths: a zero-byte or torn .spv read, or a failed
    // vkCreateShaderModule. A missing *file* was already loud (ReadSpv throws "Failed to open file"),
    // so the silent skip this guards was reachable only through the first - which is what racing a
    // concurrent Shaders rebuild in the same build directory produces, and why it never reproduced on
    // a clean rebuild. Before this it compiled a pipeline without the stage and failed several tests
    // later with a push-constant or descriptor VUID.
    const char* sScratchShader = "shaders/teeth_empty.spv";
    {
        std::ofstream empty(sScratchShader, std::ios::binary | std::ios::trunc);
        REQUIRE(empty.good());
    }

    RenderGraphBuilder builder(GetRenderDevice());
    RenderGraphNodeCreateInfo node{};
    node.nodeName = "UnreadableShaderNode";
    node.shaderNames = {"teeth_empty"};
    CHECK_THROWS_AS(builder.AddNode(node), std::runtime_error);

    std::filesystem::remove(sScratchShader);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphExecutor: an empty plan does not wedge a slot", "[RenderGraphBuilder]")
{
    // With no segments, resetting the slot fence would leave it unsignalled and the next AcquireSlot
    // on that slot would block on it forever. Reachable only through the executor: the builder returns
    // early on an empty plan, so this is a direct-API contract rather than a graph one.
    RenderGraphExecutor executor(GetRenderDevice());
    RenderGraphExecutionPlan empty;
    REQUIRE(empty.GetSegmentCount() == 0);

    const RenderGraphExecutor::ExecutionSlot first = executor.AcquireSlot(empty);
    executor.Submit(empty, first, RenderGraphExecuteInfo{});
    executor.WaitIdle();

    // The point: this returns instead of blocking on a fence that was never submitted.
    const RenderGraphExecutor::ExecutionSlot again = executor.AcquireSlot(empty);
    CHECK(again.index == first.index);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphExecutor: in-flight slots are distinct", "[RenderGraphBuilder]")
{
    // inFlightCount 2 lets a second execution be recorded and submitted before the first has
    // completed. Each slot owns its command buffers, handover semaphores and fence, so nothing is
    // shared between them - the property the blocking Execute() hides by draining.
    //
    // One segment and no transfers, so this also exercises the path where the single submission
    // signals the slot fence directly and no completion join is needed.
    RenderGraphExecutionPlan plan;
    plan.segments = {{QueueType::GRAPHICS, 0, 1}};
    REQUIRE(plan.GetTransferCount() == 0);

    RenderGraphExecutor executor(GetRenderDevice(), 2);

    const auto record = [](const RenderGraphExecutor::ExecutionSlot& slot)
    {
        for (VkCommandBuffer cmdBuf : slot.segmentCommandBuffers)
        {
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_ASSERT(vkBeginCommandBuffer(cmdBuf, &beginInfo));
            VK_ASSERT(vkEndCommandBuffer(cmdBuf));
        }
    };

    const RenderGraphExecutor::ExecutionSlot first = executor.AcquireSlot(plan);
    record(first);
    executor.Submit(plan, first, RenderGraphExecuteInfo{});

    // With two slots this hands back a different slot without waiting for the first to finish. With
    // one slot it would return the same index, and that is what the check catches rather than
    // hanging - a single empty segment completes quickly enough for AcquireSlot to return.
    const RenderGraphExecutor::ExecutionSlot second = executor.AcquireSlot(plan);
    CHECK(first.index != second.index);
    record(second);
    executor.Submit(plan, second, RenderGraphExecuteInfo{});

    executor.WaitIdle();
}

TEST_CASE("RenderGraphExecutionPlan: a handover is recorded only across differing queue families", "[RenderGraph]")
{
    // RebuildExecutionPlan compares the two segments' families and skips the transfer when they
    // match. That matched-family branch is the *only* one a single-family device ever takes -
    // llvmpipe, or an iGPU with no dedicated compute family - so it needs coverage that does not
    // depend on the machine the tests happen to run on. Driving the plan directly keeps it covered
    // now that the development device does have two families.
    CompiledRenderGraph graph;

    CompiledRenderGraphNode producer;
    producer.name = "Producer";
    producer.queueType = QueueType::COMPUTE;
    producer.async = true;  // without this the node is scheduled on the graphics queue
    producer.resourceUses = {ResolvedResourceUse{.handle = ResourceHandle("Shared")}};
    graph.AddNode(std::move(producer));

    CompiledRenderGraphNode consumer;
    consumer.name = "Consumer";
    consumer.queueType = QueueType::GRAPHICS;
    consumer.resourceUses = {ResolvedResourceUse{.handle = ResourceHandle("Shared")}};
    graph.AddNode(std::move(consumer));

    // The two queue keys give two segments either way; only the families decide the handover.
    const RenderGraphQueueFamilies oneQueueFamily{/*graphics*/ 0, /*compute*/ 0};
    const RenderGraphQueueFamilies twoQueueFamilies{/*graphics*/ 0, /*compute*/ 1};

    graph.RebuildExecutionPlan(oneQueueFamily);
    CHECK(graph.GetExecutionPlan().GetSegmentCount() == 2);
    CHECK(graph.GetExecutionPlan().GetTransferCount() == 0);

    graph.RebuildExecutionPlan(twoQueueFamilies);
    CHECK(graph.GetExecutionPlan().GetSegmentCount() == 2);
    CHECK(graph.GetExecutionPlan().GetTransferCount() == 1);
    // Producer is the async compute segment, so it reports the *compute* family.
    CHECK(graph.GetExecutionPlan().transfers[0].producerFamily == 1);
    CHECK(graph.GetExecutionPlan().transfers[0].consumerFamily == 0);
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
                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("PreViewData", BufferResourceDesc{.count = 1,
                                                          .stride = sizeof(PerViewData),
                                                          .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                          .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    // Scene metadata: one entry per submesh that *may* be drawn. This is the only input the
    // CPU produces for the GPU-driven path; culling and command building happen on the GPU.
    builder.AddResource("DrawSources", BufferResourceDesc{.count = 1024,
                                                          .stride = sizeof(DrawSource),
                                                          .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                          .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    // GPU-written, indirect-drawn command list (compacted by the compute pass).
    builder.AddResource("GBuffer draw commands", BufferResourceDesc{.count = 1024,
                                                                    .stride = sizeof(VkDrawIndexedIndirectCommand),
                                                                    .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                                    .memoryProperties = VMA_MEMORY_USAGE_GPU_ONLY});
    // GPU-written atomic counter consumed by vkCmdDrawIndexedIndirectCount (and read back for the test).
    builder.AddResource(
        "DrawCount",
        BufferResourceDesc{.count = 1,
                           .stride = sizeof(uint32_t),
                           .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
        .execute = [&nSourceCount, &drawList, view, proj](RenderGraphNodeContext& ctx)
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
    // It is explicitly marked async, so the graph schedules it on the dedicated async compute
    // queue and inserts a queue-family ownership handover plus a semaphore so the graphics pass
    // below can consume the generated draw commands.
    RenderGraphNodeCreateInfo cullingPass = {
        .nodeName = "DrawCmdGenerationPass",
        .queueType = QueueType::COMPUTE,
        .async = true,
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
        .execute = [&nSourceCount](RenderGraphNodeContext& ctx)
        {
            const auto* pDrawCount = ctx.GetResource<BufferResource>("DrawCount");
            const auto* pDrawCmdBuffer = ctx.GetResource<BufferResource>("GBuffer draw commands");
            REQUIRE(pDrawCount != nullptr);
            REQUIRE(pDrawCmdBuffer != nullptr);

            // Reset the atomic counter to zero before the dispatch.
            vkCmdFillBuffer(ctx.commandBuffer, pDrawCount->buffer(), 0, sizeof(uint32_t), 0);
            VkMemoryBarrier2 fillBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            fillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            VkDependencyInfo fillDependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            fillDependency.memoryBarrierCount = 1;
            fillDependency.pMemoryBarriers = &fillBarrier;
            vkCmdPipelineBarrier2(ctx.commandBuffer, &fillDependency);

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
                            .descriptorBinding = RenderGraphSemanticBindings::PerView},
                ResourceUse{.handle = ResourceHandle("PerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = RenderGraphSemanticBindings::PerObj},
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
        .execute = [&nSourceCount](RenderGraphNodeContext& ctx)
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

    // #16's observable, asserted rather than described: the graph shares memory between transients whose
    // live ranges do not overlap. This scenario has several of them - a draw-command buffer, the count
    // paired with it, per-view data - so the plan has to reserve less than one buffer each. If a later
    // change stops aliasing, this is what says so instead of a memory report nobody reads.
    const TransientMemoryPlan& transientPlan = builder.GetTransientMemoryPlan();
    INFO("aliasing: " << transientPlan.blocks.size() << " blocks, " << transientPlan.reservedBytes
                      << " bytes reserved for " << transientPlan.individualBytes << " bytes of transients");
    REQUIRE(transientPlan.individualBytes > 0);
    // No saving here, and that is the planner being right rather than idle: every transient in this
    // scenario is read by a later node, so every live range runs to the end of the execution and none of
    // them may share an offset. The case below is the one built so they can.
    //
    // Asserted as the semantic rather than as an inequality, because the inequality is device-dependent: a
    // slot's offset is aligned up to its widest member's alignment, and alignment limits are properties of
    // the device. On a stricter device the plan legitimately reserves *more* than the sum of the transients
    // - 53728 bytes against 53668 on an RTX 3090, 60 bytes of padding - so `reservedBytes <=
    // individualBytes` would be a claim about the author's driver rather than about the planner. This
    // mirrors #82's C1, one layer up: there the offset had to be derived from the device's alignment, here
    // the expectation about totals has to stop assuming one.
    CHECK_FALSE(transientPlan.SharesMemory());
    builder.Execute();

    // Read back the GPU-generated draw count.
    // Through the graph rather than the resource manager: a buffer transient's memory belongs to an
    // aliasing block the graph owns, so the manager deliberately does not hold it.
    const auto* pDrawCount = dynamic_cast<const BufferResource*>(builder.ResolveResource("DrawCount"));
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

    auto* pSceneTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("TriangleOutput"));
    REQUIRE(pSceneTarget != nullptr);
    result.nNonBlackPixels = CountNonBlackPixels(pSceneTarget);
    return result;
}

TEST_CASE_METHOD(GraphicsTestEnvMazdaScene, "RenderGraphBuilder: GPU frustum culling (async compute queue)",
                 "[RenderGraphBuilder][AsyncCompute]")
{
    const glm::mat4 proj =
        glm::perspective(glm::radians(80.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);

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
        const glm::mat4 clippedProj =
            glm::perspective(glm::radians(80.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 3.6F);
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
static std::vector<glm::vec4> ReadTargetFloats(const ImageResource* pTarget, VkImageLayout oldLayout,
                                               VkAccessFlags2 srcAccess, VkPipelineStageFlags2 srcStage)
{
    const VkExtent2D extent = {WIDTH, HEIGHT};
    const size_t pixelSize = 16;
    const size_t bufferSize = static_cast<size_t>(extent.width) * extent.height * pixelSize;

    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = srcStage;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.image = pTarget->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pTarget->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer(),
                                   1, &region);
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
    const glm::mat4 proj =
        glm::perspective(glm::radians(60.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);
    const glm::mat4 view =
        glm::lookAt(glm::vec3(0.0F, 0.0F, 10.0F), glm::vec3(0.0F, 0.0F, 5.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    Arcball camera(proj, view, 0.1F, 100.0F, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));

    RenderGraphBuilder builder(GetRenderDevice());

    builder.AddResource("RTParityCamera", BufferResourceDesc{.count = 1,
                                                             .stride = sizeof(PerViewData),
                                                             .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                             .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    builder.AddResource("RTParityRasterOutput", ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                                  .extent = {WIDTH, HEIGHT},
                                                                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTParityDepth", ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                                           .extent = {WIDTH, HEIGHT},
                                                           .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
    builder.AddResource("RTParityRayOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTParityPerObjData", BufferResourceDesc{.count = 1,
                                                                 .stride = sizeof(PerObjData),
                                                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                 .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);
    builder.ImportResource("RTParityTLAS", pTLAS);

    RenderGraphNodeCreateInfo cameraPass = {.nodeName = "RTParityCameraPrep",
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
                                            .execute = [&camera, model](RenderGraphNodeContext& ctx)
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
                            .descriptorBinding = RenderGraphSemanticBindings::PerView},
                ResourceUse{.handle = ResourceHandle("RTParityPerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = RenderGraphSemanticBindings::PerObj},
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
        .execute = [&quadMesh](RenderGraphNodeContext& ctx)
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

    auto* pRasterOutput = dynamic_cast<const ImageResource*>(builder.ResolveResource("RTParityRasterOutput"));
    auto* pRayOutput = dynamic_cast<const ImageResource*>(builder.ResolveResource("RTParityRayOutput"));
    REQUIRE(pRasterOutput != nullptr);
    REQUIRE(pRayOutput != nullptr);

    const std::vector<glm::vec4> rasterPixels =
        ReadTargetFloats(pRasterOutput, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    const std::vector<glm::vec4> rayPixels =
        ReadTargetFloats(pRayOutput, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_2_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);

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
    const float fCoverage =
        static_cast<float>(nBothGeometry) / static_cast<float>(std::max(nRasterGeometry, nRayGeometry));
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
    const glm::mat4 proj =
        glm::perspective(glm::radians(80.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);
    const glm::mat4 view = glm::lookAt(carCenter + glm::vec3(0.0F, 0.0F, 3.5F), carCenter, glm::vec3(0.0F, 1.0F, 0.0F));
    Arcball camera(proj, view, 0.1F, 100.0F, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));

    RenderGraphBuilder builder(GetRenderDevice());

    builder.AddResource("RTMazdaCamera", BufferResourceDesc{.count = 1,
                                                            .stride = sizeof(PerViewData),
                                                            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                            .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    builder.AddResource("RTMazdaRasterOutput", ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                                                 .extent = {WIDTH, HEIGHT},
                                                                 .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTMazdaDepth", ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                                          .extent = {WIDTH, HEIGHT},
                                                          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
    builder.AddResource("RTMazdaRayOutput",
                        ImageResourceDesc{.format = VK_FORMAT_R32G32B32A32_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("RTMazdaDrawCommands", BufferResourceDesc{.count = 1024,
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
        .execute = [this, &camera, &nDrawCommandCount](RenderGraphNodeContext& ctx)
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
                            .descriptorBinding = RenderGraphSemanticBindings::PerView},
                ResourceUse{.handle = ResourceHandle("PerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = RenderGraphSemanticBindings::PerObj},
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
        .execute = [&nDrawCommandCount](RenderGraphNodeContext& ctx)
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

    auto* pRasterOutput = dynamic_cast<const ImageResource*>(builder.ResolveResource("RTMazdaRasterOutput"));
    auto* pRayOutput = dynamic_cast<const ImageResource*>(builder.ResolveResource("RTMazdaRayOutput"));
    REQUIRE(pRasterOutput != nullptr);
    REQUIRE(pRayOutput != nullptr);

    const std::vector<glm::vec4> rasterPixels =
        ReadTargetFloats(pRasterOutput, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    const std::vector<glm::vec4> rayPixels =
        ReadTargetFloats(pRayOutput, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_2_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);

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
    const float fCoverage =
        static_cast<float>(nBothGeometry) / static_cast<float>(std::max(nRasterGeometry, nRayGeometry));
    const float fMatch = static_cast<float>(nMatching) / static_cast<float>(std::max(nBothGeometry, 1u));
    REQUIRE(fCoverage > 0.999F);
    REQUIRE(fMatch > 0.999F);
}
#endif  // FEATURE_RAY_TRACING

TEST_CASE("DependencyGraph: rejects invalid edges and stays usable", "[RenderGraph]")
{
    DependencyGraph<std::string> graph;
    graph.AddNode("a");
    graph.AddNode("b");
    graph.AddNode("c");

    REQUIRE(graph.AddEdge("a", "b"));
    REQUIRE(graph.AddEdge("a", "b"));        // duplicate is idempotent
    REQUIRE_FALSE(graph.AddEdge("b", "b"));  // self edge is rejected
    REQUIRE(graph.AddEdge("b", "c"));
    REQUIRE_FALSE(graph.AddEdge("c", "a"));  // cycle is rejected, without mutating the graph

    REQUIRE_FALSE(graph.HasCycle());
    REQUIRE(graph.NodeCount() == 3);
    REQUIRE(graph.TopologicalSort().size() == 3);
    REQUIRE(graph.IsAdjacentTo("a", "b"));
    REQUIRE_FALSE(graph.IsAdjacentTo("c", "a"));

    // An isolated node is still scheduled (regression: it used to be dropped).
    REQUIRE(graph.GetParallelExecutionLevels().size() >= 2);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: schedules nodes without dependency edges",
                 "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder(GetRenderDevice());

    int nExecuted = 0;
    for (int i = 0; i < 3; ++i)
    {
        builder.AddNode({.nodeName = "Node" + std::to_string(i),
                         .queueType = QueueType::CPU,
                         .execute = [&nExecuted](RenderGraphNodeContext&) { ++nExecuted; }});
    }

    // Only Node0 -> Node1 is connected; Node2 has no edges at all.
    builder.AddDependency("Node0", "Node1");
    builder.Build();

    // Every declared node must appear in the execution order (regression: edgeless nodes were
    // silently dropped from the topological sort).
    REQUIRE(builder.GetExecutionOrder().size() == 3);

    builder.Execute();
    REQUIRE(nExecuted == 3);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: rejects undeclared resources and GPU->CPU deps",
                 "[RenderGraphBuilder]")
{
    RenderGraphBuilder builder(GetRenderDevice());

    // A resource use that was never declared or imported must fail at declaration time.
    REQUIRE_THROWS(builder.AddNode({.nodeName = "Typo",
                                    .queueType = QueueType::CPU,
                                    .resourceUses = {ResourceUse{.handle = ResourceHandle("NotAResource"),
                                                                 .io = ResourceIOType::READ,
                                                                 .usage = ResourceUsage::STORAGE_BUFFER,
                                                                 .kind = ResourceKind::BUFFER}},
                                    .execute = [](RenderGraphNodeContext&) {}}));

    // CPU nodes run before GPU work is submitted, so they cannot depend on a GPU node.
    builder.AddNode({.nodeName = "Gpu", .queueType = QueueType::GRAPHICS, .execute = [](RenderGraphNodeContext&) {}});
    builder.AddNode({.nodeName = "Cpu", .queueType = QueueType::CPU, .execute = [](RenderGraphNodeContext&) {}});
    REQUIRE_THROWS(builder.AddDependency("Gpu", "Cpu"));
}

TEST_CASE_METHOD(GraphicsTestEnv,
                 "RenderGraphBuilder: a graphics node may use a set the built-in convention does not define",
                 "[RenderGraphBuilder]")
{
    // Before the reflection unification, `CompileRenderGraphNode` rejected any reflected binding with
    // set >= 3: graphics nodes were restricted to the engine's three built-in sets. Every node now
    // derives its layout from reflection, so an arbitrary set index is accepted. The fragment shader
    // declares set 3 binding 0, which the old table could not express, and the build is otherwise the
    // minimum a graphics node needs.
    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("Color", ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                                   .extent = {WIDTH, HEIGHT},
                                                   .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT});
    // Persistent, so the read-only uses need no writer and the case is about the descriptor path.
    builder.AddResource("Camera", BufferResourceDesc{.count = 1,
                                                     .stride = sizeof(PerViewData),
                                                     .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                     .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                     .lifetime = ResourceLifetime::Persistent});
    builder.AddResource("Extra", BufferResourceDesc{.count = 1,
                                                    .stride = sizeof(float) * 4,
                                                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                    .memoryProperties = VMA_MEMORY_USAGE_GPU_ONLY,
                                                    .lifetime = ResourceLifetime::Persistent});

    builder.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "ArbitrarySets",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("Camera"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = RenderGraphSemanticBindings::PerView},
                ResourceUse{.handle = ResourceHandle("Extra"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = DescriptorBinding{.set = 3, .binding = 0}},
                ResourceUse{.handle = ResourceHandle("Color"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"testWorldPos.vert.slang", "testExplicitSet.frag.slang"},
        .psoDesc = {.rasterState = {.cullMode = CullMode::NONE},
                    .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute = [](RenderGraphNodeContext&) {}});

    // The old restriction was a Build()-time throw; accepting set 3 is the whole assertion here.
    REQUIRE_NOTHROW(builder.Build());
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: nodes that bind the same resources share one descriptor set",
                 "[RenderGraphBuilder]")
{
    // #15 increment 2 / #16 criterion 4. Two graphics nodes binding the same camera at the same
    // (set, binding) have identical descriptor *content*, so they must share one VkDescriptorSet - the
    // dedup that makes the MATERIAL bindless array cost once rather than once per node. Different
    // resources at the same slot must not share, which is what keeps the cache safe under aliasing: a
    // set holds concrete views, so a shared set can never be handed a different image.
    const auto buildTwoNodes = [](bool bSameCamera)
    {
        auto builder = std::make_unique<RenderGraphBuilder>(GetRenderDevice());
        builder->AddResource("Color", ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                                        .extent = {WIDTH, HEIGHT},
                                                        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT});
        for (const char* name : {"CameraA", "CameraB"})
        {
            builder->AddResource(name, BufferResourceDesc{.count = 1,
                                                          .stride = sizeof(PerViewData),
                                                          .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                          .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                          .lifetime = ResourceLifetime::Persistent});
        }
        const auto addNode = [&builder](const char* nodeName, const char* camera)
        {
            builder->AddNode(RenderGraphNodeCreateInfo{
                .nodeName = nodeName,
                .queueType = QueueType::GRAPHICS,
                .resourceUses =
                    {
                        ResourceUse{.handle = ResourceHandle(camera),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::UNIFORM_BUFFER,
                                    .kind = ResourceKind::BUFFER,
                                    .descriptorBinding = RenderGraphSemanticBindings::PerView},
                        ResourceUse{.handle = ResourceHandle("Color"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::COLOR_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                    },
                .shaderNames = {"testWorldPos.vert.slang", "testWorldPos.frag.slang"},
                .psoDesc = {.rasterState = {.cullMode = CullMode::NONE},
                            .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
                .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
                .execute = [](RenderGraphNodeContext&) {}});
        };
        addNode("NodeA", "CameraA");
        addNode("NodeB", bSameCamera ? "CameraA" : "CameraB");
        builder->Build();
        return builder;
    };

    // Same content at the same slot: the two nodes share the one set.
    CHECK(buildTwoNodes(true)->GetDescriptorSetCount() == 1);
    // Different resource at the same slot: two sets, because a set names a concrete view.
    CHECK(buildTwoNodes(false)->GetDescriptorSetCount() == 2);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: the UI pass renders through a graph node",
                 "[RenderGraphBuilder]")
{
    // The RenderPassUI port, reduced to what the graph must express: the UI shaders, a combined image
    // sampler for the font texture, push constants, and a draw loop over ImDrawVert geometry. The
    // geometry is synthetic rather than ImGui's (ImGui needs a frame), but the node is the one the pass
    // would use. Rendering offscreen lets the result be read back headlessly.
    uint32_t whitePixel = 0xFFFFFFFFu;
    TextureResource* pFont = GetRenderResourceManager()->GetTexture("ui_font_texture", &whitePixel, 1, 1);
    REQUIRE(pFont != nullptr);

    const std::vector<ImDrawVert> vertices = {
        ImDrawVert{{-1.0F, -1.0F}, {0.0F, 0.0F}, IM_COL32(255, 255, 255, 255)},
        ImDrawVert{{1.0F, -1.0F}, {1.0F, 0.0F}, IM_COL32(255, 255, 255, 255)},
        ImDrawVert{{0.0F, 1.0F}, {0.5F, 1.0F}, IM_COL32(255, 255, 255, 255)},
    };
    const std::vector<ImDrawIdx> indices = {0, 1, 2};
    auto* pVertices = GetRenderResourceManager()->GetVertexBuffer<ImDrawVert>("UIVertexBuffer", vertices, false);
    auto* pIndices = GetRenderResourceManager()->GetIndexBuffer("UIIndexBuffer", indices, false);

    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource(
        "UITarget", ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                      .extent = {WIDTH, HEIGHT},
                                      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.ImportResource("Font", pFont);
    builder.ImportResource("UIVertices", pVertices);
    builder.ImportResource("UIIndices", pIndices);

    builder.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "UI",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("Font"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::SAMPLED,
                            .kind = ResourceKind::IMAGE,
                            .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}},
                ResourceUse{.handle = ResourceHandle("UIVertices"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("UIIndices"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("UITarget"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"ui.vert.slang", "ui.frag"},
        .psoDesc = {.vertexType = VertexType::UI,
                    .rasterState = {.cullMode = CullMode::NONE},
                    .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute = [](RenderGraphNodeContext& ctx)
        {
            struct PushConstBlock
            {
                glm::vec2 scale;
                glm::vec2 translate;
            };
            const PushConstBlock pushConstants{glm::vec2(1.0F, 1.0F), glm::vec2(0.0F, 0.0F)};
            vkCmdPushConstants(ctx.commandBuffer, ctx.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(pushConstants), &pushConstants);

            const auto* pVB = ctx.GetResource<BufferResource>("UIVertices");
            const auto* pIB = ctx.GetResource<BufferResource>("UIIndices");
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVB->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIB->buffer(), 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(ctx.commandBuffer, 3, 1, 0, 0, 0);
        }});

    builder.Build();
    builder.Execute();

    const auto* pTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("UITarget"));
    REQUIRE(pTarget != nullptr);
    // The white texture times the white vertex colours fills the triangle; a null sampler or a missing
    // descriptor would instead be a validation error or a black readback.
    REQUIRE(CountNonBlackPixels(pTarget) > 0);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: graphics nodes rebind semantic sets independently",
                 "[RenderGraphBuilder]")
{
    // Two graphics nodes bind *different* PER_VIEW buffers. They must each get their own descriptor
    // set, otherwise the single submitted command buffer would run both with whichever camera was
    // written last (regression: the three semantic sets used to be shared per builder).
    GetMeshResourceManager()->PrepareSimpleMeshes();
    GetMeshResourceManager()->UploadMeshData();
    const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();

    RenderGraphBuilder builder(GetRenderDevice());
    for (const char* name : {"CameraA", "CameraB"})
    {
        builder.AddResource(name, BufferResourceDesc{.count = 1,
                                                     .stride = sizeof(PerViewData),
                                                     .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                     .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    }
    for (const char* name : {"TargetA", "TargetB"})
    {
        // Persistent, not the default Transient: this test reads both images back after Execute() to see
        // which camera each node used, and a transient's content is promised only from its first write to
        // its last read *within* one execution. With a single write and no in-graph read, TargetA's
        // declared range is one node wide, so the planner is free to bind TargetB over it - which it did,
        // and TargetA read back black (the two targets were aliased at one offset). Nothing about aliasing
        // is under test here; the subject is that two graphics nodes each get their own PER_VIEW set.
        builder.AddResource(
            name, ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                    .extent = {WIDTH, HEIGHT},
                                    .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    .lifetime = ResourceLifetime::Persistent});
    }

    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);

    RenderGraphNodeCreateInfo cameraPass = {
        .nodeName = "CameraPrep",
        .queueType = QueueType::CPU,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("CameraA"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::UNIFORM_BUFFER,
                                     .kind = ResourceKind::BUFFER},
                         ResourceUse{.handle = ResourceHandle("CameraB"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::UNIFORM_BUFFER,
                                     .kind = ResourceKind::BUFFER}},
        .execute = [](RenderGraphNodeContext& ctx)
        {
            const auto makePerView = [](const glm::vec3& eye, const glm::vec3& center)
            {
                const glm::mat4 proj = glm::perspective(
                    glm::radians(60.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);
                const glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0F, 1.0F, 0.0F));
                PerViewData perView;
                perView.mProj = proj;
                perView.mView = view;
                perView.mProjInv = glm::inverse(proj);
                perView.mViewInv = glm::inverse(view);
                perView.vScreenExtent = {WIDTH, HEIGHT};
                return perView;
            };

            // CameraA looks at the quad (at the origin); CameraB looks away from it.
            const PerViewData a = makePerView({0.0F, 0.0F, -3.0F}, {0.0F, 0.0F, 0.0F});
            const PerViewData b = makePerView({0.0F, 0.0F, 3.0F}, {0.0F, 0.0F, 6.0F});
            ctx.GetResource<BufferResource>("CameraA")->SetData(&a, sizeof(a));
            ctx.GetResource<BufferResource>("CameraB")->SetData(&b, sizeof(b));
        }};

    const auto makeRasterNode =
        [&quadMesh](const std::string& nodeName, const std::string& camera, const std::string& target)
    {
        return RenderGraphNodeCreateInfo{
            .nodeName = nodeName,
            .queueType = QueueType::GRAPHICS,
            .resourceUses = {ResourceUse{.handle = ResourceHandle("MeshVertexBuffer"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::VERTEX_BUFFER,
                                         .kind = ResourceKind::BUFFER},
                             ResourceUse{.handle = ResourceHandle("MeshIndexBuffer"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::INDEX_BUFFER,
                                         .kind = ResourceKind::BUFFER},
                             ResourceUse{.handle = ResourceHandle(camera),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::UNIFORM_BUFFER,
                                         .kind = ResourceKind::BUFFER,
                                         .descriptorBinding = RenderGraphSemanticBindings::PerView},
                             ResourceUse{.handle = ResourceHandle(target),
                                         .io = ResourceIOType::WRITE,
                                         .usage = ResourceUsage::COLOR_ATTACHMENT,
                                         .kind = ResourceKind::IMAGE}},
            .shaderNames = {"testWorldPos.vert.slang", "testWorldPos.frag.slang"},
            .psoDesc = {.rasterState = {.cullMode = CullMode::NONE},
                        .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
            .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
            .execute = [&quadMesh](RenderGraphNodeContext& ctx)
            {
                const auto& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
                VkDeviceSize offset = 0;
                VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
                vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                vkCmdBindIndexBuffer(ctx.commandBuffer, meshManager.m_pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(ctx.commandBuffer, quadMesh.m_nIndexCount, 1, quadMesh.m_nIndexOffset, 0, 0);
            }};
    };

    builder.AddNode(cameraPass);
    const RenderGraphNodeCreateInfo nodeA = makeRasterNode("NodeA", "CameraA", "TargetA");
    const RenderGraphNodeCreateInfo nodeB = makeRasterNode("NodeB", "CameraB", "TargetB");
    builder.AddNode(nodeA);
    builder.AddNode(nodeB);
    builder.AddDependency(cameraPass.nodeName, nodeA.nodeName);
    builder.AddDependency(cameraPass.nodeName, nodeB.nodeName);
    builder.Build();
    builder.Execute();

    auto* pTargetA = dynamic_cast<const ImageResource*>(builder.ResolveResource("TargetA"));
    auto* pTargetB = dynamic_cast<const ImageResource*>(builder.ResolveResource("TargetB"));
    REQUIRE(pTargetA != nullptr);
    REQUIRE(pTargetB != nullptr);

    // NodeA sees the quad; NodeB looks away. If the two nodes shared PER_VIEW, both would run with
    // the last-written camera and one of these assertions would fail.
    REQUIRE(CountNonBlackPixels(pTargetA) > 0);
    REQUIRE(CountNonBlackPixels(pTargetB) == 0);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: read-write attachment keeps earlier content",
                 "[RenderGraphBuilder]")
{
    // A compute node fills a storage image green; a graphics node then blends a 50%-alpha red quad
    // over it while declaring the image COLOR_ATTACHMENT/READ_WRITE. The result must still contain
    // the compute output, i.e. the attachment has to be LOADed. Clearing on write-order instead of
    // io (the old behaviour) would discard the green and make the test fail.
    GetMeshResourceManager()->PrepareSimpleMeshes();
    GetMeshResourceManager()->UploadMeshData();
    const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();

    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("Overlay",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("OverlayCamera", BufferResourceDesc{.count = 1,
                                                            .stride = sizeof(PerViewData),
                                                            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                            .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);

    RenderGraphNodeCreateInfo cameraPass = {
        .nodeName = "OverlayCameraPrep",
        .queueType = QueueType::CPU,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("OverlayCamera"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::UNIFORM_BUFFER,
                                     .kind = ResourceKind::BUFFER}},
        .execute = [](RenderGraphNodeContext& ctx)
        {
            const glm::mat4 proj = glm::perspective(
                glm::radians(60.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);
            const glm::mat4 view =
                glm::lookAt(glm::vec3(0.0F, 0.0F, -3.0F), glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
            PerViewData perView;
            perView.mProj = proj;
            perView.mView = view;
            perView.mProjInv = glm::inverse(proj);
            perView.mViewInv = glm::inverse(view);
            perView.vScreenExtent = {WIDTH, HEIGHT};
            ctx.GetResource<BufferResource>("OverlayCamera")->SetData(&perView, sizeof(perView));
        }};

    RenderGraphNodeCreateInfo fillPass = {
        .nodeName = "FillGreen",
        .queueType = QueueType::COMPUTE,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("Overlay"),
                                     .io = ResourceIOType::READ_WRITE,
                                     .usage = ResourceUsage::STORAGE_IMAGE,
                                     .kind = ResourceKind::IMAGE,
                                     .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
        .shaderNames = {"testFillColor.comp.slang"},
        .execute = [](RenderGraphNodeContext& ctx)
        { vkCmdDispatch(ctx.commandBuffer, (WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1); }};

    RenderGraphNodeCreateInfo overlayPass = {
        .nodeName = "RedOverlay",
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
                ResourceUse{.handle = ResourceHandle("OverlayCamera"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::UNIFORM_BUFFER,
                            .kind = ResourceKind::BUFFER,
                            .descriptorBinding = RenderGraphSemanticBindings::PerView},
                // Read-write: blend this pass over whatever is already in the attachment.
                ResourceUse{.handle = ResourceHandle("Overlay"),
                            .io = ResourceIOType::READ_WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"testWorldPos.vert.slang", "testSolidColor.frag.slang"},
        .psoDesc = {.rasterState = {.cullMode = CullMode::NONE},
                    .blendState = {.attachmentCount = 1,
                                   .attachments = {{{.blendEnable = true,
                                                     .srcColor = BlendFactor::SRC_ALPHA,
                                                     .dstColor = BlendFactor::ONE_MINUS_SRC_ALPHA}}}}},
        .execute = [&quadMesh](RenderGraphNodeContext& ctx)
        {
            const glm::vec4 color(1.0F, 0.0F, 0.0F, 0.5F);  // 50%-alpha red
            vkCmdPushConstants(ctx.commandBuffer, ctx.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(color),
                               &color);
            const auto& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, meshManager.m_pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, quadMesh.m_nIndexCount, 1, quadMesh.m_nIndexOffset, 0, 0);
        }};

    builder.AddNode(cameraPass);
    builder.AddNode(fillPass);
    builder.AddNode(overlayPass);
    builder.AddDependency(cameraPass.nodeName, overlayPass.nodeName);
    builder.AddDependency(fillPass.nodeName, overlayPass.nodeName);
    builder.Build();
    builder.Execute();

    auto* pTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("Overlay"));
    REQUIRE(pTarget != nullptr);

    // Green survives only if the attachment was loaded rather than cleared; the red overlay is
    // 50% alpha, so the blended green is ~0.5.
    REQUIRE(CountPixelsMatching(pTarget, {0.0F, 0.25F, 0.0F}) > 0);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: CPU node between GPU nodes runs exactly once",
                 "[RenderGraphBuilder]")
{
    // A CPU node with no ordering edges sorts between two GPU nodes (Kahn breaks ready-node ties by
    // name), so it lands inside a queue segment's index range unless the plan closes the segment at
    // it. A segment that spans a CPU node makes the recording loop treat it as GPU work: its
    // callback runs a second time (this counter) and RecordBarriers emits a barrier whose
    // destination access mask is VK_ACCESS_2_HOST_WRITE_BIT, which no GPU stage mask supports
    // (VUID-VkBufferMemoryBarrier2-dstAccessMask-03917).
    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource("Target",
                        ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                          .extent = {WIDTH, HEIGHT},
                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("CpuData", BufferResourceDesc{.count = 1,
                                                      .stride = 16,
                                                      .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                      .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});

    int nCpuRuns = 0;

    const auto storageUse = [](const char* handle)
    {
        return ResourceUse{.handle = ResourceHandle(handle),
                           .io = ResourceIOType::READ_WRITE,
                           .usage = ResourceUsage::STORAGE_IMAGE,
                           .kind = ResourceKind::IMAGE,
                           .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}};
    };

    RenderGraphNodeCreateInfo firstGpu = {.nodeName = "AGpu",
                                          .queueType = QueueType::COMPUTE,
                                          .resourceUses = {storageUse("Target")},
                                          .shaderNames = {"testFillColor.comp.slang"},
                                          .execute = [](RenderGraphNodeContext& ctx)
                                          { vkCmdDispatch(ctx.commandBuffer, (WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1); }};

    RenderGraphNodeCreateInfo cpuPass = {.nodeName = "BCpu",
                                         .queueType = QueueType::CPU,
                                         .resourceUses = {ResourceUse{.handle = ResourceHandle("CpuData"),
                                                                      .io = ResourceIOType::WRITE,
                                                                      .usage = ResourceUsage::UNIFORM_BUFFER,
                                                                      .kind = ResourceKind::BUFFER}},
                                         .execute = [&nCpuRuns](RenderGraphNodeContext&) { ++nCpuRuns; }};

    RenderGraphNodeCreateInfo secondGpu = {.nodeName = "CGpu",
                                           .queueType = QueueType::COMPUTE,
                                           .resourceUses = {storageUse("Target")},
                                           .shaderNames = {"testFillColor.comp.slang"},
                                           .execute = [](RenderGraphNodeContext& ctx)
                                           { vkCmdDispatch(ctx.commandBuffer, (WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1); }};

    builder.AddNode(firstGpu);
    builder.AddNode(cpuPass);
    builder.AddNode(secondGpu);
    // Force the name order AGpu < BCpu < CGpu so the CPU node really is a middle node.
    builder.AddDependency(firstGpu.nodeName, secondGpu.nodeName);
    builder.Build();
    builder.Execute();

    REQUIRE(nCpuRuns == 1);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: two writes to the same attachment both land",
                 "[RenderGraphBuilder]")
{
    // Two graphics nodes write the same color target with the *same* declared usage
    // (COLOR_ATTACHMENT/WRITE), so the access mask and layout are identical and only the WAW
    // hazard forces a barrier. The second pass blends over the first, so the result must contain
    // both contributions. (The memory-ordering half of this is checked by the validation layer;
    // pixels alone cannot distinguish a missing barrier on a single queue.)
    GetMeshResourceManager()->PrepareSimpleMeshes();
    GetMeshResourceManager()->UploadMeshData();
    const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();

    RenderGraphBuilder builder(GetRenderDevice());
    builder.AddResource(
        "WawTarget", ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                       .extent = {WIDTH, HEIGHT},
                                       .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    builder.AddResource("WawCamera", BufferResourceDesc{.count = 1,
                                                        .stride = sizeof(PerViewData),
                                                        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                        .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU});
    const auto& meshResources = GetMeshResourceManager()->GetMeshVertexResources();
    builder.ImportResource("MeshVertexBuffer", meshResources.m_pVertexBuffer);
    builder.ImportResource("MeshIndexBuffer", meshResources.m_pIndexBuffer);

    RenderGraphNodeCreateInfo cameraPass = {
        .nodeName = "WawCameraPrep",
        .queueType = QueueType::CPU,
        .resourceUses = {ResourceUse{.handle = ResourceHandle("WawCamera"),
                                     .io = ResourceIOType::WRITE,
                                     .usage = ResourceUsage::UNIFORM_BUFFER,
                                     .kind = ResourceKind::BUFFER}},
        .execute = [](RenderGraphNodeContext& ctx)
        {
            const glm::mat4 proj = glm::perspective(
                glm::radians(60.0F), static_cast<float>(WIDTH) / static_cast<float>(HEIGHT), 0.1F, 100.0F);
            const glm::mat4 view =
                glm::lookAt(glm::vec3(0.0F, 0.0F, -3.0F), glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
            PerViewData perView;
            perView.mProj = proj;
            perView.mView = view;
            perView.mProjInv = glm::inverse(proj);
            perView.mViewInv = glm::inverse(view);
            perView.vScreenExtent = {WIDTH, HEIGHT};
            ctx.GetResource<BufferResource>("WawCamera")->SetData(&perView, sizeof(perView));
        }};

    const auto makeWriteNode = [&quadMesh](const std::string& nodeName, const glm::vec4& color, bool bBlend)
    {
        return RenderGraphNodeCreateInfo{
            .nodeName = nodeName,
            .queueType = QueueType::GRAPHICS,
            .resourceUses = {ResourceUse{.handle = ResourceHandle("MeshVertexBuffer"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::VERTEX_BUFFER,
                                         .kind = ResourceKind::BUFFER},
                             ResourceUse{.handle = ResourceHandle("MeshIndexBuffer"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::INDEX_BUFFER,
                                         .kind = ResourceKind::BUFFER},
                             ResourceUse{.handle = ResourceHandle("WawCamera"),
                                         .io = ResourceIOType::READ,
                                         .usage = ResourceUsage::UNIFORM_BUFFER,
                                         .kind = ResourceKind::BUFFER,
                                         .descriptorBinding = RenderGraphSemanticBindings::PerView},
                             // Same declared usage in both nodes: only the WAW hazard differs.
                             ResourceUse{.handle = ResourceHandle("WawTarget"),
                                         .io = ResourceIOType::WRITE,
                                         .usage = ResourceUsage::COLOR_ATTACHMENT,
                                         .kind = ResourceKind::IMAGE}},
            .shaderNames = {"testWorldPos.vert.slang", "testSolidColor.frag.slang"},
            .psoDesc = {.rasterState = {.cullMode = CullMode::NONE},
                        .blendState = {.attachmentCount = 1,
                                       .attachments = {{{.blendEnable = bBlend,
                                                         .srcColor = BlendFactor::SRC_ALPHA,
                                                         .dstColor = BlendFactor::ONE_MINUS_SRC_ALPHA}}}}},
            .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
            .execute = [&quadMesh, color](RenderGraphNodeContext& ctx)
            {
                vkCmdPushConstants(ctx.commandBuffer, ctx.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(color), &color);
                const auto& meshManager = GetMeshResourceManager()->GetMeshVertexResources();
                VkDeviceSize offset = 0;
                VkBuffer vertexBuffer = meshManager.m_pVertexBuffer->buffer();
                vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                vkCmdBindIndexBuffer(ctx.commandBuffer, meshManager.m_pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(ctx.commandBuffer, quadMesh.m_nIndexCount, 1, quadMesh.m_nIndexOffset, 0, 0);
            }};
    };

    // First pass writes opaque red; second blends 50%-alpha green over it.
    const RenderGraphNodeCreateInfo first = makeWriteNode("FirstWrite", {1.0F, 0.0F, 0.0F, 1.0F}, false);
    const RenderGraphNodeCreateInfo second = makeWriteNode("SecondWrite", {0.0F, 1.0F, 0.0F, 0.5F}, true);
    builder.AddNode(cameraPass);
    builder.AddNode(first);
    builder.AddNode(second);
    builder.AddDependency(cameraPass.nodeName, first.nodeName);
    builder.AddDependency(first.nodeName, second.nodeName);
    builder.Build();
    builder.Execute();

    auto* pTarget = dynamic_cast<const ImageResource*>(builder.ResolveResource("WawTarget"));
    REQUIRE(pTarget != nullptr);
    // The blend keeps both: red * 0.5 + green * 0.5.
    REQUIRE(CountPixelsMatching(pTarget, {0.25F, 0.25F, 0.0F}) > 0);
}

}  // namespace Muyo::RenderGraph
