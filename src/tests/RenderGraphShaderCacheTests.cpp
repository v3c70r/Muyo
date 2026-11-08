// #17, slice B: a shader module and reflection cache shared across builders.
//
// Each builder used to load the `.spv`, create a module and run reflection for itself, and the module
// lived only as long as that builder. These cases prove an injected cache creates the module once and
// that a second builder resolves the same module/reflection after the first builder is gone.
//
// #292: the words no longer have to come from `shaders/<name>.spv`. The cases below prove a provider can
// supply them from memory (the teeth case: that name has no `.spv` on disk), that a non-default search
// path is used, and that the default file path still loads what every in-tree consumer expects.

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "GraphicsTestEnv.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/ShaderCache.h"
#include "RenderGraph/ShaderSourceProvider.h"
#include "RenderResourceManager.h"
#include "ShaderReflectionFetcher.h"

using namespace Muyo;
using namespace Muyo::RenderGraph;
using Catch::Matchers::ContainsSubstring;

namespace
{
/// A shader source with no files behind it: the graph gets these exact words for one name.
class MemoryShaderSource final : public IShaderSourceProvider
{
public:
    MemoryShaderSource(std::string name, std::vector<uint32_t> words)
        : m_name(std::move(name)), m_words(std::move(words))
    {
    }

