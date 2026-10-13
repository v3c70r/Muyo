#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstring>
#include <string>
#include <vector>

#include "GraphicsTestEnv.h"
#include "ShaderAsset.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace
{
VkExtensionProperties MakeExtension(const char* sName)
{
    VkExtensionProperties properties{};
    std::strncpy(properties.extensionName, sName, VK_MAX_EXTENSION_NAME_SIZE - 1);
    return properties;
}

VkLayerProperties MakeLayer(const char* sName)
{
    VkLayerProperties properties{};
    std::strncpy(properties.layerName, sName,
                 VK_MAX_EXTENSION_NAME_SIZE - 1);  // layerName is sized by the extension-name constant
    return properties;
}
}  // namespace

TEST_CASE("VkRenderDevice: an unavailable layer or instance extension fails at Initialize", "[RenderDevice]")
{
    // #35 and #30. These guards were plain asserts, so a release build skipped them, carried on to
    // vkCreateInstance with the request still in the list, and failed there with nothing naming what
    // caused it. The message naming the offending string is the whole point of the check: "Vulkan
    // initialization failed" is a worse diagnostic than "you asked for VK_LAYER_does_not_exist".
    //
    // Device-independent: the names below exist on no machine, and the guards consult the same
    // instance-level lists on every one, so this does not depend on the GPU or the loader present.
    Muyo::VkRenderDevice device;
    CHECK_THROWS_WITH(device.Initialize({}, {"VK_LAYER_does_not_exist"}),
                      Catch::Matchers::ContainsSubstring("VK_LAYER_does_not_exist"));
    // Throwing before touching Vulkan is what makes it safe to retry on the same object.
    CHECK_THROWS_WITH(device.Initialize({"VK_EXT_instance_does_not_exist"}, {}),
                      Catch::Matchers::ContainsSubstring("VK_EXT_instance_does_not_exist"));
}

TEST_CASE("VkRenderDevice: capability lookups match names exactly", "[RenderDevice]")
{
    // Both lookups tested `strcmp(...)` directly, which is true when the strings *differ*, so they
    // returned true for essentially any name once the list was non-empty. The asserts guarding
    // VkRenderDevice::Initialize could therefore never fire, and a wrong answer looked identical to
    // a right one. Only the negative case below catches that, which is why these run against a
    // synthetic list rather than through a device: the device cannot express "this name is absent".
    const std::vector<VkExtensionProperties> extensions = {MakeExtension("VK_KHR_swapchain"),
                                                           MakeExtension("VK_EXT_headless_surface")};
    CHECK(Muyo::HasInstanceExtension(extensions, "VK_KHR_swapchain"));
    CHECK(Muyo::HasInstanceExtension(extensions, "VK_EXT_headless_surface"));
    CHECK_FALSE(Muyo::HasInstanceExtension(extensions, "VK_KHR_does_not_exist"));
    CHECK_FALSE(Muyo::HasInstanceExtension({}, "VK_KHR_swapchain"));

    const std::vector<VkLayerProperties> layers = {MakeLayer("VK_LAYER_KHRONOS_validation")};
    CHECK(Muyo::HasLayer(layers, "VK_LAYER_KHRONOS_validation"));
    CHECK_FALSE(Muyo::HasLayer(layers, "VK_LAYER_does_not_exist"));
    CHECK_FALSE(Muyo::HasLayer({}, "VK_LAYER_KHRONOS_validation"));
}

