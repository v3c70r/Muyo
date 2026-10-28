// #17, slice A: a pipeline cache shared across builders.
//
// The builder's own cache is per-builder, so two builders compile the same declaration twice and a
// builder's teardown releases only its own entries. These cases cover the injected, caller-owned cache:
// both builders must borrow the same VkPipeline, destroying one must not invalidate the other, and a
// swapchain-style ReleaseCachedDeviceObjects() must leave the shared entries alone.

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <span>
#include <string>
#include <vector>

#include "GraphicsTestEnv.h"
#include "RenderGraph/PipelineCache.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderResourceManager.h"

using namespace Muyo;
using namespace Muyo::RenderGraph;
using Catch::Matchers::ContainsSubstring;

namespace
{
/// A compute node that writes one uint32 through the heap, with a specialization value that selects
/// the pipeline key. `specValue` is id 0; id 1 is the same as the per-builder test's `y` operand.
RenderGraphNodeCreateInfo MakeCacheNode(const std::string& name, uint32_t specValue)
{
    return RenderGraphNodeCreateInfo{.nodeName = name,
                                     .queueType = QueueType::COMPUTE,
                                     .resourceUses = {{.handle = ResourceHandle("SharedCacheOut"),
                                                       .io = ResourceIOType::WRITE,
                                                       .usage = ResourceUsage::STORAGE_BUFFER,
                                                       .kind = ResourceKind::BUFFER}},
                                     .shaderNames = {"testSpecConstant.comp.heap"},
                                     .specializationConstants = {{.id = 0, .value = specValue}, {.id = 1, .value = 3}},
                                     .execute = [](RenderGraphNodeContext& ctx)
                                     {
                                         const std::optional<uint32_t> output =
                                             ctx.GetHeapIndex("SharedCacheOut", ResourceUsage::STORAGE_BUFFER);
                                         REQUIRE(output.has_value());
                                         struct Push
                                         {
                                             uint32_t x;
                                             uint32_t y;
                                         };
                                         const Push push{*output, 0};
                                         ctx.PushData(0, std::as_bytes(std::span{&push, 1}));
                                         vkCmdDispatch(ctx.commandBuffer, 1, 1, 1);
                                     }};
}

/// @return The compiled node with `name`, or `VK_NULL_HANDLE` when it is absent.
/// @param builder Builder whose last Build produced the compiled graph.
/// @param name Node name to find.
VkPipeline FindPipeline(RenderGraphBuilder& builder, const std::string& name)
{
    for (const CompiledRenderGraphNode& node : builder.GetCompiledGraph().GetNodes())
    {
        if (node.name == name) return node.pipeline;
    }
    return VK_NULL_HANDLE;
}
}  // namespace

TEST_CASE("PipelineCache: a null device is rejected", "[PipelineCache]")
{
    // The cache creates and destroys pipelines through a device. A caller that supplies none gets a
    // named error at construction, not a null-handle VUID on the first entry.
    CHECK_THROWS_WITH(PipelineCache(nullptr), ContainsSubstring("renderDevice is null"));
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "RenderGraphBuilder: two builders share one cached pipeline",
                 "[RenderGraphBuilder][PipelineCache][DescriptorHeap]")
{
    // #17 acceptance: the same declaration from two builders returns the same VkPipeline, a different
    // declaration returns a different one, and destroying the first builder does not invalidate the
    // second's pipeline. The Execute() after the first builder's scope is the teeth: a pipeline the
    // first builder destroyed is an invalid-handle VUID, not a silent miss.
    VkRenderDevice* device = GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; the shared pipeline cache records this row as a gap");
    }

    BufferResource* pOut = GetRenderResourceManager()->AllocateBuffer("SharedCacheOut", 2 * sizeof(uint32_t),
                                                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                                      VMA_MEMORY_USAGE_CPU_TO_GPU);
    REQUIRE(pOut != nullptr);

    // Declared before the builders so it outlives both: the cache owns every borrowed pipeline.
    PipelineCache cache(device);
    CHECK(cache.GetDevice() == device->GetDevice());

    const RenderGraphServices services{
        .device = device, .resourceManager = GetRenderResourceManager(), .pipelineCache = &cache};
    const RenderGraphNodeCreateInfo node = MakeCacheNode("SharedCacheNode", 7);

    VkPipeline firstPipeline = VK_NULL_HANDLE;
    {
        RenderGraphBuilder firstBuilder(services);
        firstBuilder.ImportResource("SharedCacheOut", pOut);
        firstBuilder.AddNode(node);
        firstBuilder.Build();

        firstPipeline = FindPipeline(firstBuilder, "SharedCacheNode");
        REQUIRE(firstPipeline != VK_NULL_HANDLE);
        CHECK(firstBuilder.GetCompiledGraph().GetNodes().front().bOwnsPipeline == false);
        CHECK(cache.GetPipelineCount() == 1);
    }  // firstBuilder's destructor releases what it owns; the shared entry must survive.

    RenderGraphBuilder secondBuilder(services);
    secondBuilder.ImportResource("SharedCacheOut", pOut);
    secondBuilder.AddNode(node);
    secondBuilder.Build();
    CHECK(FindPipeline(secondBuilder, "SharedCacheNode") == firstPipeline);
    CHECK(cache.GetPipelineCount() == 1);

    // Still valid after the builder that first created it is gone. The debug callback aborts on an
    // invalid handle, so reaching the end of Execute() is the assertion that it was not destroyed.
    secondBuilder.Execute();

    // A swapchain recreation releases the builder's own device objects. The shared pipeline is not the
    // builder's, so it has to survive: another graph may still be recording with it.
    secondBuilder.ReleaseCachedDeviceObjects();
    secondBuilder.Execute();

    // A different specialization value is a different pipeline key, so it creates a second entry rather
    // than aliasing the first.
    secondBuilder.AddNode(MakeCacheNode("SharedCacheNode2", 9));
    secondBuilder.Build();
    const VkPipeline secondPipeline = FindPipeline(secondBuilder, "SharedCacheNode2");
    REQUIRE(secondPipeline != VK_NULL_HANDLE);
    CHECK(secondPipeline != firstPipeline);
    CHECK(FindPipeline(secondBuilder, "SharedCacheNode") == firstPipeline);
    CHECK(cache.GetPipelineCount() == 2);

    // Executing the rebuild proves both borrowed handles are alive at once.
    secondBuilder.Execute();
}
