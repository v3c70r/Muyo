#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "Debug.h"
#include "Debug/ScopedMarker.h"
#include "Debug/Validation.h"
#include "GraphicsTestEnv.h"
#include "PipelineStateBuilder.h"
#include "ShaderReflectionFetcher.h"
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

/// Owns the process-wide device for the duration of a test case.
///
/// A failed `REQUIRE` unwinds out of the case, so cleanup written after the assertions would not run and
/// the singleton would stay initialized for every later fixture. A destructor runs on both paths.
struct ScopedDeviceLifetime
{
    Muyo::VkRenderDevice* pDevice = Muyo::GetRenderDevice();

    ~ScopedDeviceLifetime()
    {
        if (pDevice->GetDevice() != VK_NULL_HANDLE) pDevice->DestroyDevice();
        pDevice->Unintialize();
    }
};

/// A device the test owns, created the way a library client creates one, with the teardown a failed
/// assertion must still perform.
///
/// Deliberately not `GetRenderDevice()`: the client-device cases below are about a device the
/// process-wide singleton does not know about (#274, #276).
struct ClientDevice
{
    Muyo::VkRenderDevice device;

    ClientDevice() : ClientDevice({}, {}) {}
    ClientDevice(std::vector<const char*> instanceExtensions, std::vector<const char*> layers)
    {
        try
        {
            device.Initialize(instanceExtensions, layers);
            device.CreateDevice({}, {});
        }
        catch (...)
        {
            // A constructor that throws never runs the destructor, so clean up the partially built
            // hierarchy here rather than leaving the next case to trip over it (#278).
            if (device.GetDevice() != VK_NULL_HANDLE) device.DestroyDevice();
            device.Unintialize();
            throw;
        }
    }

    ~ClientDevice()
    {
        if (device.GetDevice() != VK_NULL_HANDLE) device.DestroyDevice();
        device.Unintialize();
    }

    ClientDevice(const ClientDevice&) = delete;
    ClientDevice& operator=(const ClientDevice&) = delete;
};

/// Records the command-buffer labels the validation layer reports.
///
/// The engine's messenger aborts on an error, which is the right host policy and the wrong instrument for
/// a test that has to provoke one to observe the label. The layer includes the open command-buffer labels
/// in an error's callback data, so this is how "the label was recorded" is made observable.
struct LabelCapture
{
    std::vector<std::string> cmdBufLabels;

    static VKAPI_ATTR VkBool32 VKAPI_CALL Callback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                   VkDebugUtilsMessageTypeFlagsEXT,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                   void* pUserData)
    {
        auto* pCapture = static_cast<LabelCapture*>(pUserData);
        for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; ++i)
        {
            if (pCallbackData->pCmdBufLabels[i].pLabelName != nullptr)
            {
                pCapture->cmdBufLabels.emplace_back(pCallbackData->pCmdBufLabels[i].pLabelName);
            }
        }
        return VK_FALSE;
    }
};

/// A client-owned device for marker tests: validation and `VK_EXT_debug_utils` enabled, a messenger that
/// records instead of aborting, and one command buffer to record into.
struct ClientDeviceForMarkers : ClientDevice
{
    LabelCapture capture;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyMessenger = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    ClientDeviceForMarkers() : ClientDevice({VK_EXT_DEBUG_UTILS_EXTENSION_NAME}, {Muyo::GetValidationLayerName()})
    {
        const auto pfnCreateMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(device.GetInstance(), "vkCreateDebugUtilsMessengerEXT"));
        pfnDestroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(device.GetInstance(), "vkDestroyDebugUtilsMessengerEXT"));
        REQUIRE(pfnCreateMessenger != nullptr);
        REQUIRE(pfnDestroyMessenger != nullptr);

        VkDebugUtilsMessengerCreateInfoEXT messengerInfo = {};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        messengerInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        messengerInfo.pfnUserCallback = &LabelCapture::Callback;
        messengerInfo.pUserData = &capture;
        Muyo::VK_ASSERT(pfnCreateMessenger(device.GetInstance(), &messengerInfo, nullptr, &messenger));

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = device.GetGraphicsQueueFamily();
        Muyo::VK_ASSERT(vkCreateCommandPool(device.GetDevice(), &poolInfo, nullptr, &commandPool));

        VkCommandBufferAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        Muyo::VK_ASSERT(vkAllocateCommandBuffers(device.GetDevice(), &allocateInfo, &commandBuffer));
    }

    ~ClientDeviceForMarkers()
    {
        // Before the base class destroys the instance and the device. The command buffer is not in the
        // recording state on any path that reaches here with a live device; a failure inside the label
        // scope aborts the case rather than unwinding through it.
        if (pfnDestroyMessenger != nullptr && messenger != VK_NULL_HANDLE)
        {
            pfnDestroyMessenger(device.GetInstance(), messenger, nullptr);
        }
        if (device.GetDevice() != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device.GetDevice(), commandPool, nullptr);
        }
    }
};
}  // namespace