    std::optional<std::vector<uint32_t>> LoadShader(const std::string& shaderName) const override
    {
        if (shaderName != m_name) return std::nullopt;
        return m_words;
    }

private:
    std::string m_name;
    std::vector<uint32_t> m_words;
};

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
        .device = device, .resourceAllocator = GetGraphAllocator(), .shaderCache = &cache};
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

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "ShaderCache: a shader can be supplied from memory", "[ShaderCache]")
{
    // Device-only, so this runs on every machine the suite runs on: the provider is asked for words that
    // come from a translation unit's input rather than a file. `memoryOnly.comp.heap` deliberately does
    // not exist under `shaders/`, so nothing here can succeed through the file path.
    VkRenderDevice* device = GetRenderDevice();
    const std::vector<uint32_t> words =
        Muyo::ReadSpirvFile(std::filesystem::path("shaders") / "testSpecConstant.comp.heap.spv");
    REQUIRE(!words.empty());
    const std::string shaderName = "memoryOnly.comp.heap";
    const MemoryShaderSource source(shaderName, words);

    // The cache contract with an explicit source: the key hashes the words the provider returned, and
    // the module was created from them.
    ShaderCache cache(device->GetDevice());
    const std::optional<ShaderAssetKey> key = cache.LoadShader(shaderName, source);
    REQUIRE(key.has_value());
    CHECK(key->contentHash == HashSpirv(words));
    const ShaderAsset* asset = cache.GetShaderAsset(*key);
    REQUIRE(asset != nullptr);
    CHECK(asset->spirv == words);
    CHECK(cache.GetModuleCount() == 1);
    CHECK(cache.GetModuleCreateCount() == 1);

    // A name the provider does not have is a load failure, not an exception: the graph names it.
    CHECK(!cache.LoadShader("absent.comp.heap", source).has_value());
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "RenderGraphBuilder: a shader can be supplied from memory",
                 "[RenderGraphBuilder][ShaderCache][DescriptorHeap]")
{
    // The teeth case for #292, at the builder level. `memoryOnly.comp.heap` has no `.spv` under `shaders/`,
    // so both builder paths below can only succeed by asking the provider. With the provider reverted to
    // the old file-only path, AddNode throws "Failed to open file" (the recorded teeth run).
    VkRenderDevice* device = GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; the builder shader-source case records this row as a gap");
    }

    BufferResource* pOut = GetRenderResourceManager()->AllocateBuffer("ShaderCacheOut", 2 * sizeof(uint32_t),
                                                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                                      VMA_MEMORY_USAGE_CPU_TO_GPU);
    REQUIRE(pOut != nullptr);

    const std::vector<uint32_t> words =
        Muyo::ReadSpirvFile(std::filesystem::path("shaders") / "testSpecConstant.comp.heap.spv");
    REQUIRE(!words.empty());
    const std::string shaderName = "memoryOnly.comp.heap";
    const MemoryShaderSource source(shaderName, words);

    RenderGraphNodeCreateInfo node = MakeShaderCacheNode("MemoryShaderNode");
    node.shaderNames = {shaderName};

    // Path 1: services with a shared cache. The cache must be handed the same provider, or it would look
    // for a file that does not exist.
    ShaderCache cache(device->GetDevice());
    const RenderGraphServices cachedServices{
        .device = device, .resourceAllocator = GetGraphAllocator(), .shaderCache = &cache, .shaderSource = &source};
    RenderGraphBuilder cachedBuilder(cachedServices);
    cachedBuilder.ImportResource("ShaderCacheOut", pOut);
    cachedBuilder.AddNode(node);
    CHECK(cache.GetModuleCount() == 1);  // the provider supplied the words; one module exists
    CHECK(cache.GetModuleCreateCount() == 1);
    cachedBuilder.Build();
    REQUIRE(cachedBuilder.GetCompiledGraph().GetNodes().front().pipeline != VK_NULL_HANDLE);
    cachedBuilder.Execute();

    // Path 2: services with no cache, so the builder's internal ShaderAssetManager does the loading. This
    // is the other half of the threading: the provider has to reach it too.
    const RenderGraphServices directServices{
        .device = device, .resourceAllocator = GetGraphAllocator(), .shaderSource = &source};
    RenderGraphBuilder directBuilder(directServices);
    directBuilder.ImportResource("ShaderCacheOut", pOut);
    directBuilder.AddNode(node);
    directBuilder.Build();
    REQUIRE(directBuilder.GetCompiledGraph().GetNodes().front().pipeline != VK_NULL_HANDLE);
    directBuilder.Execute();

    // A name the provider does not have is the graph's named load error, not a file open.
    RenderGraphNodeCreateInfo missing = MakeShaderCacheNode("MissingShaderNode");
    missing.shaderNames = {"absent.comp.heap"};
    RenderGraphBuilder missingBuilder(directServices);
    CHECK_THROWS_WITH(missingBuilder.AddNode(missing), ContainsSubstring("could not be loaded"));
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "ShaderCache: a non-default search path is used", "[ShaderCache]")
{
    VkRenderDevice* device = GetRenderDevice();
    const std::filesystem::path searchPath = std::filesystem::temp_directory_path() / "muyo-rg-shader-source-test";
    std::error_code ec;
    std::filesystem::remove_all(searchPath, ec);
    std::filesystem::create_directories(searchPath, ec);
    REQUIRE(!ec);

    // Copy the shader under a name that exists only in the custom directory, so the default path cannot
    // supply it: success below is evidence about the configured search path and nothing else.
    const std::string shaderName = "searchPathOnly.comp.heap";
    const std::filesystem::path spvPath = searchPath / (shaderName + ".spv");
    std::filesystem::copy_file(std::filesystem::path("shaders") / "testSpecConstant.comp.heap.spv", spvPath,
                               std::filesystem::copy_options::overwrite_existing, ec);
    REQUIRE(!ec);

    const FileShaderSourceProvider customSource(searchPath);
    CHECK(customSource.GetSearchPath() == searchPath);

    const std::vector<uint32_t> expected = Muyo::ReadSpirvFile(spvPath);
    REQUIRE(!expected.empty());

    ShaderCache cache(device->GetDevice());
    const std::optional<ShaderAssetKey> key = cache.LoadShader(shaderName, customSource);
    REQUIRE(key.has_value());
    CHECK(key->contentHash == HashSpirv(expected));

    // The same name is absent from the default `shaders/` directory, which is what makes the load above
    // the configured path's doing rather than the default path's.
    CHECK_THROWS_WITH(cache.LoadShader(shaderName), ContainsSubstring("Failed to open file"));

    std::filesystem::remove_all(searchPath, ec);
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "ShaderCache: the default file source loads shaders/<name>.spv",
                 "[ShaderCache]")
{
    // Every in-tree consumer supplies no provider, so the historical `shaders/<name>.spv` lookup is the
    // path that must keep working. The explicit `FileShaderSourceProvider()` is checked to be the same
    // lookup, so a host that constructs one to change nothing still gets nothing different.
    VkRenderDevice* device = GetRenderDevice();
    const std::vector<uint32_t> expected =
        Muyo::ReadSpirvFile(std::filesystem::path("shaders") / "testSpecConstant.comp.heap.spv");
    REQUIRE(!expected.empty());

    ShaderCache cache(device->GetDevice());
    const std::optional<ShaderAssetKey> key = cache.LoadShader("testSpecConstant.comp.heap");
    REQUIRE(key.has_value());
    CHECK(key->contentHash == HashSpirv(expected));
    const ShaderAsset* asset = cache.GetShaderAsset(*key);
    REQUIRE(asset != nullptr);
    CHECK(asset->spirv == expected);
    CHECK(cache.GetModuleCreateCount() == 1);

    const FileShaderSourceProvider defaultSource;
    CHECK(defaultSource.GetSearchPath() == std::filesystem::path("shaders"));
    const std::optional<ShaderAssetKey> viaProvider = cache.LoadShader("testSpecConstant.comp.heap", defaultSource);
    REQUIRE(viaProvider.has_value());
    CHECK(*viaProvider == *key);  // same name and content: a hit, not a second module
    CHECK(cache.GetModuleCreateCount() == 1);
}
