// B1b-1: a graph node compiled for the descriptor-heap path.
//
// This is the first graph integration slice: a compute node with a null pipeline layout and
// VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT, receiving two heap indices through
// RenderGraphNodeContext::PushData. It deliberately leaves the legacy descriptor path in place for the
// nodes that have not been converted yet; the remaining shader/test conversion is B1b-2.

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>
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

    RenderGraphServices services{.device = device,
                                 .resourceManager = GetRenderResourceManager(),
                                 .descriptorManager = GetDescriptorManager(),
                                 .descriptorHeap = &heap};
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
                                   .useDescriptorHeap = true,
                                   .execute = [&heap](RenderGraphNodeContext& context)
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
                                       heap.Upload(context.commandBuffer);
                                       context.PushData(0, std::as_bytes(std::span{&push, 1}));
                                       vkCmdDispatch(context.commandBuffer, 1, 1, 1);
                                   }};

    builder.AddNode(node);
    builder.Build();
    builder.Execute();

    CHECK(builder.GetDescriptorSetCount() == 0);
    auto* pMapped = static_cast<const uint32_t*>(pOutput->Map());
    REQUIRE(pMapped != nullptr);
    CHECK(*pMapped == 42);
    pOutput->Unmap();
}
