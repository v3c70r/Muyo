// The graph's resource/allocator boundary, exercised without the engine (#272), and the compute-only
// execution path that has no renderer in it (#54).
//
// This binary links `muyo_rg` and `muyo_device` only. It builds a device, a VMA allocator and the
// device-side graph allocator by construction, then runs a graph that owns a buffer - no
// `RenderResourceManager`, no engine resource class, no engine global. If the graph regains an engine
// dependency, this target stops linking, which is the check issue #272 asks for.
//
// The compute-only case below adds the half a renderer cannot state: a hand-built plan whose only
// segment is `COMPUTE`, no attachment and no graphics node, chained across two executions through a
// caller-owned timeline. It runs on whatever device is present - a single family takes the
// compute-on-graphics-family path, a dedicated family takes the compute queue - and the device line it
// prints records which one ran.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Debug.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphExecutionPlan.h"
#include "RenderGraph/RenderGraphExecutor.h"
#include "VkGraphResourceAllocator.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace
{
using namespace Muyo;
using namespace Muyo::RenderGraph;

/// Prints, once, the device this binary actually ran on, in the shape the engine suite uses
/// (AGENTS.md section 4): a green run on a software rasteriser is not evidence about a driver, and a
/// single-family device does not take the cross-queue path. Both of those are facts about the run, so
/// the run states them instead of leaving them to be assumed.
void LogTestDevice(VkRenderDevice& device)
{
    static bool bLogged = false;
    if (bLogged) return;
    bLogged = true;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
    VkPhysicalDeviceDriverProperties driverProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &driverProperties;
    vkGetPhysicalDeviceProperties2(device.GetPhysicalDevice(), &properties2);

    const uint32_t loaderVersion = device.GetInstanceVersion();
    std::cerr << "[test device] " << properties.deviceName << " (Vulkan " << VK_VERSION_MAJOR(properties.apiVersion)
              << "." << VK_VERSION_MINOR(properties.apiVersion) << "." << VK_VERSION_PATCH(properties.apiVersion)
              << ", driver=" << driverProperties.driverName << " " << driverProperties.driverInfo
              << ", loader=" << VK_VERSION_MAJOR(loaderVersion) << "." << VK_VERSION_MINOR(loaderVersion) << "."
              << VK_VERSION_PATCH(loaderVersion)
              << ") - validation: layer=" << (device.IsValidationLayerEnabled() ? "enabled" : "NOT enabled")
              << ", messenger=" << (device.HasDebugMessenger() ? "installed" : "NOT installed")
              << ", families graphics=" << device.GetGraphicsQueueFamily()
              << (device.IsComputeQueueDedicated()
                      ? ", compute=" + std::to_string(device.GetComputeQueueFamily()) +
                            " (dedicated compute family present, the COMPUTE segment runs on it)"
                      : " (single queue family, the COMPUTE segment runs on the graphics family)")
              << std::endl;
}

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

/// A validated headless device for the execution-path cases, which need no allocator.
///
/// The host opts in to validation (#58) through `muyo_device`'s own hooks, so a validation ERROR aborts
/// the run in every build configuration. That is what makes "validation-clean" a property of the run
/// rather than a claim about stderr, and it needs no engine header, no engine global and no `muyo` link.
///
/// It deliberately does **not** construct a `VkMemoryAllocator`: in a ray-tracing build that class
/// creates its SBT/BVH pools during initialization, and those pools name ray-tracing usage bits that
/// require the device to have the extension enabled (#287). The COMPUTE-only execution path needs
/// neither the allocator nor ray tracing, so this fixture stays on the device alone.
struct ValidatedDevice
{
    VkRenderDevice device;

    ValidatedDevice()
    {
        device.Initialize({GetValidationExtensionName()}, {GetValidationLayerName()});
        device.InitializeDebugMessenger();
        device.CreateDevice({}, {});
        device.CreateCommandPools();
        LogTestDevice(device);
    }
    ~ValidatedDevice()
    {
        device.DestroyCommandPools();
        device.DestroyDevice();
        device.Unintialize();
    }

    ValidatedDevice(const ValidatedDevice&) = delete;
    ValidatedDevice& operator=(const ValidatedDevice&) = delete;
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

namespace
{
/// Begin and end every command buffer of a slot, so the plan can be submitted with no work in it.
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

/// @return A caller-owned timeline semaphore starting at `initialValue`.
VkSemaphore CreateTimeline(VkDevice device, uint64_t initialValue)
{
    VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = initialValue;
    VkSemaphoreCreateInfo createInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    createInfo.pNext = &typeInfo;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateSemaphore(device, &createInfo, nullptr, &semaphore));
    return semaphore;
}
}  // namespace

TEST_CASE("RenderGraph standalone: a COMPUTE-only plan chains two executions through a caller-owned timeline",
          "[RenderGraph][Standalone][ComputeOnly]")
{
    // Issue #54's in-tree statement that the execution path needs no renderer. The plan is built by hand,
    // so the case depends on neither a builder nor a topology: one segment, scheduled on the compute
    // queue, with no node, no colour attachment and no resource. That is a plan and a timeline rather
    // than a fixture, and it holds on a device with a dedicated compute family and on one without.
    ValidatedDevice standalone;
    VkRenderDevice& renderDevice = standalone.device;

    // State the validation claim instead of assuming it. With the layer and messenger installed a
    // validation ERROR aborts the run, so a green run is a validation-clean run in Debug and Release.
    REQUIRE(renderDevice.IsValidationLayerEnabled());
    REQUIRE(renderDevice.HasDebugMessenger());

    RenderGraphExecutionPlan plan;
    plan.segments = {{QueueType::COMPUTE, 0, 1}};
    REQUIRE(plan.GetSegmentCount() == 1);
    CHECK(plan.segments[0].queueType == QueueType::COMPUTE);
    CHECK(plan.GetTransferCount() == 0);
    CHECK(plan.cpuNodes.empty());

    // No graphics-family assumption: the compute queue key resolves to the compute family, which is the
    // graphics family on a single-family device and a separate family where one exists. Both topologies
    // run this case; the [test device] line says which one did.
    const RenderGraphQueueFamilies families{renderDevice.GetGraphicsQueueFamily(),
                                            renderDevice.GetComputeQueueFamily()};
    CHECK(GetQueueFamilyForQueueType(QueueType::COMPUTE, families) == renderDevice.GetComputeQueueFamily());
    REQUIRE(renderDevice.GetComputeQueue() != VK_NULL_HANDLE);

    // Two in-flight slots, so the second execution is accepted while the first is still outstanding.
    // One slot is the documented backpressure path and would make the second AcquireSlot wait for the
    // first to complete, which is exactly the serialisation this case exists to rule out.
    RenderGraphExecutor executor(&renderDevice, 2);
    REQUIRE(executor.GetInFlightCount() == 2);
    CHECK(executor.GetQueueForType(QueueType::COMPUTE) == renderDevice.GetComputeQueue());

    VkDevice device = renderDevice.GetDevice();
    // Caller-owned timelines. `gate` keeps the first execution outstanding until the test releases it;
    // `chain` is the timeline the two executions are chained through - the first signals it, the second
    // waits on it - and neither belongs to the executor.
    VkSemaphore gate = CreateTimeline(device, 0);
    VkSemaphore chain = CreateTimeline(device, 0);
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence firstDone = VK_NULL_HANDLE;
    VkFence secondDone = VK_NULL_HANDLE;
    VK_ASSERT(vkCreateFence(device, &fenceInfo, nullptr, &firstDone));
    VK_ASSERT(vkCreateFence(device, &fenceInfo, nullptr, &secondDone));

    // Execution 1: waits on the caller's gate, signals the chain at value 1 and its own fence.
    const RenderGraphExecutor::ExecutionSlot first = executor.AcquireSlot(plan);
    RecordEmptyCommandBuffers(first);
    executor.Submit(plan, first,
                    RenderGraphExecuteInfo{.waitSemaphore = gate,
                                           .waitValue = 1,
                                           .signalSemaphore = chain,
                                           .signalValue = 1,
                                           .signalFence = firstDone});

    // Execution 2 is submitted without waiting for the first, and waits on the chain at value 2 - a
    // value the first execution does not signal. That is what separates "chained through the caller's
    // timeline" from "waits for the previous execution to finish": the first can complete and signal 1,
    // and the second must still be outstanding because 2 has not been reached.
    const RenderGraphExecutor::ExecutionSlot second = executor.AcquireSlot(plan);
    RecordEmptyCommandBuffers(second);
    executor.Submit(plan, second,
                    RenderGraphExecuteInfo{.waitSemaphore = chain, .waitValue = 2, .signalFence = secondDone});

    // Work outstanding, with both submissions made: the gate is uncrossed, so the first cannot have
    // completed, and the second was accepted before it did.
    uint64_t gateValue = 0;
    VK_ASSERT(vkGetSemaphoreCounterValue(device, gate, &gateValue));
    CHECK(gateValue == 0);
    CHECK(vkGetFenceStatus(device, firstDone) == VK_NOT_READY);
    // 100 ms is several orders of magnitude longer than an empty submission takes, so a dropped wait
    // signals the second fence well within it and this check fails. A zero timeout would be satisfied
    // by ordinary submit latency and prove nothing.
    CHECK(vkWaitForFences(device, 1, &secondDone, VK_TRUE, 100'000'000) == VK_TIMEOUT);

    // Release the gate. Execution 1 completes and signals the chain at 1; execution 2 waits for 2, so it
    // is still outstanding even though the first has finished.
    VkSemaphoreSignalInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    signalInfo.semaphore = gate;
    signalInfo.value = 1;
    VK_ASSERT(vkSignalSemaphore(device, &signalInfo));
    REQUIRE(vkWaitForFences(device, 1, &firstDone, VK_TRUE, 5'000'000'000) == VK_SUCCESS);

    uint64_t chainValue = 0;
    VK_ASSERT(vkGetSemaphoreCounterValue(device, chain, &chainValue));
    CHECK(chainValue == 1);
    CHECK(vkWaitForFences(device, 1, &secondDone, VK_TRUE, 100'000'000) == VK_TIMEOUT);

    // The caller advances the chain past the value the second execution waits for, and it completes.
    signalInfo.semaphore = chain;
    signalInfo.value = 2;
    VK_ASSERT(vkSignalSemaphore(device, &signalInfo));
    REQUIRE(vkWaitForFences(device, 1, &secondDone, VK_TRUE, 5'000'000'000) == VK_SUCCESS);

    executor.WaitIdle();

    vkDestroyFence(device, firstDone, nullptr);
    vkDestroyFence(device, secondDone, nullptr);
    vkDestroySemaphore(device, gate, nullptr);
    vkDestroySemaphore(device, chain, nullptr);
}
