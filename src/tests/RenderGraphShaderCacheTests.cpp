// #17, slice B: a shader module and reflection cache shared across builders.
//
// Each builder used to load the `.spv`, create a module and run reflection for itself, and the module
// lived only as long as that builder. These cases prove an injected cache creates the module once and
// that a second builder resolves the same module/reflection after the first builder is gone.

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <span>
#include <string>

#include "GraphicsTestEnv.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/ShaderCache.h"
#include "RenderResourceManager.h"

using namespace Muyo;
using namespace Muyo::RenderGraph;
using Catch::Matchers::ContainsSubstring;

namespace
{
/// A compute node that writes one uint32 through the heap, declaring the shader the cache should reuse.
RenderGraphNodeCreateInfo MakeShaderCacheNode(const std::string& name)
{
    return RenderGraphNodeCreateInfo{.nodeName = name,
                                     .queueType = QueueType::COMPUTE,
                                     .resourceUses = {{.handle = ResourceHandle("ShaderCacheOut"),
                                                       .io = ResourceIOType::WRITE,
                                                       .usage = ResourceUsage::STORAGE_BUFFER,
                                                       .kind = ResourceKind::BUFFER}},
                                     .shaderNames = {"testSpecConstant.comp.heap"},
                                     .specializationConstants = {{.id = 0, .value = 7}, {.id = 1, .value = 3}},
                                     .execute = [](RenderGraphNodeContext& ctx)
                                     {
                                         const std::optional<uint32_t> output =
                                             ctx.GetHeapIndex("ShaderCacheOut", ResourceUsage::STORAGE_BUFFER);
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
}  // namespace

TEST_CASE("ShaderCache: a null device is rejected", "[ShaderCache]")
{
    // Modules are created and destroyed through a device; a caller that supplies none gets a named
    // error rather than a null-handle VUID on the first load.
    CHECK_THROWS_WITH(ShaderCache(VK_NULL_HANDLE), ContainsSubstring("device is VK_NULL_HANDLE"));
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "RenderGraphBuilder: two builders reuse one shader module",
                 "[RenderGraphBuilder][ShaderCache][DescriptorHeap]")
{
    VkRenderDevice* device = GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; the shader cache cases record this row as a gap");
    }

    BufferResource* pOut = GetRenderResourceManager()->AllocateBuffer("ShaderCacheOut", 2 * sizeof(uint32_t),
                                                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                                      VMA_MEMORY_USAGE_CPU_TO_GPU);
    REQUIRE(pOut != nullptr);

    // Declared before the builders so it outlives both: modules belong to the cache, not to a builder.
    ShaderCache cache(device->GetDevice());
    CHECK(cache.GetDevice() == device->GetDevice());
    CHECK(cache.GetModuleCount() == 0);
    CHECK(cache.GetModuleCreateCount() == 0);

    // The cache's own contract first: the same name and content is one module and one reflection.
    const std::optional<ShaderAssetKey> firstKey = cache.LoadShader("testSpecConstant.comp.heap");
    REQUIRE(firstKey.has_value());
    const ShaderAsset* firstAsset = cache.GetShaderAsset(*firstKey);
    REQUIRE(firstAsset != nullptr);
    const std::optional<ShaderAssetKey> secondKey = cache.LoadShader("testSpecConstant.comp.heap");
    REQUIRE(secondKey.has_value());
    CHECK(*secondKey == *firstKey);
    CHECK(cache.GetShaderAsset(*secondKey) == firstAsset);
    CHECK(cache.GetModuleCount() == 1);
    CHECK(cache.GetModuleCreateCount() == 1);

    // A different shader is a different key and a second module, so a hit is not name-insensitive.
    const std::optional<ShaderAssetKey> otherKey = cache.LoadShader("copyBuffer.comp.heap");
    REQUIRE(otherKey.has_value());
    CHECK(*otherKey != *firstKey);
    CHECK(cache.GetModuleCount() == 2);
    CHECK(cache.GetModuleCreateCount() == 2);

    const RenderGraphServices services{
        .device = device, .resourceManager = GetRenderResourceManager(), .shaderCache = &cache};
    RenderGraphNodeCreateInfo node = MakeShaderCacheNode("ShaderCacheNode");

    VkPipeline firstPipeline = VK_NULL_HANDLE;
    {
        RenderGraphBuilder firstBuilder(services);
        firstBuilder.ImportResource("ShaderCacheOut", pOut);
        firstBuilder.AddNode(node);
        CHECK(cache.GetModuleCount() == 2);        // the builder's shader was already cached
        CHECK(cache.GetModuleCreateCount() == 2);  // and therefore created no module
        firstBuilder.Build();
        firstPipeline = firstBuilder.GetCompiledGraph().GetNodes().front().pipeline;
        REQUIRE(firstPipeline != VK_NULL_HANDLE);
        firstBuilder.Execute();
    }  // firstBuilder destroyed: the modules it did not own must survive.

    RenderGraphBuilder secondBuilder(services);
    secondBuilder.ImportResource("ShaderCacheOut", pOut);
    secondBuilder.AddNode(node);
    CHECK(cache.GetModuleCount() == 2);
    CHECK(cache.GetModuleCreateCount() == 2);
    secondBuilder.Build();
    REQUIRE(secondBuilder.GetCompiledGraph().GetNodes().front().pipeline != VK_NULL_HANDLE);

    // The module cache is still alive after the first builder; resolving the key in the second Build and
    // recording a dispatch with the resulting pipeline is what proves it was not destroyed with it.
    secondBuilder.Execute();
}
