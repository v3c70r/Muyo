#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstring>
#include <string>
#include <vector>

#include "GraphicsTestEnv.h"
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
