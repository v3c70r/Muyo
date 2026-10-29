#include <vulkan/vulkan_core.h>

#include <ostream>
#include <stdexcept>
#include <tuple>
#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <optional>
#include <span>
#include <vector>

#include "../thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Camera.h"
#include "Debug.h"
#include "DescriptorHeapService.h"
#include "DescriptorManager.h"
#include "EventSystem.h"
#include "FrameSync.h"
#include "Geometry.h"
#include "ImGuiControl.h"
#include "Material.h"
#include "MeshResourceManager.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderPassManager.h"
#include "RenderResourceManager.h"
#include "RenderResourceNames.h"
#include "SamplerManager.h"
#include "SceneImporter.h"
#include "SceneManager.h"
#include "Swapchain.h"
#include "Texture.h"
#include "UniformBuffer.h"
#include "VkExtFuncsLoader.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

#if defined(USE_SDL)
#include "WindowSDL.h"
#else
#include "WindowGLFW.h"
#endif

#ifdef FEATURE_RAY_TRACING
#include "RayTracingSceneManager.h"
#endif

using namespace Muyo;
using namespace Muyo::RenderGraph;

bool g_bWaylandExt = false;
const int WIDTH = 1920;
const int HEIGHT = 1080;

// The graph's FINAL node needs the acquired image to be available, and the legacy scene still writes one
// shared composite that the node samples. With more than one execution in flight, frame N+1's scene submit
// could overwrite that composite while frame N's node is still sampling it; one slot serialises the pair and
// is correct until OPAQUE_LIGHTING/UI port and the composite becomes a per-slot graph transient (#20).
constexpr uint32_t kImagesInFlight = 1;
// Same format and mode the legacy manager used, so the port does not change what the surface presents.
constexpr VkSurfaceFormatKHR kSwapchainFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
constexpr VkPresentModeKHR kPresentMode = VK_PRESENT_MODE_FIFO_KHR;

static int s_framebufferWidth = WIDTH;
static int s_framebufferHeight = HEIGHT;

///
// Arcball callbacks
static void clickArcballCallback(int button, int action)
{
    Arcball *pArcball = dynamic_cast<Arcball *>(GetRenderPassManager()->GetCamera());
    if (pArcball)
    {
        if (button == Input::Button::BUTTON_LEFT)
        {
            if (EventState::PRESSED == action)
            {
                pArcball->StartDragging();
            }
            else if (EventState::RELEASED == action)
            {
                pArcball->StopDragging();
            }
        }
    }
}

static void rotateArcballCallback(double xpos, double ypos)
{
    Arcball *pArcball = dynamic_cast<Arcball *>(GetRenderPassManager()->GetCamera());
    if (pArcball)
    {
        pArcball->UpdateDrag(glm::vec2(xpos, ypos));
    }
}

///////////////////////////////////////////////////////////////////////////////
// event callbacks
///////////////////////////////////////////////////////////////////////////////

static void InitEventHandlers()
{
    auto pMove = EventSystem::sys()->globalEvent<EventType::MOUSEMOTION, GlobalMotionEvent>();
    pMove->Watch([](uint32_t, float sx, float sy) { rotateArcballCallback(sx, sy); });

    auto pBtn = EventSystem::sys()->globalEvent<EventType::MOUSEBUTTON, GlobalButtonEvent>();
    pBtn->Watch([](uint32_t, Input::Button btn, EventState state) { clickArcballCallback(btn, state); });

    auto pWheel = EventSystem::sys()->globalEvent<EventType::MOUSEWHEEL, GlobalWheelEvent>();

    pWheel->Watch(
        [](uint32_t, double, double yoffset)
        {
            if (Arcball *pArcball = dynamic_cast<Arcball *>(GetRenderPassManager()->GetCamera()))
            {
                pArcball->AddZoom(yoffset * -0.1f);
            }
        });

    auto pResize = EventSystem::sys()->globalEvent<EventType::WINDOWRESIZE, GlobalResizeEvent>();
}

///////////////////////////////////////////

