// B1a: DescriptorHeapService lifetime, sampler cache and per-execution rings.
//
// The descriptor-readback half is the B0 probe converted to the service
// (DescriptorHeapProbeTests.cpp); these cases cover the state machine that B0 did not need: static
// free-list reuse with generations, sampler deduplication, ring separation and the named YCbCr
// rejection. They deliberately create no descriptor set, set layout, pool or pipeline layout.

#include <vulkan/vulkan.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <memory>

#include "DescriptorHeapService.h"
#include "GraphicsTestEnv.h"
#include "VkExtFuncsLoader.h"
#include "VkMemoryAllocator.h"

namespace
{
std::unique_ptr<Muyo::DescriptorHeapService> CreateService()
{
    auto service = std::make_unique<Muyo::DescriptorHeapService>();
    service->Initialize(Muyo::GetRenderDevice());
    return service;
}

VkSamplerCreateInfo MakeSampler(VkFilter filter)
{
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.maxLod = 0.0F;
    return info;
}

struct AddressBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
};

AddressBuffer CreateAddressBuffer()
{
    AddressBuffer result;
    Muyo::GetMemoryAllocator()->AllocateBuffer(
        16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
        result.buffer, result.allocation);
    result.address = Muyo::GetRenderDevice()->GetBufferDeviceAddress(result.buffer);
    return result;
}

void DestroyAddressBuffer(AddressBuffer& buffer)
{
    Muyo::GetMemoryAllocator()->FreeBuffer(buffer.buffer, buffer.allocation);
}
}  // namespace

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "DescriptorHeapService: sampler cache and YCbCr rejection", "[DescriptorHeap]")
{
    Muyo::VkRenderDevice* device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B1a records the row as a gap, not a pass");
    }
    auto service = CreateService();

    VkSamplerCreateInfo linear = MakeSampler(VK_FILTER_LINEAR);
    const Muyo::DescriptorSlot first = service->AcquireSampler(linear);
    const Muyo::DescriptorSlot second = service->AcquireSampler(linear);
    CHECK(first.index == second.index);
    CHECK(service->IndexOf(first) == first.index);

    VkSamplerCreateInfo nearest = MakeSampler(VK_FILTER_NEAREST);
    const Muyo::DescriptorSlot different = service->AcquireSampler(nearest);
    CHECK(different.index != first.index);

    VkSamplerYcbcrConversionInfo ycbcr{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    VkSamplerCreateInfo ycbcrSampler = MakeSampler(VK_FILTER_LINEAR);
    ycbcrSampler.pNext = &ycbcr;
    CHECK_THROWS_WITH(service->AcquireSampler(ycbcrSampler),
                      Catch::Matchers::ContainsSubstring("YCbCr/subsampled samplers require the mapping path"));

    // Custom-border samplers need the extension enabled on the device before the service can register an
    // index. The test host does not request it, so the named rejection is the observable here; the
    // registry/lifecycle path is exercised where the extension is enabled.
    VkSamplerCreateInfo custom = MakeSampler(VK_FILTER_LINEAR);
    custom.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
    VkSamplerCustomBorderColorCreateInfoEXT customInfo{VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    customInfo.customBorderColor.float32[0] = 1.0F;
    customInfo.format = VK_FORMAT_UNDEFINED;
    custom.pNext = &customInfo;
    if (Muyo::VkExt::vkRegisterCustomBorderColorEXT == nullptr)
    {
        CHECK_THROWS_WITH(service->AcquireSampler(custom),
                          Catch::Matchers::ContainsSubstring("require VK_EXT_custom_border_color"));
    }
    else
    {
        WARN(
            "VK_EXT_custom_border_color is enabled; the custom-border registry path is exercised by the "
            "extension-capable device row");
    }
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv,
                 "DescriptorHeapService: static slots retire and generations reject stale handles", "[DescriptorHeap]")
{
    Muyo::VkRenderDevice* device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B1a records the row as a gap, not a pass");
    }
    auto service = CreateService();
    AddressBuffer buffer = CreateAddressBuffer();

    const Muyo::DescriptorSlot slot = service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const uint32_t index = service->IndexOf(slot);
    service->Retire(slot, 1);
    service->ResetExecution(0, 1);

    const Muyo::DescriptorSlot reused = service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(reused.index == index);
    CHECK_THROWS_WITH(service->IndexOf(slot), Catch::Matchers::ContainsSubstring("stale descriptor slot generation"));
    DestroyAddressBuffer(buffer);
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "DescriptorHeapService: per-execution rings are separate and reset by slot",
                 "[DescriptorHeap]")
{
    Muyo::VkRenderDevice* device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B1a records the row as a gap, not a pass");
    }
    auto service = CreateService();
    AddressBuffer buffer = CreateAddressBuffer();

    service->BeginExecution(0);
    const Muyo::DescriptorSlot zero =
        service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, true);
    service->BeginExecution(1);
    const Muyo::DescriptorSlot one =
        service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, true);
    CHECK(zero.index != one.index);
    CHECK(service->IndexOf(zero) == zero.index);
    CHECK(service->IndexOf(one) == one.index);

    service->ResetExecution(0, 1);
    service->BeginExecution(0);
    const Muyo::DescriptorSlot reset =
        service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, true);
    CHECK(reset.index == zero.index);
    DestroyAddressBuffer(buffer);
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "DescriptorHeapService: AS direct decision follows the queried descriptor size",
                 "[DescriptorHeap]")
{
    Muyo::VkRenderDevice* device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B1a records the row as a gap, not a pass");
    }
    if (!device->IsRayTracingSupported())
    {
        SKIP("ray tracing is not enabled in this configuration; AS coverage needs the RT build");
    }
    auto service = CreateService();
    const VkDeviceSize size = device->GetDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
    CHECK(service->CanUseDirectAccelerationStructure() == (size == 8));
}
TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "DescriptorHeapService: re-initialize clears stale free-list and ring state",
                 "[DescriptorHeap]")
{
    Muyo::VkRenderDevice* device = Muyo::GetRenderDevice();
    if (!device->IsDescriptorHeapEnabled())
    {
        SKIP("descriptor heap not enabled on this device; B1a records the row as a gap, not a pass");
    }
    auto service = CreateService();
    AddressBuffer buffer = CreateAddressBuffer();

    Muyo::DescriptorHeapService::Config large;
    large.bufferSlots = 4;
    service->Initialize(device, large);
    Muyo::DescriptorSlot retired{};
    for (uint32_t i = 0; i < 4; ++i)
    {
        retired = service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }
    service->Retire(retired, 1);
    service->ResetExecution(0, 1);

    Muyo::DescriptorHeapService::Config small;
    small.bufferSlots = 1;
    service->Initialize(device, small);
    CHECK_THROWS_WITH(service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, true),
                      Catch::Matchers::ContainsSubstring("ring acquisition without BeginExecution"));
    service->BeginExecution(0);
    const Muyo::DescriptorSlot slot = service->AcquireBuffer(buffer.address, 16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK_NOTHROW(service->IndexOf(slot));
    DestroyAddressBuffer(buffer);
}