TEST_CASE("VkRenderDevice: requested device extensions are resolved against what the device reports", "[RenderDevice]")
{
    // Device extensions were enabled without being looked up at all: a name the device does not report
    // reached vkCreateDevice and came back as VK_ERROR_EXTENSION_NOT_PRESENT, naming neither the extension
    // nor the device. Both branches are exercised here rather than on hardware, because the machine that
    // lacks a given extension is not the machine that has it.
    const std::vector<VkExtensionProperties> extensions = {MakeExtension("VK_KHR_swapchain"),
                                                           MakeExtension("VK_KHR_cooperative_matrix")};

    CHECK(Muyo::HasDeviceExtension(extensions, "VK_KHR_swapchain"));
    CHECK(Muyo::HasDeviceExtension(extensions, "VK_KHR_cooperative_matrix"));
    CHECK_FALSE(Muyo::HasDeviceExtension(extensions, "VK_KHR_does_not_exist"));
    CHECK_FALSE(Muyo::HasDeviceExtension({}, "VK_KHR_swapchain"));
    CHECK_FALSE(Muyo::HasDeviceExtension(extensions, nullptr));

    CHECK(Muyo::FindUnsupportedDeviceExtension({"VK_KHR_swapchain"}, extensions) == nullptr);
    CHECK(Muyo::FindUnsupportedDeviceExtension({}, extensions) == nullptr);
    // The first missing one is named, which is what the throw in CreateDevice reports.
    CHECK(std::string(Muyo::FindUnsupportedDeviceExtension({"VK_KHR_swapchain", "VK_EXT_missing"}, extensions)) ==
          "VK_EXT_missing");
    CHECK(std::string(Muyo::FindUnsupportedDeviceExtension({"VK_EXT_missing", "VK_KHR_swapchain"}, extensions)) ==
          "VK_EXT_missing");
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "VkRenderDevice: the cooperative-matrix query agrees with the device",
                 "[RenderDevice]")
{
    // #63. The query reads two things and either can be wrong in a way that still compiles: reading the
    // wrong feature struct, or treating the extension as sufficient when the feature is what gates it. So
    // the answer is compared against the device's own extension list rather than trusted.
    Muyo::VkRenderDevice* pDevice = Muyo::GetRenderDevice();
    const VkPhysicalDevice physicalDevice = pDevice->GetPhysicalDevice();

    uint32_t nExtensionCount = 0;
    Muyo::VK_ASSERT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, nullptr));
    std::vector<VkExtensionProperties> extensions(nExtensionCount);
    Muyo::VK_ASSERT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, extensions.data()));

    const bool bExposesExtension = Muyo::HasDeviceExtension(extensions, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);

    // A device without the extension can never be reported as supporting it. The converse - extension
    // present but the feature false - is possible in principle, so it is checked rather than assumed, and
    // a device that does it is worth knowing about because the extension would then be enabled for nothing.
    if (!bExposesExtension) CHECK_FALSE(pDevice->IsCooperativeMatrixSupported());
    INFO("extension exposed: " << bExposesExtension);
    INFO("unavailable - this device is RADV, which does not expose it; see AGENTS.md section 8");
    CHECK(pDevice->IsCooperativeMatrixSupported() == bExposesExtension);

    // Nothing here enumerates property sets, deliberately. The fetch runs only for a caller that requests
    // the extension - this environment does not, because the renderer must not require cooperative matrix -
    // and enumerating them dereferences a driver dispatch entry that some driver + layer combinations leave
    // NULL (#72), where the crash *is* the probe. So the shape assertions belong to a consumer that asks;
    // what this asserts is the contract either side of that request.
    INFO("this machine's device " << (bExposesExtension ? "exposes" : "does not expose")
                                  << " VK_KHR_cooperative_matrix");
    CHECK(pDevice->GetCooperativeMatrixProperties().empty());
    if (!bExposesExtension)
    {
        WARN(
            "no cooperative matrix on this device, so the supported branch is not covered by this run - "
            "AMDVLK exposes it here, RADV does not");
    }
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv,
                 "VkRenderDevice: 16-bit storage is enabled, so an F16 kernel's module is accepted", "[RenderDevice]")
{
    // #78: an enablement gap, not a support gap. The feature was promoted to core in Vulkan 1.1, and a
    // core optional feature is still off unless the device chain sets it - so a consumer running F16
    // kernels got "SPIR-V contains a 16-bit OpVariable with StorageBuffer StorageClass, but
    // storageBuffer16BitAccess was not enabled" from vkCreateShaderModule, with no way to fix it from
    // outside. See the comment on the features11 block for why the consumer cannot append it.
    //
    // The observable is the consumer's own failure rather than our bookkeeping: create the module and
    // require it to be accepted. Two modules, because a device feature is only proven by SPIR-V that
    // demands it - the GLSL one carries OpCapability StorageBuffer16BitAccess (a runtime array of half,
    // which is the llama.cpp shape) and the slang one carries UniformAndStorageBuffer16BitAccess
    // (16-bit inside a block), covering both flags this enables.
    const std::vector<std::string> shaderPaths = {"shaders/test16BitStorage.comp.spv",
                                                  "shaders/test16BitStorageUniform.comp.slang.spv"};
    for (const std::string& shaderPath : shaderPaths)
    {
        INFO("shader: " << shaderPath << " (rebuild the Shaders target if this is missing)");
        Muyo::SpirvCode spirv = Muyo::ReadSpv(shaderPath);
        REQUIRE(spirv.size() > 0);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        REQUIRE(vkCreateShaderModule(Muyo::GetRenderDevice()->GetDevice(), &createInfo, nullptr, &shaderModule) ==
                VK_SUCCESS);
        vkDestroyShaderModule(Muyo::GetRenderDevice()->GetDevice(), shaderModule, nullptr);
    }
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "VkMemoryAllocator: several resources alias one block at chosen offsets",
                 "[RenderDevice]")
{
    // #16. The mechanism transient memory reuse will be built on: one block, resources bound into it at
    // offsets the caller chooses. Both cases are here because they differ only in the offsets picked -
    // disjoint offsets let both resources be live at once, while the *same* offset is the aliasing case
    // that requires their uses not to overlap in time.
    //
    // On the teardown order: the VMA documentation's aliasing example frees the block before destroying
    // the resources bound to it, which reads as risky. This test destroys first, and the order was checked
    // by trying the other one - the validation layer accepted both on this driver and layer version, so
    // this run does *not* settle which is required, and the claim here is only that the conservative order
    // is the one the memory's own contract implies.
    auto* pAllocator = Muyo::GetMemoryAllocator();
    VkDevice device = Muyo::GetRenderDevice()->GetDevice();
    REQUIRE(pAllocator != nullptr);

    const auto alignUp = [](VkDeviceSize nValue, VkDeviceSize nAlignment)
    { return nAlignment == 0 ? nValue : ((nValue + nAlignment - 1) / nAlignment) * nAlignment; };

    const auto createUnboundBuffer = [device](VkDeviceSize nSize)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = nSize;
        // Storage and both transfer usages, so nothing about a host-visible batch of memory is what the
        // test is accidentally measuring.
        info.usage =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer buffer = VK_NULL_HANDLE;
        REQUIRE(vkCreateBuffer(device, &info, nullptr, &buffer) == VK_SUCCESS);
        return buffer;
    };

    constexpr VkDeviceSize kBytesA = 4096;
    constexpr VkDeviceSize kBytesB = 1024;
    VkBuffer bufferA = createUnboundBuffer(kBytesA);
    VkBuffer bufferB = createUnboundBuffer(kBytesB);
    VkBuffer aliasOfA = createUnboundBuffer(kBytesA);

    // VMA's own recipe for the block: maxima for size and alignment, bitwise AND for the memory types.
    // The AND is the part that matters - a block is only valid for a resource whose type bits it satisfies.
    VkMemoryRequirements reqA{};
    VkMemoryRequirements reqB{};
    VkMemoryRequirements reqAlias{};
    vkGetBufferMemoryRequirements(device, bufferA, &reqA);
    vkGetBufferMemoryRequirements(device, bufferB, &reqB);
    vkGetBufferMemoryRequirements(device, aliasOfA, &reqAlias);

    const VkDeviceSize nOffsetB = alignUp(reqA.size, reqB.alignment);
    VkMemoryRequirements blockRequirements{};
    blockRequirements.size = nOffsetB + reqB.size;
    blockRequirements.alignment = std::max(reqA.alignment, reqB.alignment);
    blockRequirements.memoryTypeBits = reqA.memoryTypeBits & reqB.memoryTypeBits;
    REQUIRE(blockRequirements.memoryTypeBits != 0);

    VmaAllocation block = pAllocator->AllocateAliasingBlock(blockRequirements, VMA_MEMORY_USAGE_CPU_TO_GPU);
    REQUIRE(block != nullptr);

    pAllocator->BindBufferToBlock(bufferA, block, 0);
    pAllocator->BindBufferToBlock(bufferB, block, nOffsetB);
    // The same offset as A on purpose: two resources over one range is the case that needs their uses to
    // be separated in time, and binding it is what proves the block really is shareable.
    pAllocator->BindBufferToBlock(aliasOfA, block, 0);

    void* pMapped = nullptr;
    pAllocator->MapBuffer(block, &pMapped);
    REQUIRE(pMapped != nullptr);
    auto* pWords = static_cast<uint32_t*>(pMapped);
    const auto* pWordsAtOffset = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(pMapped) + nOffsetB);

    // Disjoint: the caller's layout is real - each pattern is where the offsets say.
    pWords[0] = 0xA1A1A1A1;
    pWords[nOffsetB / sizeof(uint32_t)] = 0x11111111;

    // And the *bindings* are where the offsets say, which the mapped pointers above cannot show: a write
    // through a buffer is the only way to observe what a VkBuffer is bound to. B's fill must land at B's
    // offset and leave A's word alone - bind B at zero instead and this is the check that fails.
    Muyo::GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            vkCmdFillBuffer(cmdBuf, bufferB, 0, sizeof(uint32_t), 0xB2B2B2B2);

            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = bufferB;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);
        });

    INFO("A at 0: " << std::hex << pWords[0] << ", B at " << nOffsetB << ": " << std::hex
                    << pWords[nOffsetB / sizeof(uint32_t)]);
    CHECK(pWords[0] == 0xA1A1A1A1);
    CHECK(pWords[nOffsetB / sizeof(uint32_t)] == 0xB2B2B2B2);

    // Overlapping: a write through the alias lands on A's bytes, which is what makes this aliasing rather
    // than suballocation - and why the live ranges, not the offsets, are what keeps it legal.
    Muyo::GetRenderDevice()->ExecuteImmediateCommand(
        [&](VkCommandBuffer cmdBuf)
        {
            vkCmdFillBuffer(cmdBuf, aliasOfA, 0, sizeof(uint32_t), 0xC3C3C3C3);

            // The same transfer-to-host edge as above: without it the read below is a host read of memory
            // a transfer may still be writing.
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = aliasOfA;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmdBuf, &dependency);
        });
    CHECK(pWords[0] == 0xC3C3C3C3);

    pAllocator->UnmapBuffer(block);

    // Destroy, then free. The opposite order is what the VMA example shows; if it were legal the run would
    // continue, and if it is not the callback aborts on the validation error rather than this line.
    vkDestroyBuffer(device, aliasOfA, nullptr);
    vkDestroyBuffer(device, bufferB, nullptr);
    vkDestroyBuffer(device, bufferA, nullptr);
    pAllocator->FreeAliasingBlock(block);
    CHECK(block == nullptr);
}
