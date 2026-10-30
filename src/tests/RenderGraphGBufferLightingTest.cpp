// Headless coverage for the #20 GBUFFER and direct OPAQUE_LIGHTING graph nodes.
//
// The app's node declarations live in src/app/helloVulkan.cpp; these tests declare the same shaders,
// resources and pipeline state, so they exercise the heap shaders end to end without a window. The
// shared push struct is src/app/ScenePushData.h, which static_asserts the shader's offsets.
//
// Two devices are relevant and the case says which it needs: the descriptor-heap path is the subject,
// so a device without it skips rather than passing vacuously. The tests run on any heap device (RADV
// here, llvmpipe where its Mesa exposes the extension).

#include <vulkan/vulkan.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "../app/ScenePushData.h"
#include "DescriptorHeapService.h"
#include "DrawCommandBuffer.h"
#include "GraphicsTestEnv.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderResourceManager.h"
#include "SharedStructures.h"
#include "UniformBuffer.h"

namespace Muyo
{
using namespace RenderGraph;
namespace
{
constexpr uint32_t kExtent = 128;
constexpr float kTolerance = 0.01F;

float HalfToFloat(uint16_t h)
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
    std::memcpy(&out, &f, 4);
    return out;
}

struct Rgba
{
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 0.0F;
};

/// Copy an RGBA16F target back to the host. The caller states the layout the graph left it in: a
/// colour target whose last use is a write stays COLOR_ATTACHMENT_OPTIMAL, one that a later node
/// sampled is SHADER_READ_ONLY_OPTIMAL.
std::vector<Rgba> ReadColor(const ImageResource* pImage, VkImageLayout currentLayout)
{
    const size_t pixelCount = static_cast<size_t>(kExtent) * kExtent;
    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, pixelCount * 8);
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.oldLayout = currentLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.image = pImage->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {kExtent, kExtent, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pImage->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer(),
                                   1, &region);
        });

    const auto* pPixels = static_cast<const uint16_t*>(readback.Map());
    std::vector<Rgba> pixels(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        pixels[i] = Rgba{HalfToFloat(pPixels[i * 4 + 0]), HalfToFloat(pPixels[i * 4 + 1]),
                         HalfToFloat(pPixels[i * 4 + 2]), HalfToFloat(pPixels[i * 4 + 3])};
    }
    readback.Unmap();
    return pixels;
}

/// Copy a D32 depth target back to the host.
std::vector<float> ReadDepth(const ImageResource* pImage)
{
    const size_t pixelCount = static_cast<size_t>(kExtent) * kExtent;
    BufferResource readback(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, pixelCount * 4);
    GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.image = pImage->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            region.imageExtent = {kExtent, kExtent, 1};
            vkCmdCopyImageToBuffer(cmdBuf, pImage->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer(),
                                   1, &region);
        });

    const auto* pDepth = static_cast<const float*>(readback.Map());
    std::vector<float> depths(pDepth, pDepth + pixelCount);
    readback.Unmap();
    return depths;
}

Rgba PixelAt(const std::vector<Rgba>& pixels, uint32_t x, uint32_t y)
{
    return pixels[static_cast<size_t>(y) * kExtent + x];
}

uint32_t CountNonBlack(const std::vector<Rgba>& pixels)
{
    return static_cast<uint32_t>(
        std::count_if(pixels.begin(), pixels.end(), [](const Rgba& p)
                      { return std::abs(p.r) > 1e-5F || std::abs(p.g) > 1e-5F || std::abs(p.b) > 1e-5F; }));
}

/// One graph, its heap and the scene data the two nodes consume. Building it once per case keeps the
/// resource setup out of the assertions; `Run()` executes and waits.
struct GBufferHarness
{
    static constexpr uint32_t kMaterialCount = 2;
    static constexpr uint32_t kTextureCount = 7;  // 0..5 material slots, 6 a spare albedo for the table mutation

    glm::mat4 view = glm::mat4(1.0F);
    glm::mat4 proj = glm::perspective(glm::radians(60.0F), 1.0F, 0.1F, 100.0F);

