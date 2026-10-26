#pragma once
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "DescriptorManager.h"
#include "Material.h"
#include "MeshResourceManager.h"
#include "PerObjResourceManager.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "SceneManager.h"
#include "VkExtFuncsLoader.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{
// RAII graphics environment
class GraphicsTestEnv
{
public:
    GraphicsTestEnv()
    {
        // The test host creates a headless surface when the loader offers one, so the device is given a
        // real present queue and the frame-sync/present path is exercisable in CI. Where the extension
        // is absent the surface stays null and the device is created exactly as before.
        const bool bHeadlessSurface = HasInstanceExtension(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME) &&
                                      HasInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME);
        std::vector<const char *> instanceExtensions;
        if (bHeadlessSurface)
        {
            instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            instanceExtensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
        }
        GetRenderDevice()->Initialize(instanceExtensions, {});
#ifdef FEATURE_RAY_TRACING
        // Device-level ray tracing entry points are resolved through the instance.
        VkExt::LoadInstanceFunctions(GetRenderDevice()->GetInstance());
#endif
        if (bHeadlessSurface)
        {
            const auto pfnCreateHeadlessSurface = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
                vkGetInstanceProcAddr(GetRenderDevice()->GetInstance(), "vkCreateHeadlessSurfaceEXT"));
            if (pfnCreateHeadlessSurface != nullptr)
            {
                VkHeadlessSurfaceCreateInfoEXT surfaceInfo{VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
                pfnCreateHeadlessSurface(GetRenderDevice()->GetInstance(), &surfaceInfo, nullptr, &m_surface);
            }
        }