std::vector<const char *> GetRequiredInstanceExtensions()
{
    std::vector<const char *> vExtensions;
    uint32_t countExtensions = 0;

    countExtensions = Window::GetVulkanInstanceExtensions(vExtensions);
    if (countExtensions == 0 || vExtensions.size() == 0)
        throw std::runtime_error("Failed to query instance extensions");
    // const char *waylandExt = "VK_KHR_wayland_surface";
    // query if we have wayland surface, nvidia GPU will not work with it
    // bool waylandSurface = std::find_if(std::begin(vExtensions),
    //                               std::end(vExtensions),
    //                               [waylandExt](const char *ext) {
    //                                   return std::strcmp(ext, waylandExt) == 0;
    //                               }) != std::end(vExtensions);

    // This exteinsion is required by logical device's multiview extension
    vExtensions.push_back("VK_KHR_get_physical_device_properties2");
    return vExtensions;
}

std::vector<const char *> GetRequiredDeviceExtensions()
{
    std::vector<const char *> vDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,

        VK_KHR_MULTIVIEW_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,

        // The graph path is descriptor-heap-only (B1b). Requesting both extensions here makes a device
        // without them fail at creation with the missing extension named, rather than at the first graph
        // node's Build(). CreateDevice appends and verifies the matching feature structs.
        VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
        VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,

#ifdef FEATURE_RAY_TRACING
        // Ray tracing extensions
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    // VK_EXT_descriptor_indexing is core in Vulkan 1.2. Enabling the extension while
    // chaining VkPhysicalDeviceVulkan12Features requires descriptorIndexing = VK_TRUE
    // (VUID-VkDeviceCreateInfo-ppEnabledExtensionNames-02833), so rely on the core feature instead.
#endif
    };
    return vDeviceExtensions;
}

void cleanup()
{
    GetDescriptorManager()->DestroyDescriptorSetLayouts();
    GetDescriptorManager()->DestroyDescriptorPool();

    GetRenderPassManager()->Unintialize();
    GetRenderDevice()->DestroyCommandPools();
    GetRenderResourceManager()->Unintialize();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
    Window::Uninitialize();
}

void updateUniformBuffer(UniformBuffer<PerViewData> *) {
    // s_arcball.UpdatePerViewDataUBO(ub);
};

