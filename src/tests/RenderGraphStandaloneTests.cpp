// The graph's resource/allocator boundary, exercised without the engine (#272).
//
// This binary links `muyo_rg` and `muyo_device` only. It builds a device, a VMA allocator and the
// device-side graph allocator by construction, then runs a graph that owns a buffer - no
// `RenderResourceManager`, no engine resource class, no engine global. If the graph regains an engine
// dependency, this target stops linking, which is the check issue #272 asks for.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "RenderGraph/RenderGraphBuilder.h"
#include "VkGraphResourceAllocator.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace
{
using namespace Muyo;
using namespace Muyo::RenderGraph;

/// A headless device, its VMA allocator and a clean teardown, all constructed here rather than taken
/// from the engine's globals.
struct StandaloneDevice
{
    VkRenderDevice device;
    VkMemoryAllocator allocator;

    StandaloneDevice()
    {
        device.Initialize({}, {});
        device.CreateDevice({}, {});
        device.CreateCommandPools();
        allocator.Initalize(&device);
    }
    ~StandaloneDevice()
    {
        device.DestroyCommandPools();
        allocator.Unintialize();
        device.DestroyDevice();
        device.Unintialize();
    }

    StandaloneDevice(const StandaloneDevice&) = delete;
    StandaloneDevice& operator=(const StandaloneDevice&) = delete;
};
}  // namespace

TEST_CASE("RenderGraph standalone: a CPU node writes a graph-owned buffer", "[RenderGraph][Standalone]")
{
    StandaloneDevice standalone;
    VkGraphResourceAllocator graphAllocator(standalone.device, standalone.allocator);

    RenderGraphBuilder builder(RenderGraphServices{.device = &standalone.device, .resourceAllocator = &graphAllocator});

    constexpr uint32_t kExpected = 0xC0FFEEu;
    builder.AddResource("StandaloneOut", BufferResourceDesc{.count = 1,
                                                            .stride = sizeof(uint32_t),
                                                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                            .memoryProperties = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                                            .lifetime = ResourceLifetime::Persistent});
    builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "WriteStandaloneOut",
                                              .queueType = QueueType::CPU,
                                              .resourceUses = {{.handle = ResourceHandle("StandaloneOut"),
                                                                .io = ResourceIOType::HOST_WRITE,
                                                                .usage = ResourceUsage::STORAGE_BUFFER,
                                                                .kind = ResourceKind::BUFFER}},
                                              .execute = [](RenderGraphNodeContext& context)
                                              {
                                                  IGraphBuffer* pBuffer =
                                                      context.GetResource<IGraphBuffer>("StandaloneOut");
                                                  REQUIRE(pBuffer != nullptr);
                                                  const uint32_t value = kExpected;
                                                  pBuffer->SetData(&value, sizeof(value));
                                              }});

    builder.Build();
    builder.Execute();
    builder.WaitIdle();

    // Resolve through the graph's own surface: the resource is a graph-owned buffer, not an engine class.
    const IGraphResource* pResource = builder.ResolveResource("StandaloneOut");
    REQUIRE(pResource != nullptr);
    REQUIRE(pResource->GetResourceKind() == ResourceKind::BUFFER);
    auto* pBuffer = const_cast<IGraphBuffer*>(static_cast<const IGraphBuffer*>(pResource));
    REQUIRE(pBuffer->buffer() != VK_NULL_HANDLE);
    REQUIRE(pBuffer->GetSize() == sizeof(uint32_t));

    const auto* pMapped = static_cast<const uint32_t*>(pBuffer->Map());
    REQUIRE(pMapped != nullptr);
    CHECK(*pMapped == kExpected);
    pBuffer->Unmap();
}

TEST_CASE("RenderGraph standalone: a graph-owned image is an IGraphImage", "[RenderGraph][Standalone]")
{
    StandaloneDevice standalone;
    VkGraphResourceAllocator graphAllocator(standalone.device, standalone.allocator);

    RenderGraphBuilder builder(RenderGraphServices{.device = &standalone.device, .resourceAllocator = &graphAllocator});
    builder.AddResource("StandaloneTarget", ImageResourceDesc{.format = VK_FORMAT_R8G8B8A8_UNORM,
                                                              .extent = {8, 8},
                                                              .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                                              .lifetime = ResourceLifetime::Persistent});
    builder.AddNode(RenderGraphNodeCreateInfo{.nodeName = "ClearStandaloneTarget",
                                              .queueType = QueueType::CPU,
                                              .resourceUses = {{.handle = ResourceHandle("StandaloneTarget"),
                                                                .io = ResourceIOType::READ,
                                                                .usage = ResourceUsage::SAMPLED,
                                                                .kind = ResourceKind::IMAGE}},
                                              .execute = [](RenderGraphNodeContext&) {}});

    builder.Build();

    const IGraphResource* pResource = builder.ResolveResource("StandaloneTarget");
    REQUIRE(pResource != nullptr);
    REQUIRE(pResource->GetResourceKind() == ResourceKind::IMAGE);
    const auto* pImage = static_cast<const IGraphImage*>(pResource);
    CHECK(pImage->getImage() != VK_NULL_HANDLE);
    CHECK(pImage->getView() != VK_NULL_HANDLE);
    CHECK(pImage->GetImageFormat() == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(pImage->GetImageExtent().width == 8);
    CHECK(pImage->GetImageExtent().height == 8);
}