TEST_CASE("VkRenderDevice: validation is opt-in and a headless device works without it", "[RenderDevice]")
{
    // #58. Every consumer used to pay for validation: `GetRenderDevice()` returned a
    // `VkDebugRenderDevice` whose `Initialize` appended `VK_LAYER_KHRONOS_validation` and
    // `VK_EXT_debug_utils`, then created a messenger. A library client cannot opt out of that, and an
    // inference service pays for a layer it did not ask for.
    //
    // What this pins: the minimal call - no layer list, no surface - produces a working device with no
    // validation layer and no messenger. On the old singleton the first two checks would be true, so
    // the case fails if the appending policy comes back (mutate `Initialize` to append the layer and
    // the test goes red). The device is real rather than a mock because the policy is about what
    // `vkCreateInstance` was asked for.
    ScopedDeviceLifetime lifetime;
    Muyo::VkRenderDevice* pDevice = Muyo::GetRenderDevice();

    pDevice->Initialize({}, {});
    // CHECK(!x), not CHECK_FALSE(x): Catch2's CHECK_FALSE expands through a flag OR that the
    // clang-analyzer EnumCastOutOfRange check reports on the changed line. The assertion is the same.
    CHECK(!pDevice->IsValidationLayerEnabled());
    CHECK(!pDevice->HasDebugMessenger());
    CHECK(pDevice->GetEnabledInstanceLayers().empty());
    CHECK(pDevice->GetEnabledInstanceExtensions().empty());

    // No surface: a headless client must not need one for device creation. The present queue is
    // therefore absent, which is a legitimate configuration rather than a failure (AGENTS.md section 8).
    pDevice->CreateDevice({}, {}, nullptr, {});
    CHECK(pDevice->GetDevice() != VK_NULL_HANDLE);
    CHECK(pDevice->GetPresentQueue() == VK_NULL_HANDLE);
    CHECK(!pDevice->HasDebugMessenger());
}

TEST_CASE("VkRenderDevice: Initialize and Unintialize refuse to destroy a live instance hierarchy", "[RenderDevice]")
{
    // A fixture constructor that throws after `Initialize` never runs its destructor, so the next
    // fixture calls `Initialize` again on a device that still owns an instance - and, in the test host,
    // a surface. Replacing the instance silently would leak it, and destroying it would be invalid usage
    // because the surface is a child this class cannot see. The contract is therefore explicit teardown,
    // and both halves are checked here: Initialize refuses a live instance without touching it, and
    // Unintialize refuses a live logical device.
    ScopedDeviceLifetime lifetime;
    Muyo::VkRenderDevice* pDevice = Muyo::GetRenderDevice();

    Muyo::InitializeDeviceWithValidation(pDevice, {});
    REQUIRE(pDevice->GetInstance() != VK_NULL_HANDLE);
    REQUIRE(pDevice->HasDebugMessenger());
    REQUIRE(pDevice->IsValidationLayerEnabled());
    const auto& extensions = pDevice->GetEnabledInstanceExtensions();
    CHECK(std::find(extensions.begin(), extensions.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) != extensions.end());

    CHECK_THROWS_WITH(pDevice->Initialize({}, {}),
                      Catch::Matchers::ContainsSubstring("called on an initialized device"));
    // The failed call left the live instance and its messenger alone.
    CHECK(pDevice->GetInstance() != VK_NULL_HANDLE);
    CHECK(pDevice->HasDebugMessenger());
    CHECK(pDevice->IsValidationLayerEnabled());

    // An explicit teardown allows a fresh, unvalidated instance; the old instance is gone and the new
    // one has no messenger or validation layer.
    pDevice->Unintialize();
    pDevice->Initialize({}, {});
    CHECK(!pDevice->HasDebugMessenger());
    CHECK(!pDevice->IsValidationLayerEnabled());
    CHECK(pDevice->GetEnabledInstanceLayers().empty());
    CHECK(pDevice->GetEnabledInstanceExtensions().empty());

    pDevice->CreateDevice({}, {}, nullptr, {});
    CHECK(pDevice->GetDevice() != VK_NULL_HANDLE);
    CHECK_THROWS_WITH(pDevice->Unintialize(), Catch::Matchers::ContainsSubstring("logical device exists"));
}