        // Request the heap extension chain only when the selected device reports both extensions and both
        // features. A test host that requested them unconditionally would turn an unsupported CI device
        // into a red build before the probe could record the unsupported row; the probe itself skips with
        // the reason, and the [test device] line says whether this run exercised the heap path.
        std::vector<const char *> deviceExtensions = GetTestDeviceExtensions();
        std::vector<void *> deviceFeatures = GetTestDeviceFeatures();
        if (DeviceSupportsDescriptorHeap())
        {
            deviceExtensions.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME);
        }
        if (m_surface != VK_NULL_HANDLE) deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        const VkSurfaceKHR *pSurface = m_surface != VK_NULL_HANDLE ? &m_surface : nullptr;
        GetRenderDevice()->CreateDevice(deviceExtensions, std::vector<const char *>(), pSurface, deviceFeatures);
        // The loader can offer the extension while the chosen device does not support the surface (AMDVLK
        // does not, Lavapipe does). `CreateDevice` then selects no present family; drop the surface so
        // present-path tests report their scope instead of using an unsupported one.
        if (m_surface != VK_NULL_HANDLE && GetRenderDevice()->GetPresentQueue() == VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(GetRenderDevice()->GetInstance(), m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }

        // Say which device this run is on, once. A "tests passed" line does not distinguish a
        // driver from a software rasteriser, and this suite has silently run on both for the same
        // commit; verification tables record the device name for that reason (AGENTS.md section 4).
        // The topology note matters too: a device with a single queue family exercises none of the
        // cross-queue code, so a green run there is not evidence about it. Driver, Mesa/loader and the
        // heap/untyped feature bits are recorded beside it because B0 is a go/no-go and an unsupported
        // row has to be visible in the run that did not exercise it.
        static bool bLoggedDevice = false;
        if (!bLoggedDevice)
        {
            bLoggedDevice = true;
            VkPhysicalDeviceProperties deviceProperties{};
            vkGetPhysicalDeviceProperties(GetRenderDevice()->GetPhysicalDevice(), &deviceProperties);
            VkPhysicalDeviceDriverProperties driverProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
            VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties2.pNext = &driverProperties;
            vkGetPhysicalDeviceProperties2(GetRenderDevice()->GetPhysicalDevice(), &properties2);

            VkRenderDevice *pDevice = GetRenderDevice();
            const VkPhysicalDeviceDescriptorHeapPropertiesEXT &heapProperties = pDevice->GetDescriptorHeapProperties();
            std::cerr
                << "[test device] " << deviceProperties.deviceName << " (Vulkan "
                << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "." << VK_VERSION_MINOR(deviceProperties.apiVersion)
                << "." << VK_VERSION_PATCH(deviceProperties.apiVersion) << ", driver=" << driverProperties.driverName
                << " " << driverProperties.driverInfo << ", loader=" << VK_VERSION_MAJOR(pDevice->GetInstanceVersion())
                << "." << VK_VERSION_MINOR(pDevice->GetInstanceVersion()) << "."
                << VK_VERSION_PATCH(pDevice->GetInstanceVersion())
                << ") - heap: descriptorHeap=" << (pDevice->IsDescriptorHeapSupported() ? "true" : "false") << " ("
                << (pDevice->IsDescriptorHeapEnabled()
                        ? "enabled"
                        : (pDevice->IsDescriptorHeapSupported() ? "available, not requested" : "unsupported"))
                << ")" << ", shaderUntypedPointers=" << (pDevice->IsShaderUntypedPointersSupported() ? "true" : "false")
                << " ("
                << (pDevice->IsShaderUntypedPointersEnabled()
                        ? "enabled"
                        : (pDevice->IsShaderUntypedPointersSupported() ? "available, not requested" : "unsupported"))
                << ")" << ", sizes sampler/image/buffer/AS=" << pDevice->GetDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER)
                << "/" << pDevice->GetDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) << "/"
                << pDevice->GetDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) << "/"
                << pDevice->GetDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                << ", maxPushData=" << heapProperties.maxPushDataSize << " - "
                << (pDevice->IsComputeQueueDedicated() ? "dedicated compute family present, cross-queue path exercised"
                                                       : "single queue family, cross-queue path NOT exercised")
                << std::endl;
        }
        GetRenderDevice()->CreateCommandPools();
        GetMemoryAllocator()->Initalize(GetRenderDevice());
        GetRenderResourceManager()->Initialize();
        GetDescriptorManager()->CreateDescriptorPool();
        GetDescriptorManager()->CreateDescriptorSetLayouts();
        GetSamplerManager()->createSamplers();
    }
    ~GraphicsTestEnv()
    {
        GetSamplerManager()->destroySamplers();
        GetTextureResourceManager()->Destroy();
        GetDescriptorManager()->DestroyDescriptorSetLayouts();
        GetDescriptorManager()->DestroyDescriptorPool();
        GetRenderResourceManager()->Unintialize();
        GetRenderDevice()->DestroyCommandPools();
        GetMemoryAllocator()->Unintialize();
        GetRenderDevice()->DestroyDevice();
        if (m_surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(GetRenderDevice()->GetInstance(), m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
        GetRenderDevice()->Unintialize();
    }

    /// @return The headless surface the device was created with, or `VK_NULL_HANDLE` where the loader
    ///         offers no `VK_EXT_headless_surface`. The frame-sync tests create a swapchain against it;
    ///         a test that needs it must state its scope rather than pass quietly on a machine without.
    VkSurfaceKHR GetHeadlessSurface() const { return m_surface; }

    /// @return True when this run has a headless surface, so a present-path test can run at all.
    bool HasHeadlessSurface() const { return m_surface != VK_NULL_HANDLE; }

protected:
    DrawLists m_mDrawList;

private:
    /// Does the loader offer this instance extension? Asked before `vkCreateInstance`, because enabling
    /// an absent one fails instance creation for every test rather than only the surface ones.
    static bool HasInstanceExtension(const char *name)
    {
        uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return false;
        std::vector<VkExtensionProperties> props(count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS) return false;
        for (const VkExtensionProperties &prop : props)
        {
            if (std::strcmp(prop.extensionName, name) == 0) return true;
        }
        return false;
    }

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    /// @return True when the selected device exposes both heap extensions and reports both features.
    ///
    /// Checked before requesting the chain so an unsupported device keeps its existing tests; the heap
    /// probe itself skips with the same condition. The two features are queried rather than inferred from
    /// the extension strings: a device can expose an extension and report the feature false.
    static bool DeviceSupportsDescriptorHeap()
    {
        VkPhysicalDevice physicalDevice = GetRenderDevice()->GetPhysicalDevice();
        uint32_t nExtensionCount = 0;
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, nullptr) != VK_SUCCESS)
        {
            return false;
        }
        std::vector<VkExtensionProperties> extensions(nExtensionCount);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &nExtensionCount, extensions.data()) !=
            VK_SUCCESS)
        {
            return false;
        }
        const auto hasExtension = [&extensions](const char *name)
        {
            for (const VkExtensionProperties &extension : extensions)
            {
                if (std::strcmp(extension.extensionName, name) == 0) return true;
            }
            return false;
        };
        if (!hasExtension(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME) ||
            !hasExtension(VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME))
        {
            return false;
        }

        VkPhysicalDeviceDescriptorHeapFeaturesEXT heapFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT};
        VkPhysicalDeviceFeatures2 heapQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        heapQuery.pNext = &heapFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &heapQuery);

        VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untypedFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR};
        VkPhysicalDeviceFeatures2 untypedQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        untypedQuery.pNext = &untypedFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &untypedQuery);

        return heapFeatures.descriptorHeap == VK_TRUE && untypedFeatures.shaderUntypedPointers == VK_TRUE;
    }

    // Device extensions required by the tests. Ray tracing must be requested here so the
    // acceleration-structure / SBT pools created by the memory allocator are valid.
    static const std::vector<const char *> &GetTestDeviceExtensions()
    {
        static const std::vector<const char *> extensions = {
#ifdef FEATURE_RAY_TRACING
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
#endif
        };
        return extensions;
    }

    static const std::vector<void *> &GetTestDeviceFeatures()
    {
        static std::vector<void *> features = []
        {
            std::vector<void *> f;
#ifdef FEATURE_RAY_TRACING
            static VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeature = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
            static VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeature = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
            rtFeature.rayTracingPipeline = VK_TRUE;
            asFeature.accelerationStructure = VK_TRUE;
            f.push_back(&asFeature);
            f.push_back(&rtFeature);
#endif
            return f;
        }();
        return features;
    }
};

class GraphicsTestEnvMazdaScene : public GraphicsTestEnv
{
public:
    GraphicsTestEnvMazdaScene()
    {
        // SceneManager / MeshResourceManager / PerObjResourceManager are process-wide caches, while
        // the device (and therefore every GPU resource) is recreated for each test case. Load the
        // scene once and re-upload the shared buffers against the new device.
        if (GetSceneManager()->GetAllScenes().empty())
        {
            GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5_spot/untitled.gltf");
        }
        // GatherDrawLists populates the per-object data on first use.
        m_mDrawList = GetSceneManager()->GatherDrawLists();
        GetMeshResourceManager()->UploadMeshData();
        GetPerObjResourceManager()->Upload();
        GetMaterialManager()->RefreshGPUResources();
    }
};
}  // namespace Muyo