    DescriptorHeapService heap;
    DescriptorSlot samplerSlot{};
    std::unique_ptr<RenderGraphBuilder> builder;

    StorageBuffer<PerObjData>* pPerObj = nullptr;
    StorageBuffer<PBRMaterial>* pMaterials = nullptr;
    StorageBuffer<uint32_t>* pTextureTable = nullptr;
    StorageBuffer<LightData>* pLightData = nullptr;
    UniformBuffer<uint32_t>* pLightCount = nullptr;
    DrawCommandBuffer<VkDrawIndexedIndirectCommand>* pDrawCommands = nullptr;

    std::vector<uint32_t> textureTable;

    void Run() { builder->Execute(); }
};

std::unique_ptr<GBufferHarness> MakeHarness(bool bAddLighting)
{
    auto pHarness = std::make_unique<GBufferHarness>();
    GBufferHarness& harness = *pHarness;
    harness.heap.Initialize(GetRenderDevice());

    // One sampler for every material texture, matching the legacy SAMPLER_1_MIPS state.
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0F;
    harness.samplerSlot = harness.heap.AcquireSampler(samplerInfo);

    // Six material textures and a spare. Each is 1x1 so the sampled value is exact and the expected
    // GBuffer content is a number rather than a filtering question.
    uint8_t red[4] = {255, 0, 0, 255};
    uint8_t green[4] = {0, 255, 0, 255};
    uint8_t black[4] = {0, 0, 0, 255};
    uint8_t roughness[4] = {0, 128, 0, 255};  // G = 0.502, multiplied by the factor below
    uint8_t metalness[4] = {64, 0, 0, 255};   // R = 0.251
    uint8_t occlusion[4] = {204, 0, 0, 255};  // R = 0.8
    TextureResource* pTextures[GBufferHarness::kTextureCount] = {};
    pTextures[0] = GetRenderResourceManager()->GetTexture("GBufferTestAlbedo", red, 1, 1);
    pTextures[1] = GetRenderResourceManager()->GetTexture("GBufferTestNormal", black, 1, 1);
    pTextures[2] = GetRenderResourceManager()->GetTexture("GBufferTestMetalness", metalness, 1, 1);
    pTextures[3] = GetRenderResourceManager()->GetTexture("GBufferTestRoughness", roughness, 1, 1);
    pTextures[4] = GetRenderResourceManager()->GetTexture("GBufferTestAO", occlusion, 1, 1);
    pTextures[5] = GetRenderResourceManager()->GetTexture("GBufferTestEmissive", black, 1, 1);
    pTextures[6] = GetRenderResourceManager()->GetTexture("GBufferTestSpareAlbedo", green, 1, 1);

    harness.textureTable.resize(GBufferHarness::kTextureCount);
    for (uint32_t i = 0; i < GBufferHarness::kTextureCount; ++i)
    {
        harness.textureTable[i] =
            harness.heap
                .AcquireImage(pTextures[i]->GetImageViewCreateInfo(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                .index;
    }

    // Two materials with identical texture ids; only the base-colour factor differs, so the material
    // index is observable in the albedo target.
    std::vector<PBRMaterial> materials(GBufferHarness::kMaterialCount);
    for (uint32_t i = 0; i < GBufferHarness::kMaterialCount; ++i)
    {
        PBRMaterial& material = materials[i];
        material.vBaseColorFactors = glm::vec4(1.0F);
        material.fRoughness = 1.0F;
        material.fMetalness = 1.0F;
        for (uint32_t texture = 0; texture < TEX_COUNT; ++texture)
        {
            material.UVIndices[texture] = 0;
            material.textureIds[texture] = static_cast<uint32_t>(texture);
        }
    }
    materials[1].vBaseColorFactors = glm::vec4(0.0F, 1.0F, 0.0F, 1.0F);

    std::vector<PerObjData> perObjects(1);
    perObjects[0].mWorldMatrix = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.0F, -2.0F));
    perObjects[0].nSubmeshCount = 1;
    perObjects[0].vSubmeshDatas[0].nMaterialIndex = 0;

    harness.pPerObj = GetRenderResourceManager()->GetStorageBuffer("GBufferTestPerObj", perObjects);
    harness.pMaterials = GetRenderResourceManager()->GetStorageBuffer("GBufferTestMaterials", materials);
    harness.pTextureTable =
        GetRenderResourceManager()->GetStorageBuffer("GBufferTestTextureTable", harness.textureTable);

    // A screen-covering quad at z=-2, drawn as one indirect command. Two commands are reserved so the
    // separate-light case can grow the buffer without reallocating it under the graph.
    std::vector<Vertex> vertices = {
        {{-1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 0.0F}},
        {{1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F, 0.0F}},
        {{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 0.0F, 0.0F}},
        {{-1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F, 0.0F}},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
    auto* pVertices = GetRenderResourceManager()->GetVertexBuffer<Vertex>("GBufferTestVertices", vertices);
    auto* pIndices = GetRenderResourceManager()->GetIndexBuffer("GBufferTestIndices", indices);

    VkDrawIndexedIndirectCommand drawCommand{};
    drawCommand.indexCount = static_cast<uint32_t>(indices.size());
    drawCommand.instanceCount = 1;
    drawCommand.firstIndex = 0;
    drawCommand.vertexOffset = 0;
    drawCommand.firstInstance = PackSubmeshObjectIndex(0, 0);
    harness.pDrawCommands = GetRenderResourceManager()->GetDrawCommandBuffer(
        "GBufferTestDrawCommands", std::vector<VkDrawIndexedIndirectCommand>{drawCommand});

    std::vector<LightData> lights(2);
    harness.pLightData = GetRenderResourceManager()->GetStorageBuffer("GBufferTestLightData", lights);
    harness.pLightCount = GetRenderResourceManager()->GetUniformBuffer<uint32_t>("GBufferTestLightCount");
    harness.pLightCount->SetData(0u);

    harness.builder =
        std::make_unique<RenderGraphBuilder>(RenderGraphServices{.device = GetRenderDevice(),
                                                                 .resourceManager = GetRenderResourceManager(),
                                                                 .descriptorHeap = &harness.heap},
                                             1);
    RenderGraphBuilder& builder = *harness.builder;

    const auto addColorTarget = [&builder](const char* name)
    {
        builder.AddResource(ResourceHandle(name),
                            ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                              .extent = {kExtent, kExtent},
                                              .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    };
    addColorTarget("GBufferPositionAO");
    addColorTarget("GBufferAlbedoTransmittance");
    addColorTarget("GBufferNormalRoughness");
    addColorTarget("GbufferMetalnessTranslucency");
    builder.AddResource("GBufferDepth",
                        ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                          .extent = {kExtent, kExtent},
                                          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});
    if (bAddLighting) addColorTarget("Composite");

    builder.ImportResource("PerObjData", harness.pPerObj);
    builder.ImportResource("Materials", harness.pMaterials);
    builder.ImportResource("TextureTable", harness.pTextureTable);
    builder.ImportResource("DrawCommands", harness.pDrawCommands);
    builder.ImportResource("Vertices", pVertices);
    builder.ImportResource("Indices", pIndices);
    if (bAddLighting)
    {
        builder.ImportResource("LightCount", harness.pLightCount);
        builder.ImportResource("LightData", harness.pLightData);
    }

    builder.AddNode(RenderGraphNodeCreateInfo{
        .nodeName = "GBUFFER",
        .queueType = QueueType::GRAPHICS,
        .resourceUses =
            {
                ResourceUse{.handle = ResourceHandle("PerObjData"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("Materials"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("TextureTable"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::STORAGE_BUFFER,
                            .kind = ResourceKind::BUFFER},
                // INDIRECT_BUFFER, not DRAW_COMMAND_BUFFER: the legacy DrawCommandBuffer has no
                // SHADER_DEVICE_ADDRESS usage, and the draw never reads it through a shader. The
                // DRAW_COMMAND_BUFFER role would acquire a storage-buffer heap descriptor and call
                // vkGetBufferDeviceAddress on it (VUID-VkBufferDeviceAddressInfo-buffer-02601).
                ResourceUse{.handle = ResourceHandle("DrawCommands"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDIRECT_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("Vertices"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::VERTEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("Indices"),
                            .io = ResourceIOType::READ,
                            .usage = ResourceUsage::INDEX_BUFFER,
                            .kind = ResourceKind::BUFFER},
                ResourceUse{.handle = ResourceHandle("GBufferPositionAO"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
                ResourceUse{.handle = ResourceHandle("GBufferAlbedoTransmittance"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
                ResourceUse{.handle = ResourceHandle("GBufferNormalRoughness"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
                ResourceUse{.handle = ResourceHandle("GbufferMetalnessTranslucency"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::COLOR_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
                ResourceUse{.handle = ResourceHandle("GBufferDepth"),
                            .io = ResourceIOType::WRITE,
                            .usage = ResourceUsage::DEPTH_STENCIL_ATTACHMENT,
                            .kind = ResourceKind::IMAGE},
            },
        .shaderNames = {"GBuffer.vert.heap", "GBuffer.frag.heap"},
        .psoDesc = {.vertexType = VertexType::GENERIC,
                    .rasterState = {.cullMode = CullMode::NONE},
                    .depthStencilState = {.depthTestEnable = true, .depthWriteEnable = true},
                    // Four colours must be declared: a blend state with a different attachment count fails
                    // pipeline creation (VUID-VkGraphicsPipelineCreateInfo-renderPass-06055).
                    .blendState = {.attachmentCount = 4,
                                   .attachments = {{{.blendEnable = false},
                                                    {.blendEnable = false},
                                                    {.blendEnable = false},
                                                    {.blendEnable = false}}}}},
        .attachmentClearValues = {VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}},
                                  VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}},
                                  VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}},
                                  VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}}},
        .execute =
            [view = harness.view, proj = harness.proj, sampler = harness.samplerSlot.index](RenderGraphNodeContext& ctx)
        {
            const std::optional<uint32_t> perObj = ctx.GetHeapIndex("PerObjData", ResourceUsage::STORAGE_BUFFER);
            const std::optional<uint32_t> materials = ctx.GetHeapIndex("Materials", ResourceUsage::STORAGE_BUFFER);
            const std::optional<uint32_t> table = ctx.GetHeapIndex("TextureTable", ResourceUsage::STORAGE_BUFFER);
            if (!perObj.has_value() || !materials.has_value() || !table.has_value())
            {
                throw std::runtime_error("GBUFFER test: a buffer use has no heap index");
            }

            GBufferPush push{};
            push.mView = view;
            push.mProj = proj;
            push.perObjData = glm::uvec2(*perObj, 0);
            push.materialData = glm::uvec2(*materials, 0);
            push.textureTable = glm::uvec2(*table, 0);
            push.sampler = sampler;
            ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

            const auto* pVertexBuffer = ctx.GetResource<VertexBuffer<Vertex>>("Vertices");
            const auto* pIndexBuffer = ctx.GetResource<IndexBuffer>("Indices");
            const auto* pDrawBuffer = ctx.GetResource<DrawCommandBuffer<VkDrawIndexedIndirectCommand>>("DrawCommands");
            if (pVertexBuffer == nullptr || pIndexBuffer == nullptr || pDrawBuffer == nullptr)
            {
                throw std::runtime_error("GBUFFER test: the geometry resources did not resolve");
            }

            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = pVertexBuffer->buffer();
            vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(ctx.commandBuffer, pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(ctx.commandBuffer, pDrawBuffer->buffer(), 0, pDrawBuffer->GetDrawCommandCount(),
                                     pDrawBuffer->GetStride());
        }});

    if (bAddLighting)
    {
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = "LIGHTING",
            .queueType = QueueType::GRAPHICS,
            .resourceUses =
                {
                    ResourceUse{.handle = ResourceHandle("GBufferPositionAO"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::SAMPLED,
                                .kind = ResourceKind::IMAGE},
                    ResourceUse{.handle = ResourceHandle("GBufferAlbedoTransmittance"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::SAMPLED,
                                .kind = ResourceKind::IMAGE},
                    ResourceUse{.handle = ResourceHandle("GBufferNormalRoughness"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::SAMPLED,
                                .kind = ResourceKind::IMAGE},
                    ResourceUse{.handle = ResourceHandle("GbufferMetalnessTranslucency"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::SAMPLED,
                                .kind = ResourceKind::IMAGE},
                    ResourceUse{.handle = ResourceHandle("LightCount"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::UNIFORM_BUFFER,
                                .kind = ResourceKind::BUFFER},
                    ResourceUse{.handle = ResourceHandle("LightData"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::STORAGE_BUFFER,
                                .kind = ResourceKind::BUFFER},
                    ResourceUse{.handle = ResourceHandle("Vertices"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::VERTEX_BUFFER,
                                .kind = ResourceKind::BUFFER},
                    ResourceUse{.handle = ResourceHandle("Indices"),
                                .io = ResourceIOType::READ,
                                .usage = ResourceUsage::INDEX_BUFFER,
                                .kind = ResourceKind::BUFFER},
                    ResourceUse{.handle = ResourceHandle("Composite"),
                                .io = ResourceIOType::WRITE,
                                .usage = ResourceUsage::COLOR_ATTACHMENT,
                                .kind = ResourceKind::IMAGE},
                },
            .shaderNames = {"triangle.vert", "lighting.frag.heap"},
            .psoDesc = {.vertexType = VertexType::GENERIC,
                        .rasterState = {.cullMode = CullMode::NONE},
                        .depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false},
                        .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
            .execute = [view = harness.view, sampler = harness.samplerSlot.index,
                        indexCount = indices.size()](RenderGraphNodeContext& ctx)
            {
                const std::optional<uint32_t> positionAO =
                    ctx.GetHeapIndex("GBufferPositionAO", ResourceUsage::SAMPLED);
                const std::optional<uint32_t> albedo =
                    ctx.GetHeapIndex("GBufferAlbedoTransmittance", ResourceUsage::SAMPLED);
                const std::optional<uint32_t> normal =
                    ctx.GetHeapIndex("GBufferNormalRoughness", ResourceUsage::SAMPLED);
                const std::optional<uint32_t> metalness =
                    ctx.GetHeapIndex("GbufferMetalnessTranslucency", ResourceUsage::SAMPLED);
                const std::optional<uint32_t> lightCount =
                    ctx.GetHeapIndex("LightCount", ResourceUsage::UNIFORM_BUFFER);
                const std::optional<uint32_t> lightData = ctx.GetHeapIndex("LightData", ResourceUsage::STORAGE_BUFFER);
                if (!positionAO.has_value() || !albedo.has_value() || !normal.has_value() || !metalness.has_value() ||
                    !lightCount.has_value() || !lightData.has_value())
                {
                    throw std::runtime_error("LIGHTING test: a use has no heap index");
                }

                LightingPush push{};
                push.mView = view;
                push.positionAO = glm::uvec2(*positionAO, 0);
                push.albedoTransmittance = glm::uvec2(*albedo, 0);
                push.normalRoughness = glm::uvec2(*normal, 0);
                push.metalnessTranslucency = glm::uvec2(*metalness, 0);
                push.lightCount = glm::uvec2(*lightCount, 0);
                push.lightData = glm::uvec2(*lightData, 0);
                push.sampler = sampler;
                ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

                const auto* pVertexBuffer = ctx.GetResource<VertexBuffer<Vertex>>("Vertices");
                const auto* pIndexBuffer = ctx.GetResource<IndexBuffer>("Indices");
                if (pVertexBuffer == nullptr || pIndexBuffer == nullptr)
                {
                    throw std::runtime_error("LIGHTING test: the quad resources did not resolve");
                }
                VkDeviceSize offset = 0;
                VkBuffer vertexBuffer = pVertexBuffer->buffer();
                vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                vkCmdBindIndexBuffer(ctx.commandBuffer, pIndexBuffer->buffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(ctx.commandBuffer, static_cast<uint32_t>(indexCount), 1, 0, 0, 0);
            }});
        builder.AddDependency("GBUFFER", "LIGHTING");
    }

    builder.Build();
    return pHarness;
}

}  // namespace

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: GBUFFER node writes the four targets and depth",
                 "[RenderGraphBuilder][DescriptorHeap]")
{
    if (!GetRenderDevice()->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; the GBUFFER node is heap-only");
    }

    auto pHarness = MakeHarness(/*bAddLighting=*/false);
    pHarness->Run();

    const auto* pPosition = dynamic_cast<const ImageResource*>(pHarness->builder->ResolveResource("GBufferPositionAO"));
    const auto* pAlbedo =
        dynamic_cast<const ImageResource*>(pHarness->builder->ResolveResource("GBufferAlbedoTransmittance"));
    const auto* pNormal =
        dynamic_cast<const ImageResource*>(pHarness->builder->ResolveResource("GBufferNormalRoughness"));
    const auto* pMetalness =
        dynamic_cast<const ImageResource*>(pHarness->builder->ResolveResource("GbufferMetalnessTranslucency"));
    const auto* pDepth = dynamic_cast<const ImageResource*>(pHarness->builder->ResolveResource("GBufferDepth"));
    REQUIRE(pPosition != nullptr);
    REQUIRE(pAlbedo != nullptr);
    REQUIRE(pNormal != nullptr);
    REQUIRE(pMetalness != nullptr);
    REQUIRE(pDepth != nullptr);

    const uint32_t center = kExtent / 2;
    const Rgba position = PixelAt(ReadColor(pPosition, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    const Rgba albedo = PixelAt(ReadColor(pAlbedo, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    const Rgba normal = PixelAt(ReadColor(pNormal, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    const Rgba metalness = PixelAt(ReadColor(pMetalness, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);

    // World position: the quad sits at z=-2 with an identity world rotation. The centre pixel is near
    // x=y=0, so the stored position is (0, 0, -2).
    REQUIRE(std::abs(position.r) < 0.05F);
    REQUIRE(std::abs(position.g) < 0.05F);
    REQUIRE(std::abs(position.b + 2.0F) < 0.05F);
    // AO is the sampled red channel, 204/255.
    REQUIRE(std::abs(position.a - 204.0F / 255.0F) < kTolerance);

    // Albedo is the red texture times the white material factor; transmittance is 1.
    REQUIRE(std::abs(albedo.r - 1.0F) < kTolerance);
    REQUIRE(std::abs(albedo.g) < kTolerance);
    REQUIRE(std::abs(albedo.b) < kTolerance);
    REQUIRE(std::abs(albedo.a - 1.0F) < kTolerance);

    // Normal texture is black, so the stored normal is the geometric world normal (0, 0, 1).
    REQUIRE(std::abs(normal.r) < kTolerance);
    REQUIRE(std::abs(normal.g) < kTolerance);
    REQUIRE(std::abs(normal.b - 1.0F) < kTolerance);
    // Roughness: texture G 128/255 times factor 1.
    REQUIRE(std::abs(normal.a - 128.0F / 255.0F) < kTolerance);

    // Metalness: texture R 64/255 times factor 1.
    REQUIRE(std::abs(metalness.r - 64.0F / 255.0F) < kTolerance);
    REQUIRE(std::abs(metalness.g - 1.0F) < kTolerance);

    // Depth: the centre fragment's projected depth. The quad covers the centre, and the depth image
    // was cleared to 1.0, so a value near the projection's z/w proves the depth attachment was bound
    // and written rather than left cleared.
    const std::vector<float> depths = ReadDepth(pDepth);
    const float depth = depths[static_cast<size_t>(center) * kExtent + center];
    const glm::vec4 clip = pHarness->proj * glm::vec4(0.0F, 0.0F, -2.0F, 1.0F);
    const float expectedDepth = clip.z / clip.w;
    INFO("depth = " << depth << ", expected " << expectedDepth);
    REQUIRE(depth > 0.0F);
    REQUIRE(depth < 1.0F);
    REQUIRE(std::abs(depth - expectedDepth) < kTolerance);

    // Teeth: select material 1, whose base-colour factor is (0,1,0). The albedo target must lose its
    // red channel. Without the material-index read this stays red.
    {
        std::vector<PerObjData> mutated(1);
        mutated[0].mWorldMatrix = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.0F, -2.0F));
        mutated[0].nSubmeshCount = 1;
        mutated[0].vSubmeshDatas[0].nMaterialIndex = 1;
        pHarness->pPerObj->SetData(mutated.data(), sizeof(PerObjData));
    }
    pHarness->Run();
    const Rgba materialOne = PixelAt(ReadColor(pAlbedo, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    INFO("material 1 albedo = (" << materialOne.r << ", " << materialOne.g << ", " << materialOne.b << ")");
    REQUIRE(std::abs(materialOne.r) < kTolerance);
    REQUIRE(std::abs(materialOne.g) < kTolerance);
    REQUIRE(std::abs(materialOne.b) < kTolerance);

    // Teeth for the texture table: point material 0's albedo slot at the spare green texture. The
    // material buffer is unchanged, so only a table read can make this green.
    {
        std::vector<PerObjData> restored(1);
        restored[0].mWorldMatrix = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.0F, -2.0F));
        restored[0].nSubmeshCount = 1;
        restored[0].vSubmeshDatas[0].nMaterialIndex = 0;
        pHarness->pPerObj->SetData(restored.data(), sizeof(PerObjData));

        // The spare texture's heap index was table[6]; table[0] now names the same image.
        const uint32_t spare = pHarness->textureTable[GBufferHarness::kTextureCount - 1];
        pHarness->textureTable[0] = spare;
        pHarness->pTextureTable->SetData(pHarness->textureTable.data(),
                                         pHarness->textureTable.size() * sizeof(uint32_t));
    }
    pHarness->Run();
    const Rgba mutatedAlbedo = PixelAt(ReadColor(pAlbedo, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    INFO("mutated table albedo = (" << mutatedAlbedo.r << ", " << mutatedAlbedo.g << ", " << mutatedAlbedo.b << ")");
    REQUIRE(std::abs(mutatedAlbedo.r) < kTolerance);
    REQUIRE(std::abs(mutatedAlbedo.g - 1.0F) < kTolerance);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphBuilder: direct OPAQUE_LIGHTING is direct only",
                 "[RenderGraphBuilder][DescriptorHeap]")
{
    if (!GetRenderDevice()->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; the LIGHTING node is heap-only");
    }

    auto pHarness = MakeHarness(/*bAddLighting=*/true);
    const uint32_t center = kExtent / 2;

    // The GBuffer must have survived the node boundary and the sampled layout transition; otherwise a
    // black composite would say nothing about the lighting shader.
    const auto* pAlbedo =
        dynamic_cast<const ImageResource*>(pHarness->builder->ResolveResource("GBufferAlbedoTransmittance"));
    REQUIRE(pAlbedo != nullptr);
    pHarness->Run();
    const Rgba gbufferAlbedo = PixelAt(ReadColor(pAlbedo, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), center, center);
    INFO("GBuffer albedo through the graph = (" << gbufferAlbedo.r << ", " << gbufferAlbedo.g << ", " << gbufferAlbedo.b
                                                << ")");
    REQUIRE(std::abs(gbufferAlbedo.r - 1.0F) < kTolerance);

    // No lights. There is no IBL ambient and no RSM term, so the composite must be black everywhere.
    pHarness->pLightCount->SetData(0u);
    pHarness->Run();
    const auto* pComposite = dynamic_cast<const ImageResource*>(pHarness->builder->ResolveResource("Composite"));
    REQUIRE(pComposite != nullptr);
    const std::vector<Rgba> unlit = ReadColor(pComposite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    REQUIRE(CountNonBlack(unlit) == 0);

    // One directional light shining along -z, onto a +z normal, at the quad 2m in front of the camera.
    LightData light{};
    light.LightType = LIGHT_TYPE_DIRECTIONAL;
    light.vPosition = glm::vec3(0.0F);
    light.vDirection = glm::vec3(0.0F, 0.0F, -1.0F);
    light.fRange = 100.0F;
    light.vColor = glm::vec3(1.0F);
    light.fIntensity = 1.0F;
    pHarness->pLightData->SetData(&light, sizeof(LightData));
    pHarness->pLightCount->SetData(1u);
    pHarness->Run();
    const Rgba oneLight = PixelAt(ReadColor(pComposite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    INFO("one light = (" << oneLight.r << ", " << oneLight.g << ", " << oneLight.b << ")");
    REQUIRE(oneLight.r > 0.01F);

    // Two identical lights must accumulate: the direct term is linear and nothing else contributes.
    std::vector<LightData> twoLights = {light, light};
    pHarness->pLightData->SetData(twoLights.data(), twoLights.size() * sizeof(LightData));
    pHarness->pLightCount->SetData(2u);
    pHarness->Run();
    const Rgba secondLight = PixelAt(ReadColor(pComposite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    INFO("two lights = (" << secondLight.r << ", " << secondLight.g << ", " << secondLight.b << ")");
    REQUIRE(std::abs(secondLight.r - 2.0F * oneLight.r) < 0.02F * std::max(oneLight.r, 1.0F));
    REQUIRE(std::abs(secondLight.g - 2.0F * oneLight.g) < 0.02F * std::max(oneLight.g, 1.0F));

    // The same light reversed is behind the surface (NdotL = 0), so it contributes nothing.
    light.vDirection = glm::vec3(0.0F, 0.0F, 1.0F);
    pHarness->pLightData->SetData(&light, sizeof(LightData));
    pHarness->pLightCount->SetData(1u);
    pHarness->Run();
    const Rgba backLit = PixelAt(ReadColor(pComposite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    INFO("back-facing light = (" << backLit.r << ", " << backLit.g << ", " << backLit.b << ")");
    REQUIRE(std::abs(backLit.r) < 1e-5F);
    REQUIRE(std::abs(backLit.g) < 1e-5F);
    REQUIRE(std::abs(backLit.b) < 1e-5F);

    // A point light in front of the quad exercises the LightData fields the directional case does not:
    // position, range and intensity. It sits one metre in front, so the falloff is ~1 over a range of
    // 100 and NdotL is 1.
    LightData point{};
    point.LightType = LIGHT_TYPE_POINT;
    point.vPosition = glm::vec3(0.0F, 0.0F, -1.0F);
    point.vDirection = glm::vec3(0.0F);
    point.fRange = 100.0F;
    point.vColor = glm::vec3(1.0F);
    point.fIntensity = 1.0F;
    pHarness->pLightData->SetData(&point, sizeof(LightData));
    pHarness->pLightCount->SetData(1u);
    pHarness->Run();
    const Rgba pointLit = PixelAt(ReadColor(pComposite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    INFO("point light = (" << pointLit.r << ", " << pointLit.g << ", " << pointLit.b << ")");
    REQUIRE(pointLit.r > 0.01F);

    // Behind the surface: the point light contributes nothing (NdotL = 0), and the black result rules
    // out a position offset that still happens to point at the quad.
    point.vPosition = glm::vec3(0.0F, 0.0F, -3.0F);
    pHarness->pLightData->SetData(&point, sizeof(LightData));
    pHarness->Run();
    const Rgba behind = PixelAt(ReadColor(pComposite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), center, center);
    INFO("behind point light = (" << behind.r << ", " << behind.g << ", " << behind.b << ")");
    REQUIRE(std::abs(behind.r) < 1e-5F);
    REQUIRE(std::abs(behind.g) < 1e-5F);
    REQUIRE(std::abs(behind.b) < 1e-5F);
}

}  // namespace Muyo