TEST_CASE("GraphicsTestEnv: a constructor throw tears down what it created", "[RenderDevice]")
{
    // #278. The whole fixture setup runs in the constructor, so a throw after the instance, surface or
    // device exists used to skip the destructor. The next fixture's `Initialize` then refused the live
    // instance, turning one device-capability failure into a failure of every later case. The two seams
    // below force the partial states that matter: `CreateDevice` throws (instance and surface exist, no
    // device), and the constructor throws once the device exists but before any pool or manager.
    using ConstructionFailure = Muyo::GraphicsTestEnv::ConstructionFailure;
    Muyo::VkRenderDevice* pDevice = Muyo::GetRenderDevice();

    SECTION("CreateDevice throws: the instance and its surface are torn down")
    {
        Muyo::GraphicsTestEnv::FailNextConstructionForTest(ConstructionFailure::DeviceCreation);
        CHECK_THROWS_AS(Muyo::GraphicsTestEnv(), std::runtime_error);
    }
    SECTION("a throw after device creation: the device is torn down as well")
    {
        Muyo::GraphicsTestEnv::FailNextConstructionForTest(ConstructionFailure::AfterDeviceCreation);
        CHECK_THROWS_AS(Muyo::GraphicsTestEnv(), std::runtime_error);
    }

    // Nothing is left live. The surface matters twice: leaking it would leave the instance as live as it
    // was, and destroying the instance while the surface exists is invalid usage
    // (VUID-vkDestroyInstance-instance-00629), which the validation callback aborts on.
    CHECK(pDevice->GetDevice() == VK_NULL_HANDLE);
    CHECK(pDevice->GetInstance() == VK_NULL_HANDLE);
    CHECK(pDevice->GetPhysicalDevice() == VK_NULL_HANDLE);

    // The next fixture's construction is the call that used to fail with "called on an initialized
    // device". A full environment built and destroyed afterwards is the proof, not a flag reset.
    {
        Muyo::GraphicsTestEnv env;
        CHECK(pDevice->GetDevice() != VK_NULL_HANDLE);
        CHECK(pDevice->GetInstance() != VK_NULL_HANDLE);
    }
    CHECK(pDevice->GetInstance() == VK_NULL_HANDLE);
}

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

TEST_CASE("VkRenderDevice: a caller-supplied mesh-shader chain still reaches the stage filter", "[RenderDevice]")
{
    // #265. `helloVulkan` stopped requesting VK_EXT_mesh_shader because no built shader declares a mesh
    // stage, but the feature is not dead in the device layer: `RecordEnabledStages` reads the caller's
    // chain, so a host that builds its own mesh pipeline still gets the stage bits it asked for. This pins
    // that half, because the branch would otherwise look deletable now that the engine no longer asks for
    // it itself. A device without the extension skips with its reason; the claim is about the filter, not
    // about this machine.
    ScopedDeviceLifetime lifetime;
    Muyo::VkRenderDevice* pDevice = Muyo::GetRenderDevice();
    Muyo::InitializeDeviceWithValidation(pDevice, {});

    const VkPhysicalDevice physicalDevice = pDevice->GetPhysicalDevice();
    uint32_t nExtensionCount = 0;
    Muyo::VK_ASSERT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, nullptr));
    std::vector<VkExtensionProperties> extensions(nExtensionCount);
    Muyo::VK_ASSERT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, extensions.data()));
    if (!Muyo::HasDeviceExtension(extensions, VK_EXT_MESH_SHADER_EXTENSION_NAME))
    {
        SKIP("this device does not expose " << VK_EXT_MESH_SHADER_EXTENSION_NAME
                                            << "; the caller-supplied chain cannot be exercised");
    }

    VkPhysicalDeviceMeshShaderFeaturesEXT supportedMesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supported.pNext = &supportedMesh;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
    if (supportedMesh.meshShader != VK_TRUE)
    {
        SKIP("this device exposes " << VK_EXT_MESH_SHADER_EXTENSION_NAME << " but reports meshShader = VK_FALSE");
    }

    VkPhysicalDeviceMeshShaderFeaturesEXT requestedMesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    requestedMesh.meshShader = VK_TRUE;
    pDevice->CreateDevice({VK_EXT_MESH_SHADER_EXTENSION_NAME}, {}, nullptr, {&requestedMesh});

    const VkPipelineStageFlags2 enabled = pDevice->EnabledPipelineStages();
    CHECK((enabled & VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT) != 0);
    // Only meshShader was set: taskShader stays off, so the task bit must not appear.
    CHECK((enabled & VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT) == 0);
    // The engine's own always-on bits are unaffected by a caller-supplied feature struct.
    CHECK((enabled & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) != 0);
}

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "VkRenderDevice: the cooperative-matrix query agrees with the device",
                 "[RenderDevice]")
{
    // #63. The query reads two things and either can be wrong in a way that still compiles: reading the
    // wrong feature struct, or treating the extension as sufficient when the feature is what gates it. So
    // the answer is compared against the device's own extension list rather than trusted.
    Muyo::VkRenderDevice* pDevice = Muyo::GetRenderDevice();

    // #157: on some drivers the pinned device stops being enumerated after enough instance re-creations and
    // PickPhysicalDevice falls back to enumeration order, so the run continues on a different device. This
    // case's claim is per-device and applies only while the fixture is still on the device the run started
    // on; `VkRenderDevice` also keeps the previous device's cooperative-matrix capability across that switch,
    // so the agreement check below would report the fallback rather than a disagreement on one device.
    const std::string& sRunDeviceName = Muyo::GraphicsTestEnv::GetFirstDeviceName();
    if (pDevice->GetPhysicalDeviceName() != sRunDeviceName)
    {
        SKIP(
            "the process fell back to a different physical device mid-run (#157); a per-device claim covers "
            "only the run before the fallback");
    }

    const VkPhysicalDevice physicalDevice = pDevice->GetPhysicalDevice();

    uint32_t nExtensionCount = 0;
    Muyo::VK_ASSERT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, nullptr));
    std::vector<VkExtensionProperties> extensions(nExtensionCount);
    Muyo::VK_ASSERT(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, extensions.data()));

    const bool bExposesExtension = Muyo::HasDeviceExtension(extensions, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);

    // A device without the extension can never be reported as supporting it. The converse - extension
    // present but the feature false - is possible in principle, so it is checked rather than assumed, and
    // a device that does it is worth knowing about because the extension would then be enabled for nothing.
    // CHECK(!x), not CHECK_FALSE(x): Catch2's CHECK_FALSE expands through a flag OR that the clang-analyzer
    // EnumCastOutOfRange check reports on a changed line. The assertion is the same.
    if (!bExposesExtension) CHECK(!pDevice->IsCooperativeMatrixSupported());
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
        Muyo::SpirvCode spirv = Muyo::ReadSpirvFile(shaderPath);
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

