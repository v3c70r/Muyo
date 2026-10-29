// #20 increment 1: the FINAL node's heap shader samples a known imported image.
//
// The windowed app cannot settle this on its own: the point of the node is that an image produced outside
// the graph keeps its contents when the graph's first use transitions it. This test fills an imported image
// with two known colours, imports it with the layout it is in, samples it through the same shader the app
// uses, and checks that both halves of the output match. Two colours rather than one is deliberate: a
// fragment that never consumes the vertex texcoord would sample one constant location and pass a uniform
// test, so the left/right split is what proves the quad's texcoords reach the sample.
//
// The barrier's *source layout* is also asserted, through the trace. That is the half with teeth: on RADV
// and llvmpipe a transition from UNDEFINED happens to preserve the pixels, so removing the import's stated
// layout would leave the colour checks green while the contract was silently broken.

#include <vulkan/vulkan.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "DescriptorHeapService.h"
#include "GraphicsTestEnv.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderGraph/RenderGraphTrace.h"
#include "RenderResourceManager.h"
#include "VertexBuffer.h"

using namespace Muyo;
using namespace Muyo::RenderGraph;

namespace
{
/// Copies `pTarget` (left in COLOR_ATTACHMENT_OPTIMAL) back and returns two pixels: one from each half.
std::array<std::array<uint8_t, 4>, 2> ReadLeftAndRightPixels(const ImageResource* pTarget, VkExtent2D extent)
{
    const size_t bufferSize = static_cast<size_t>(extent.width) * extent.height * 4;
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
            VkDependencyInfo dependencyInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pTarget->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer(),
                                   1, &region);
        });

    const auto* pPixels = static_cast<const uint8_t*>(readback.Map());
    const auto pixelAt = [&](uint32_t x, uint32_t y)
    {
        const size_t index = (static_cast<size_t>(y) * extent.width + x) * 4;
        return std::array<uint8_t, 4>{pPixels[index + 0], pPixels[index + 1], pPixels[index + 2], pPixels[index + 3]};
    };
    const std::array<std::array<uint8_t, 4>, 2> result = {pixelAt(extent.width / 8, extent.height / 2),
                                                          pixelAt(7 * extent.width / 8, extent.height / 2)};
    readback.Unmap();
    return result;
}
}  // namespace

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: FINAL samples an imported image and preserves its content",
                 "[RenderGraphBuilder][DescriptorHeap][FinalNode]")
{
    if (!GetRenderDevice()->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; the heap shader cannot run here");
    }

    constexpr VkExtent2D extent{64, 64};
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    // The source is filled outside the graph, exactly like the legacy scene's composite: content the graph
    // did not produce, in a layout the caller states. TRANSFER_SRC is for the output's readback.
    RenderTarget* pSource = GetRenderResourceManager()->GetColorTarget("FinalNodeSource", extent, format, 1, 1,
                                                                       VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    RenderTarget* pOutput = GetRenderResourceManager()->GetColorTarget("FinalNodeOutput", extent, format, 1, 1,
                                                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    REQUIRE(pSource != nullptr);
    REQUIRE(pOutput != nullptr);

    constexpr uint8_t kLeftRed = 32;
    constexpr uint8_t kRightGreen = 224;
    std::vector<uint8_t> sourcePixels(static_cast<size_t>(extent.width) * extent.height * 4);
    for (uint32_t y = 0; y < extent.height; ++y)
    {
        for (uint32_t x = 0; x < extent.width; ++x)
        {
            const size_t index = (static_cast<size_t>(y) * extent.width + x) * 4;
            const bool bLeft = x < extent.width / 2;
            sourcePixels[index + 0] = bLeft ? kLeftRed : 0;
            sourcePixels[index + 1] = bLeft ? 0 : kRightGreen;
            sourcePixels[index + 2] = 0;
            sourcePixels[index + 3] = 255;
        }
    }
    BufferResource sourceUpload(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sourcePixels.size());
    sourceUpload.SetData(sourcePixels.data(), sourcePixels.size());

    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkImageMemoryBarrier2 toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toDst.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            toDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            toDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toDst.image = pSource->getImage();
            toDst.subresourceRange = range;
            VkDependencyInfo toDstDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            toDstDep.imageMemoryBarrierCount = 1;
            toDstDep.pImageMemoryBarriers = &toDst;
            vkCmdPipelineBarrier2(cmdBuf, &toDstDep);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyBufferToImage(cmdBuf, sourceUpload.buffer(), pSource->getImage(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // Leave it where the app's UI pass leaves the composite, so the import can state that layout.
            VkImageMemoryBarrier2 toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            toRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            toRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.image = pSource->getImage();
            toRead.subresourceRange = range;
            VkDependencyInfo toReadDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            toReadDep.imageMemoryBarrierCount = 1;
            toReadDep.pImageMemoryBarriers = &toRead;
            vkCmdPipelineBarrier2(cmdBuf, &toReadDep);
        });

    // A quad with the same vertex layout triangle.vert expects; its texcoords cover the source.
    const std::vector<Vertex> quadVertices = {
        {{-1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 0.0F}},
        {{1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F, 0.0F}},
        {{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 0.0F}},
        {{-1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 1.0F, 0.0F}},
    };
    const std::vector<uint32_t> quadIndices = {0, 1, 2, 2, 3, 0};
    auto* pQuadVertices = GetRenderResourceManager()->GetVertexBuffer<Vertex>("FinalNodeQuadVertices", quadVertices);
    auto* pQuadIndices = GetRenderResourceManager()->GetIndexBuffer<uint32_t>("FinalNodeQuadIndices", quadIndices);

    DescriptorHeapService heap;
    heap.Initialize(GetRenderDevice(), DescriptorHeapService::Config{.inFlightCount = 1});
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0F;
    const DescriptorSlot samplerSlot = heap.AcquireSampler(samplerInfo);

    RenderGraphBuilder builder(
        RenderGraphServices{
            .device = GetRenderDevice(), .resourceManager = GetRenderResourceManager(), .descriptorHeap = &heap},
        1);
    // A layout describes an image; stating one for a buffer is a caller error, reported by name.
    CHECK_THROWS_AS(builder.ImportResource("QuadVertices", pQuadVertices, VK_IMAGE_LAYOUT_GENERAL), std::runtime_error);
    // The statement under test: the source already holds content in SHADER_READ_ONLY_OPTIMAL.
    builder.ImportResource("Source", pSource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    builder.ImportResource("QuadVertices", pQuadVertices);
    builder.ImportResource("QuadIndices", pQuadIndices);
    builder.ImportResource("Output", pOutput);
    // The trace is the only record of the barrier's *source layout*, which is what the feature changes.
    std::optional<RenderGraphTrace> captured;
    builder.SetTraceSink([&captured](RenderGraphTrace trace) { captured = std::move(trace); });
    builder.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "Final",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("Source"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::SAMPLED,
                            .kind = ResourceKind::IMAGE},
                ResourceUse{.handle = ResourceHandle("QuadVertices"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("QuadIndices"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("Output"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"triangle.vert", "final.frag.heap"},
        .psoDesc = {.depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false},
                    .blendState = {.attachmentCount = 1}},
        .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
        .execute = [samplerSlot, indexCount = static_cast<uint32_t>(quadIndices.size())](RenderGraphNodeContext& ctx)
        {
            struct FinalPush
            {
                uint32_t image;
                uint32_t sampler;
            };
            const std::optional<uint32_t> image = ctx.GetHeapIndex("Source", ResourceUsage::SAMPLED);
            REQUIRE(image.has_value());
            const FinalPush push{*image, samplerSlot.index};
            ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

            const auto* pVertices = dynamic_cast<const BufferResource*>(ctx.resolveResource("QuadVertices"));
            const auto* pIndices = dynamic_cast<const BufferResource*>(ctx.resolveResource("QuadIndices"));
            REQUIRE(pVertices != nullptr);
            REQUIRE(pIndices != nullptr);
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVertices->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIndices->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.commandBuffer, indexCount, 1, 0, 0, 0);
        }});
    builder.Build();
    builder.Execute();

    REQUIRE(captured.has_value());
    const TraceBarrier* pSourceBarrier = nullptr;
    for (const TraceSegment& segment : captured->segments)
    {
        for (const TraceBarrier& barrier : segment.barriers)
        {
            if (barrier.resource == "Source" && barrier.kind == TraceBarrierKind::Image) pSourceBarrier = &barrier;
        }
    }
    REQUIRE(pSourceBarrier != nullptr);
    CHECK(pSourceBarrier->oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(pSourceBarrier->newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const std::array<std::array<uint8_t, 4>, 2> pixels = ReadLeftAndRightPixels(pOutput, extent);
    // R8G8B8A8_UNORM is the output format; the margins absorb linear-filter weighting near the seam, which
    // these sample points are far from.
    CHECK(std::abs(static_cast<int>(pixels[0][0]) - kLeftRed) <= 2);
    CHECK(pixels[0][1] <= 2);
    CHECK(pixels[1][0] <= 2);
    CHECK(std::abs(static_cast<int>(pixels[1][1]) - kRightGreen) <= 2);
    CHECK(pixels[0][3] == 255);
    CHECK(pixels[1][3] == 255);
}
