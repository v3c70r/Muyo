#pragma once
#include <cstring>
#include <iostream>

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

        std::vector<const char *> deviceExtensions = GetTestDeviceExtensions();
        if (m_surface != VK_NULL_HANDLE) deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        const VkSurfaceKHR *pSurface = m_surface != VK_NULL_HANDLE ? &m_surface : nullptr;
        GetRenderDevice()->CreateDevice(deviceExtensions, std::vector<const char *>(), pSurface,
                                        GetTestDeviceFeatures());

        // Say which device this run is on, once. A "tests passed" line does not distinguish a
        // driver from a software rasteriser, and this suite has silently run on both for the same
        // commit; verification tables record the device name for that reason (AGENTS.md section 4).
        // The topology note matters too: a device with a single queue family exercises none of the
        // cross-queue code, so a green run there is not evidence about it.
        static bool bLoggedDevice = false;
        if (!bLoggedDevice)
        {
            bLoggedDevice = true;
            VkPhysicalDeviceProperties deviceProperties{};
            vkGetPhysicalDeviceProperties(GetRenderDevice()->GetPhysicalDevice(), &deviceProperties);
            std::cerr << "[test device] " << deviceProperties.deviceName << " (Vulkan "
                      << VK_VERSION_MAJOR(deviceProperties.apiVersion) << "."
                      << VK_VERSION_MINOR(deviceProperties.apiVersion) << "."
                      << VK_VERSION_PATCH(deviceProperties.apiVersion) << ") - "
                      << (GetRenderDevice()->IsComputeQueueDedicated()
                              ? "dedicated compute family present, cross-queue path exercised"
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
