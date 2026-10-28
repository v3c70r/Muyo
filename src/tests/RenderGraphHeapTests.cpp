// B1b-1: a graph node compiled for the descriptor-heap path.
//
// This is the first graph integration slice: a compute node with a null pipeline layout and
// VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT, receiving two heap indices through
// RenderGraphNodeContext::PushData. It deliberately leaves the legacy descriptor path in place for the
// nodes that have not been converted yet; the remaining shader/test conversion is B1b-2.

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <span>

#include "DescriptorHeapService.h"
#include "GraphicsTestEnv.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderResourceManager.h"

using namespace Muyo;
using namespace Muyo::RenderGraph;

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "RenderGraphBuilder: a compute node uses the descriptor heap with no layout",
                 "[RenderGraphBuilder][DescriptorHeap]")
{
    VkRenderDevice* device = GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B1b records the row as a gap, not a pass");
    }

    DescriptorHeapService heap;
    heap.Initialize(device);

    BufferResource* pInput = GetRenderResourceManager()->AllocateBuffer(
        "GraphHeapInput", sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    BufferResource* pOutput = GetRenderResourceManager()->AllocateBuffer(
        "GraphHeapOutput", sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    REQUIRE(pInput != nullptr);
    REQUIRE(pOutput != nullptr);
    const uint32_t inputValue = 41;
    pInput->SetData(&inputValue, sizeof(inputValue));
    const uint32_t outputValue = 0;
    pOutput->SetData(&outputValue, sizeof(outputValue));

    RenderGraphServices services{
        .device = device, .resourceManager = GetRenderResourceManager(), .descriptorHeap = &heap};
    RenderGraphBuilder builder(services);
    builder.ImportResource("GraphHeapInput", pInput);
    builder.ImportResource("GraphHeapOutput", pOutput);

    RenderGraphNodeCreateInfo node{.nodeName = "HeapCopy",
                                   .queueType = QueueType::COMPUTE,
                                   .resourceUses =
                                       {
                                           {.handle = ResourceHandle("GraphHeapInput"),
                                            .io = ResourceIOType::READ,
                                            .usage = ResourceUsage::STORAGE_BUFFER,
                                            .kind = ResourceKind::BUFFER},
                                           {.handle = ResourceHandle("GraphHeapOutput"),
                                            .io = ResourceIOType::WRITE,
                                            .usage = ResourceUsage::STORAGE_BUFFER,
                                            .kind = ResourceKind::BUFFER},
                                       },
                                   .shaderNames = {"graphHeapCopy.comp.heap"},
                                   .execute = [](RenderGraphNodeContext& context)
                                   {
                                       struct Push
                                       {
                                           uint32_t inputIndex;
                                           uint32_t outputIndex;
                                       };
                                       const std::optional<uint32_t> inputIndex =
                                           context.GetHeapIndex("GraphHeapInput", ResourceUsage::STORAGE_BUFFER);
                                       const std::optional<uint32_t> outputIndex =
                                           context.GetHeapIndex("GraphHeapOutput", ResourceUsage::STORAGE_BUFFER);
                                       REQUIRE(inputIndex.has_value());
                                       REQUIRE(outputIndex.has_value());
                                       const Push push{*inputIndex, *outputIndex};
                                       RenderGraphRootSlice root = context.AllocateRoot(16, 8);
                                       REQUIRE(root.host != nullptr);
                                       CHECK(root.deviceAddress % 8 == 0);
                                       context.WriteRootIndex(root, 0, *inputIndex);
                                       context.FlushRoot();
                                       context.PushData(0, std::as_bytes(std::span{&push, 1}));
                                       vkCmdDispatch(context.commandBuffer, 1, 1, 1);
                                   }};

    builder.AddNode(node);
    builder.Build();
    builder.Execute();
    auto* pMapped = static_cast<const uint32_t*>(pOutput->Map());
    REQUIRE(pMapped != nullptr);
    CHECK(*pMapped == 42);
    pOutput->Unmap();

    // A heap node whose shader declares a push block but whose callback never pushes it is a named
    // record-time error, not an undefined read at dispatch (#226).
    RenderGraphBuilder badBuilder(services);
    badBuilder.ImportResource("GraphHeapInput", pInput);
    badBuilder.ImportResource("GraphHeapOutput", pOutput);
    RenderGraphNodeCreateInfo badNode = node;
    badNode.nodeName = "HeapCopyNoPush";
    badNode.execute = [](RenderGraphNodeContext&) {};
    badBuilder.AddNode(badNode);
    badBuilder.Build();
    bool bNamedCoverageError = false;
    try
    {
        badBuilder.Execute();
    }
    catch (const std::runtime_error& error)
    {
        bNamedCoverageError = std::string(error.what()).find("not covered by PushData") != std::string::npos;
    }
    CHECK(bNamedCoverageError);
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "RenderGraphBuilder: repeated builds reuse descriptor slots",
                 "[RenderGraphBuilder][DescriptorHeap]")
{
    // B2b-2. Build() acquires a static heap slot per resource use, and before the content cache every
    // build took two new slots. The buffer region has 256 static slots, so an unchanged graph exhausted
    // it after 128 builds - which is the per-frame Build case B2 exists to make cheap. The cache keys on
    // the descriptor's content, so 140 builds of the same declaration must stay at two slots.
    VkRenderDevice* device = GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B2b-2 records the row as a gap, not a pass");
    }

    DescriptorHeapService heap;
    heap.Initialize(device);

    BufferResource* pInput = GetRenderResourceManager()->AllocateBuffer(
        "BuildCacheInput", sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    BufferResource* pOutput = GetRenderResourceManager()->AllocateBuffer(
        "BuildCacheOutput", sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    REQUIRE(pInput != nullptr);
    REQUIRE(pOutput != nullptr);

    RenderGraphServices services{
        .device = device, .resourceManager = GetRenderResourceManager(), .descriptorHeap = &heap};
    RenderGraphBuilder builder(services);
    builder.ImportResource("BuildCacheInput", pInput);
    builder.ImportResource("BuildCacheOutput", pOutput);
    builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "BuildCacheCopy",
                                              .queueType = QueueType::COMPUTE,
                                              .resourceUses = {{.handle = ResourceHandle("BuildCacheInput"),
                                                                .io = ResourceIOType::READ,
                                                                .usage = ResourceUsage::STORAGE_BUFFER,
                                                                .kind = ResourceKind::BUFFER},
                                                               {.handle = ResourceHandle("BuildCacheOutput"),
                                                                .io = ResourceIOType::WRITE,
                                                                .usage = ResourceUsage::STORAGE_BUFFER,
                                                                .kind = ResourceKind::BUFFER}},
                                              .shaderNames = {"copyBuffer.comp.heap"},
                                              .execute = [](RenderGraphNodeContext&) {}});

    int nBuilt = 0;
    std::string failure;
    for (; nBuilt < 140; ++nBuilt)
    {
        try
        {
            builder.Build();
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            break;
        }
    }
    INFO("build " << nBuilt << " failed: " << failure);
    CHECK(nBuilt == 140);
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "RenderGraphBuilder: a recreated resource releases its descriptor slots",
                 "[RenderGraphBuilder][DescriptorHeap]")
{
    // The other half of the cache: a graph-owned resource whose declaration changes is recreated by
    // B2a's path, and the slot that named the old resource must be released with it. Without the
    // release, a resize per frame leaks one buffer slot per frame and exhausts the 256-slot region.
    VkRenderDevice* device = GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B2b-2 records the row as a gap, not a pass");
    }

    RenderGraphBuilder builder(device);
    const auto declare = [&builder](uint64_t count)
    {
        builder.AddResource("RecreatedBuffer", BufferResourceDesc{.count = count,
                                                                  .stride = sizeof(uint32_t),
                                                                  .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                                  .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                                  .lifetime = ResourceLifetime::Persistent});
    };
    declare(4);
    builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "RecreatedCopy",
                                              .queueType = QueueType::COMPUTE,
                                              .resourceUses = {{.handle = ResourceHandle("RecreatedBuffer"),
                                                                .io = ResourceIOType::READ,
                                                                .usage = ResourceUsage::STORAGE_BUFFER,
                                                                .kind = ResourceKind::BUFFER}},
                                              .shaderNames = {"copyBuffer.comp.heap"},
                                              .execute = [](RenderGraphNodeContext&) {}});

    int nBuilt = 0;
    std::string failure;
    for (; nBuilt < 300; ++nBuilt)
    {
        declare(4 + nBuilt);
        try
        {
            builder.Build();
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            break;
        }
    }
    INFO("build " << nBuilt << " failed: " << failure);
    CHECK(nBuilt == 300);
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "RenderGraphBuilder: repeated builds reuse image descriptor slots",
                 "[RenderGraphBuilder][DescriptorHeap]")
{
    // The image half of the content key: view handle, format, extent/view type, subresources and layout.
    // The image region has 256 static slots, so 300 builds of one unchanged storage image would exhaust
    // it without the cache.
    VkRenderDevice* device = GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B2b-2 records the row as a gap, not a pass");
    }

    RenderGraphBuilder builder(device);
    builder.AddResource("BuildCacheImage",
                        ImageResourceDesc{.format = VK_FORMAT_R8G8B8A8_UNORM,
                                          .extent = {16, 16},
                                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                          .lifetime = ResourceLifetime::Persistent});
    builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "BuildCacheFill",
                                              .queueType = QueueType::COMPUTE,
                                              .resourceUses = {{.handle = ResourceHandle("BuildCacheImage"),
                                                                .io = ResourceIOType::READ_WRITE,
                                                                .usage = ResourceUsage::STORAGE_IMAGE,
                                                                .kind = ResourceKind::IMAGE}},
                                              .shaderNames = {"testFillColor.comp.heap"},
                                              .execute = [](RenderGraphNodeContext&) {}});

    int nBuilt = 0;
    std::string failure;
    for (; nBuilt < 300; ++nBuilt)
    {
        try
        {
            builder.Build();
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            break;
        }
    }
    INFO("build " << nBuilt << " failed: " << failure);
    CHECK(nBuilt == 300);
}

TEST_CASE_METHOD(GraphicsTestEnv, "RenderGraphNodeContext::PushData names its throws",
                 "[RenderGraphBuilder][DescriptorHeap]")
{
    RenderGraphNodeContext context{.queueType = QueueType::COMPUTE, .resourceManager = *GetRenderResourceManager()};
    const uint32_t words[2]{0, 0};
    CHECK_THROWS_WITH(context.PushData(0, std::as_bytes(std::span{words})),
                      "RenderGraphNodeContext::PushData: the node is not a descriptor-heap node");

    DescriptorHeapService heap;
    context.descriptorHeap = &heap;
    context.maxPushDataSize = 8;
    CHECK_THROWS_WITH(context.PushData(8, std::as_bytes(std::span{words})),
                      "RenderGraphNodeContext::PushData: offset + size exceeds maxPushDataSize");
}