TEST_CASE("VkRenderDevice: debug markers label the device passed in, not the singleton", "[RenderDevice]")
{
    // #274. `beginMarker`/`endMarker` used to resolve `GetRenderDevice()->GetDevice()`. With the singleton
    // uninitialized that is VK_NULL_HANDLE, so `vkGetDeviceProcAddr` returns null and the label is silently
    // never recorded; with a different live device it labels the wrong one. This test makes the label
    // observable through the validation layer, which reports the open command-buffer labels with an error
    // raised inside the scope. On the old code the capture records no label and the CHECK below fails.
    //
    // The singleton is required to be uninitialized because that is what makes the old path a no-op rather
    // than a call into some other test's device. Every preceding case tears its device down, so this holds
    // whether the binary is run whole or through ctest one case at a time.
    REQUIRE(Muyo::GetRenderDevice()->GetDevice() == VK_NULL_HANDLE);

    ClientDeviceForMarkers client;

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Muyo::VK_ASSERT(vkBeginCommandBuffer(client.commandBuffer, &beginInfo));
    {
        using namespace Muyo;
        SCOPED_MARKER(client.device.GetDevice(), client.commandBuffer, "client-device-marker");

        // VUID-VkViewport-width-01770: the layer reports this while the label is open, and a zero-width
        // viewport is safe to hand the driver - unlike a null pViewports, which the driver dereferences
        // after the layer has already reported it.
        VkViewport viewport = {};
        vkCmdSetViewport(client.commandBuffer, 0, 1, &viewport);
    }
    Muyo::VK_ASSERT(vkEndCommandBuffer(client.commandBuffer));

    CHECK(std::find(client.capture.cmdBufLabels.begin(), client.capture.cmdBufLabels.end(), "client-device-marker") !=
          client.capture.cmdBufLabels.end());
}

TEST_CASE("VkRenderDevice: CreateShaderModule creates the module on the device passed in", "[RenderDevice]")
{
    // #276. The old helper called `vkCreateShaderModule(GetRenderDevice()->GetDevice(), ...)`. With the
    // singleton uninitialized that device is VK_NULL_HANDLE, which the loader rejects before the driver is
    // reached (and VK_ASSERT would abort on any error result), so a client-constructed device could not
    // create a module at all. Passing this test requires the module to have been created on the device
    // given to the helper; the destroy below uses that same device.
    REQUIRE(Muyo::GetRenderDevice()->GetDevice() == VK_NULL_HANDLE);

    ClientDevice client;
    const std::vector<char> code = Muyo::ReadSpv("shaders/linearizeDepth.comp.spv");
    REQUIRE(!code.empty());
    REQUIRE(code.size() % sizeof(uint32_t) == 0);

    const VkShaderModule module = Muyo::CreateShaderModule(client.device.GetDevice(), code);
    CHECK(module != VK_NULL_HANDLE);
    vkDestroyShaderModule(client.device.GetDevice(), module, nullptr);
}
