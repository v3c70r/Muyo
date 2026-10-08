#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstring>
#include <vector>

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