int main(int argc, char **argv)
{
    // Load mesh into memory
    if (!Window::Initialize("hello Vulkan", WIDTH, HEIGHT)) return -1;

    // Create Instace
    std::vector<const char *> vInstanceExtensions = GetRequiredInstanceExtensions();
    GetRenderDevice()->Initialize(vInstanceExtensions, {});
    VkExt::LoadInstanceFunctions(GetRenderDevice()->GetInstance());

    // Create swapchain
    VkSurfaceKHR surface = Window::GetVulkanSurface(GetRenderDevice()->GetInstance());

    // Create device
    // Included in features12
    // VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatrues = {};
    // bufferDeviceAddressFeatrues.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
    // bufferDeviceAddressFeatrues.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeature = {};
    rayTracingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingFeature.rayTracingPipeline = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accStructFeature = {};
    accStructFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accStructFeature.accelerationStructure = VK_TRUE;

    // Mesh shader feature
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeature = {};
    meshShaderFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshShaderFeature.taskShader = VK_FALSE;
    meshShaderFeature.meshShader = VK_TRUE;
    meshShaderFeature.multiviewMeshShader = VK_FALSE;
    meshShaderFeature.primitiveFragmentShadingRateMeshShader = VK_FALSE;
    meshShaderFeature.meshShaderQueries = VK_FALSE;

    std::vector<void *> features;
    // features.push_back(&bufferDeviceAddressFeatrues);
    if (GetRenderDevice()->IsRayTracingSupported())
    {
        features.push_back(&rayTracingFeature);
        features.push_back(&accStructFeature);
    }

    features.push_back(&meshShaderFeature);
    GetRenderDevice()->CreateDevice(GetRequiredDeviceExtensions(),  // Extensions
                                    std::vector<const char *>(),    // Layers
                                    &surface, features);

    GetMemoryAllocator()->Initalize(GetRenderDevice());

    GetRenderDevice()->CreateCommandPools();

    // Initialize managers
    GetDescriptorManager()->CreateDescriptorPool();
    GetDescriptorManager()->CreateDescriptorSetLayouts();

    GetSamplerManager()->createSamplers();

    InitEventHandlers();

    {
        bool bFileFromArg = false;
        if (argc == 2)
        {
            namespace fs = std::filesystem;
            fs::path path(argv[1]);
            if (fs::exists(path))
            {
                std::string sPath{path.relative_path().string()};
                GetSceneManager()->LoadSceneFromFile(sPath);
                bFileFromArg = true;
            }
        }

        if (!bFileFromArg)
        {
            // Load scene
            // GetSceneManager()->LoadSceneFromFile("assets/triangle/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5/scene.gltf");
            GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5_spot/untitled.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/mazda_mx-5_spotlight/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/balls/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/Cars/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/Emissive/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/Cornell_box_Emissive/untitled.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/Cornell_box/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/Cornell_box_spot/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/sofa_combination/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/StudioSetup/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/shiba/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/catherdral/scene.gltf");
            // GetSceneManager()->LoadSceneFromFile("C:/Users/mcvec/source/repos/v3c70r/Muyo/assets/glTF-Sample-Models/2.0/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf");
            // GetSceneManager()->LoadSceneFromFile("C:/Users/mcvec/Documents/cars/New Folder/cars.gltf");
            // GetSceneManager()->LoadSceneFromFile("assets/TestAssets/EnvironmentTest/glTF/EnvironmentTest.gltf");
        }

        // Finalize material buffer
        GetMaterialManager()->CreateDefaultMaterial();
        GetMaterialManager()->UploadMaterialBuffer();

        // update framebuffer size
        Window::GetFramebufferSize(s_framebufferWidth, s_framebufferHeight);
        GetRenderPassManager()->Initialize(s_framebufferWidth, s_framebufferHeight);
        DrawLists dl = GetSceneManager()->GatherDrawLists();
        GetSceneManager()->ConstructLightBufferFromDrawLists(dl);

        // Registration, not a lookup: the passes fetch "perView" by name, so the call has to happen
        // even though this pointer is not read (#25).
        (void)GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");

        // Load materials

        // Test ray tracing
#ifdef FEATURE_RAY_TRACING

        RayTracingSceneManager RTSceneManager;
        RTSceneManager.BuildScene(dl.m_aDrawLists[DrawLists::DL_OPAQUE]);
        GetRenderPassManager()->SetRayTracingSceneManager(&RTSceneManager);
        // Allocate ray tracing descriptor layout based on number of textures in scene.
        // GetDescriptorManager()->CreateRayTracingDescriptorLayout(GetTextureResourceManager()->GetTextures().size());

        // RTBuilder rayTracingBuilder;
        // if (GetRenderDevice()->IsRayTracingSupported())
        //{
        //     // Test ray tracing
        //     RTInputs rtInputs;
        //     rtInputs = ConstructRTInputsFromDrawLists(dl);
        //     rayTracingBuilder.BuildBLAS(
        //         rtInputs.BLASs,
        //         VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
        //         VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
        //     rayTracingBuilder.BuildTLAS(
        //         rtInputs.vInstances,
        //         VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
        //         VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

        //    // Allocate primitive description buffer
        //    StorageBuffer<PrimitiveDescription>* primDescBuffer =
        //    GetRenderResourceManager()->GetStorageBuffer("primitive descs", rtInputs.vPrimitiveDescriptions);

        //    // Allocate output image
        //    ImageResource* rtOutputImage = GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Output",
        //    vpExtent, VK_FORMAT_R16G16B16A16_SFLOAT);

        //    rayTracingBuilder.BuildRTPipeline();
        //    rayTracingBuilder.BuildShaderBindingTable();
        //    rayTracingBuilder.RayTrace(vpExtent);
        //}
#endif
        // Create perview constant buffer
        //

        // Record static command buffer
        GetRenderPassManager()->RecordStaticCmdBuffers(dl);

        // ── The graph owns the frame loop and the present from here (#20 increment 1) ────────────────
        {
            int initialWidth = 0;
            int initialHeight = 0;
            Window::GetFramebufferSize(initialWidth, initialHeight);
            Swapchain swapchain;
            // The window owns the surface, so the swapchain must not destroy it; the app destroys it below.
            swapchain.CreateSwapchain(
                surface, kSwapchainFormat, kPresentMode, kImagesInFlight, /*bOwnSurface=*/false,
                VkExtent2D{static_cast<uint32_t>(initialWidth), static_cast<uint32_t>(initialHeight)});
            FrameSync frameSync(GetRenderDevice(), &swapchain, kImagesInFlight);

            const ImageResource *pComposite =
                GetRenderResourceManager()->GetResource<ImageResource>(OPAQUE_LIGHTING_OUTPUT_ATTACHMENT_NAME);
            if (pComposite == nullptr)
            {
                throw std::runtime_error("helloVulkan: the scene's composite target was not created.");
            }
            const MeshVertexResources &meshResources = GetMeshResourceManager()->GetMeshVertexResources();
            if (meshResources.m_pVertexBuffer == nullptr || meshResources.m_pIndexBuffer == nullptr)
            {
                throw std::runtime_error("helloVulkan: the quad mesh buffers were not uploaded.");
            }
            const Mesh &quad = GetMeshResourceManager()->GetQuad();

            // The builder's slots and the heap's per-execution rings must agree, so configure the heap from
            // the same constant.
            DescriptorHeapService heap;
            heap.Initialize(GetRenderDevice(), DescriptorHeapService::Config{.inFlightCount = kImagesInFlight});
            // Match the legacy FINAL pass's sampler (SAMPLER_1_MIPS): linear filtering, clamp, one mip.
            VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxLod = 1.0F;
            const DescriptorSlot compositeSamplerSlot = heap.AcquireSampler(samplerInfo);

            // One builder for the process: Build() is per frame, and the resource/pipeline/descriptor caches
            // make an unchanged declaration cheap. Only the acquired swapchain image changes per frame.
            RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
                                                           .resourceManager = GetRenderResourceManager(),
                                                           .descriptorHeap = &heap},
                                       kImagesInFlight);
            // The composite is produced by the legacy scene outside the graph, and the UI pass leaves it in
            // SHADER_READ_ONLY_OPTIMAL. Stating that layout is what keeps the graph's first-use barrier from
            // treating the content as UNDEFINED and discarding it.
            builder.ImportResource("Composite", pComposite, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            builder.ImportResource("FinalVertices", meshResources.m_pVertexBuffer);
            builder.ImportResource("FinalIndices", meshResources.m_pIndexBuffer);
            // Image 0 is only the declaration-time placeholder; every frame re-imports the acquired image.
            builder.ImportResource("Present", swapchain.GetImageResource(0));
            builder.AddNode(RenderGraphNodeCreateInfo{
                .nodeName = "Final",
                .queueType = QueueType::GRAPHICS,
                .resourceUses =
                    {
                        ResourceUse{.handle = ResourceHandle("Composite"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("FinalVertices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::VERTEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("FinalIndices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::INDEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("Present"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::PRESENT,
                                    .kind = ResourceKind::IMAGE},
                    },
                .shaderNames = {"triangle.vert", "final.frag.heap"},
                .psoDesc = {.vertexType = VertexType::GENERIC,
                            .rasterState = {.cullMode = CullMode::NONE},
                            // The quad is a blit; it has no depth attachment.
                            .depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false},
                            // One colour attachment and no blending: the node overwrites every pixel.
                            .blendState = {.attachmentCount = 1}},
                .attachmentClearValues = {{{.color = {0.0F, 0.0F, 0.0F, 1.0F}}}},
                .execute = [indexCount = quad.m_nIndexCount, indexOffset = quad.m_nIndexOffset,
                            compositeSamplerSlot](RenderGraphNodeContext &ctx)
                {
                    struct FinalPush
                    {
                        uint32_t image;
                        uint32_t sampler;
                    };
                    const std::optional<uint32_t> composite = ctx.GetHeapIndex("Composite", ResourceUsage::SAMPLED);
                    if (!composite.has_value())
                    {
                        throw std::runtime_error("helloVulkan: the FINAL node's composite has no heap index.");
                    }
                    const FinalPush push{*composite, compositeSamplerSlot.index};
                    // The graph requires the push block on every execution, even one that draws nothing.
                    ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

                    const auto *pVertices = dynamic_cast<const BufferResource *>(ctx.resolveResource("FinalVertices"));
                    const auto *pIndices = dynamic_cast<const BufferResource *>(ctx.resolveResource("FinalIndices"));
                    if (pVertices == nullptr || pIndices == nullptr) return;
                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = pVertices->buffer();
                    vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(ctx.commandBuffer, pIndices->buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(ctx.commandBuffer, indexCount, 1, indexOffset, 0, 0);
                }});

            // Drain, recreate the swapchain at the window's new size, and rebuild the graph's per-execution
            // device objects. The legacy scene targets stay at their creation extent until their passes port;
            // the graph stretches the composite into the new swapchain, and #186 owns the camera aspect.
            const auto recreateSwapchain = [&]()
            {
                VK_ASSERT(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()));
                const VkSurfaceKHR reclaimed = swapchain.GetSurface();
                swapchain.DestroySwapchain();
                int width = 0;
                int height = 0;
                Window::GetFramebufferSize(width, height);
                swapchain.CreateSwapchain(reclaimed, kSwapchainFormat, kPresentMode, kImagesInFlight,
                                          /*bOwnSurface=*/false,
                                          VkExtent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
                frameSync.OnSwapchainRecreated();
                builder.ReleaseCachedDeviceObjects();
            };

            while (!Window::ShouldQuit())
            {
                Window::ProcessEvents();

                int framebufferWidth = 0;
                int framebufferHeight = 0;
                Window::GetFramebufferSize(framebufferWidth, framebufferHeight);
                if (framebufferWidth > 0 && framebufferHeight > 0 &&
                    (static_cast<uint32_t>(framebufferWidth) != swapchain.GetSwapchainExtent().width ||
                     static_cast<uint32_t>(framebufferHeight) != swapchain.GetSwapchainExtent().height))
                {
                    recreateSwapchain();
                }

                // BeginFrame waits the slot's previous execution, which is what makes re-recording the UI
                // command buffer and re-uploading `perView` safe with one slot in flight.
                const std::optional<FrameSync::Frame> frame = frameSync.BeginFrame();
                if (!frame.has_value())
                {
                    // Out of date (a resize).
                    recreateSwapchain();
                    continue;
                }

                // The scene still records through the legacy manager; it no longer acquires or presents.
                GetRenderPassManager()->UpdatePerViewData();
                GetRenderPassManager()->RecordDynamicCmdBuffers();
                GetRenderPassManager()->SubmitSceneCommandBuffers(frame->imageAvailable);

                builder.ImportResource("Present", swapchain.GetImageResource(frame->imageIndex));
                builder.Build();
                // The graph waits on the scene's completion (which consumed the acquire) and signals the
                // frame's renderFinished + fence, which is what FrameSync presents and reuses.
                RenderGraphExecuteInfo executeInfo = frameSync.ExecuteInfo(*frame);
                executeInfo.waitSemaphore = GetRenderPassManager()->GetSceneFinishedSemaphore();
                builder.Execute(executeInfo);

                const VkResult presentResult = frameSync.Present(*frame);
                if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
                {
                    recreateSwapchain();
                }

                ImGui::Update();
            }

            VK_ASSERT(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()));
            swapchain.DestroySwapchain();
        }

        // The app owns the surface now (the swapchain was created with bOwnSurface=false). It must go before
        // the instance, or validation reports it as a child object left behind.
        vkDestroySurfaceKHR(GetRenderDevice()->GetInstance(), surface, nullptr);

        std::cout << "Closing window, wait for device to finish..." << std::endl;
        VK_ASSERT(vkDeviceWaitIdle(GetRenderDevice()->GetDevice()));
        std::cout << "Device finished" << std::endl;

        // Destroy managers
        GetMaterialManager()->DestroyMaterials();
        GetGeometryManager()->Destroy();
        GetSamplerManager()->destroySamplers();
        GetTextureResourceManager()->Destroy();
    }
    ImGui::Shutdown();
    cleanup();
    return 0;
}
