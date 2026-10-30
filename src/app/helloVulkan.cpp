#include <vulkan/vulkan_core.h>

#include <ostream>
#include <stdexcept>
#include <tuple>
#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
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
#include "Debug/DebugUIOverlay.h"
#include "DescriptorHeapService.h"
#include "DescriptorManager.h"
#include "DrawCommandBuffer.h"
#include "EventSystem.h"
#include "FrameSync.h"
#include "Geometry.h"
#include "ImGuiControl.h"
#include "Material.h"
#include "MeshResourceManager.h"
#include "PerObjResourceManager.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderPassManager.h"
#include "RenderResourceManager.h"
#include "RenderResourceNames.h"
#include "SamplerManager.h"
#include "Scene.h"
#include "SceneImporter.h"
#include "SceneManager.h"
#include "ScenePushData.h"
#include "SharedStructures.h"
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

// The composite is a per-slot graph transient now, so the composite hazard that pinned this to 1 is gone.
// The remaining legacy bridge is not per-slot, though: one `sceneFinished` binary semaphore and one set of
// IBL/cubemap command buffers are shared by every frame. Raising this is a separate change (#20).
constexpr uint32_t kImagesInFlight = 1;
// Same format and mode the legacy manager used, so the port does not change what the surface presents.
constexpr VkSurfaceFormatKHR kSwapchainFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
constexpr VkPresentModeKHR kPresentMode = VK_PRESENT_MODE_FIFO_KHR;
// The UI node's draw data carries these ids; 0 is the font, 1 the environment-map preview.
constexpr ImTextureID kFontTextureId = 0;
constexpr ImTextureID kEnvMapTextureId = 1;

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

        // The GBUFFER node draws the opaque list with one indirect command per submesh, built once here.
        // This is the legacy RenderPassGBuffer::RecordCommandBuffers construction, moved out of the
        // deleted pass: firstInstance packs (object, submesh), which the shader unpacks again.
        std::vector<VkDrawIndexedIndirectCommand> gbufferDrawCommands;
        for (const SceneNode *pGeometryNode : dl.m_aDrawLists[DrawLists::DL_OPAQUE])
        {
            const Geometry *pGeometry = static_cast<const GeometrySceneNode *>(pGeometryNode)->GetGeometry();
            uint32_t submeshIndex = 0;
            for (const auto &pSubmesh : pGeometry->getSubmeshes())
            {
                const Mesh &mesh = GetMeshResourceManager()->GetMesh(pSubmesh->GetMeshIndex());
                VkDrawIndexedIndirectCommand drawCommand{};
                drawCommand.indexCount = mesh.m_nIndexCount;
                drawCommand.instanceCount = 1;
                drawCommand.firstIndex = mesh.m_nIndexOffset;
                drawCommand.vertexOffset = 0;
                drawCommand.firstInstance = PackSubmeshObjectIndex(pGeometryNode->GetPerObjId(), submeshIndex++);
                gbufferDrawCommands.push_back(drawCommand);
            }
        }
        // The buffer must exist even for an empty list so the node's declared use resolves; the draw
        // count, not the buffer size, says how many commands are real.
        const uint32_t gbufferDrawCount = static_cast<uint32_t>(gbufferDrawCommands.size());
        if (gbufferDrawCommands.empty()) gbufferDrawCommands.push_back(VkDrawIndexedIndirectCommand{});
        auto *pGBufferDrawCommands =
            GetRenderResourceManager()->GetDrawCommandBuffer("GBUFFER draw commands", gbufferDrawCommands);

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

            const MeshVertexResources &meshResources = GetMeshResourceManager()->GetMeshVertexResources();
            if (meshResources.m_pVertexBuffer == nullptr || meshResources.m_pIndexBuffer == nullptr)
            {
                throw std::runtime_error("helloVulkan: the quad mesh buffers were not uploaded.");
            }
            const Mesh &quad = GetMeshResourceManager()->GetQuad();

            // The debug UI is host-side: the overlay owns the ImGui context, the pages and the font
            // texture; the UI graph node composites its draw data.
            DebugUIOverlay overlay;
            overlay.RegisterPage<DockSpace>("DockSpace");
            overlay.RegisterPage<ResourceManagerDebugPage>("Render Manager Resources");
            overlay.RegisterPage<SceneDebugPage>("Loaded Scenes");
            overlay.RegisterPage<EnvironmentMapDebugPage>("Env HDRs");
            overlay.RegisterPage<LightsDebugPage>("Lights");
            overlay.RegisterPage<CameraDebugPage>("MainCamera");

            // One vertex/index pair per swapchain image: with a frame in flight, frame N+1 must not
            // overwrite the UI data frame N's GPU work is still reading.
            const std::vector<ImDrawVert> dummyVertices = {ImDrawVert{{0.0F, 0.0F}, {0.0F, 0.0F}, 0}};
            const std::vector<ImDrawIdx> dummyIndices = {0};
            std::vector<VertexBuffer<ImDrawVert> *> pUIVertices;
            std::vector<IndexBuffer *> pUIIndices;
            const auto ensureUIBuffers = [&]()
            {
                const uint32_t imageCount = swapchain.GetImageCount();
                pUIVertices.resize(imageCount, nullptr);
                pUIIndices.resize(imageCount, nullptr);
                for (uint32_t i = 0; i < imageCount; ++i)
                {
                    if (pUIVertices[i] == nullptr)
                    {
                        pUIVertices[i] = GetRenderResourceManager()->GetVertexBuffer<ImDrawVert>(
                            "UIVertexBuffer" + std::to_string(i), dummyVertices, false);
                    }
                    if (pUIIndices[i] == nullptr)
                    {
                        pUIIndices[i] = GetRenderResourceManager()->GetIndexBuffer("UIIndexBuffer" + std::to_string(i),
                                                                                   dummyIndices, false);
                    }
                }
            };
            ensureUIBuffers();

            // The builder's slots and the heap's per-execution rings must agree, so configure the heap from
            // the same constant.
            // The GBUFFER node gives every material texture one *static* sampled-image descriptor, so the
            // image region must hold the scene's textures as well as the default 256 slots. Sizing it from
            // the scene keeps a texture-heavy scene from exhausting the region where the legacy descriptor
            // array had no such limit.
            const auto &sceneTextures = GetTextureResourceManager()->GetTextures();
            const uint32_t imageSlots = std::max(256u, static_cast<uint32_t>(sceneTextures.size()) + 16u);
            DescriptorHeapService heap;
            heap.Initialize(GetRenderDevice(),
                            DescriptorHeapService::Config{.imageSlots = imageSlots, .inFlightCount = kImagesInFlight});
            // Match the legacy UI's SAMPLER_1_MIPS: linear filtering, clamp, one mip. The FINAL node uses
            // the same state for the composite.
            VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxLod = 1.0F;
            const DescriptorSlot uiSamplerSlot = heap.AcquireSampler(samplerInfo);

            // One builder for the process: Build() is per frame, and the resource/pipeline/descriptor caches
            // make an unchanged declaration cheap. Only the acquired swapchain image changes per frame.
            RenderGraphBuilder builder(RenderGraphServices{.device = GetRenderDevice(),
                                                           .resourceManager = GetRenderResourceManager(),
                                                           .descriptorHeap = &heap},
                                       kImagesInFlight);
            TextureResource *pEnvMap = GetRenderResourceManager()->GetResource<TextureResource>("EnvMap");
            if (pEnvMap == nullptr)
            {
                throw std::runtime_error("helloVulkan: the environment map texture was not created.");
            }

            // ── Material texture table ───────────────────────────────────────────────────────────────
            // PBRMaterial::textureIds are TextureResourceManager indices, not heap indices, and an
            // imported texture's heap slot is per-execution. So every material texture gets one static
            // sampled-image descriptor, and a small storage buffer maps the manager index to it. The
            // images are static, read-only, written once and retired with the heap; the graph does not
            // declare them as uses, because nothing writes them and there is no barrier to order.
            std::vector<uint32_t> materialTextureTable;
            materialTextureTable.reserve(sceneTextures.size());
            for (const auto &pTexture : sceneTextures)
            {
                materialTextureTable.push_back(heap.AcquireImage(pTexture->GetImageViewCreateInfo(),
                                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, /*bRing=*/false)
                                                   .index);
            }
            // GPU-only: the table is written once at load and never read back, so it does not need a
            // host-visible allocation.
            const StorageBuffer<uint32_t> *pMaterialTextureTable = GetRenderResourceManager()->GetStorageBuffer(
                "material texture table", materialTextureTable, /*bAllowReadback=*/false);
            const StorageBuffer<PerObjData> *pPerObj = GetPerObjResourceManager()->GetPerObjResource();
            const StorageBuffer<PBRMaterial> *pMaterials = GetMaterialManager()->GetMaterialBuffer();
            const auto *pLightCount = GetRenderResourceManager()->GetResource<UniformBuffer<uint32_t>>(LIGHT_COUNT);
            const auto *pLightData = GetRenderResourceManager()->GetResource<StorageBuffer<LightData>>(LIGHT_DATA);
            if (pPerObj == nullptr || pMaterials == nullptr || pLightCount == nullptr || pLightData == nullptr)
            {
                throw std::runtime_error("helloVulkan: a scene buffer the GBUFFER/LIGHTING nodes need is missing.");
            }

            builder.ImportResource("Font", overlay.GetFontTexture());
            builder.ImportResource("EnvMap", pEnvMap);
            builder.ImportResource("UIVertices", pUIVertices[0]);
            builder.ImportResource("UIIndices", pUIIndices[0]);
            builder.ImportResource("SceneVertices", meshResources.m_pVertexBuffer);
            builder.ImportResource("SceneIndices", meshResources.m_pIndexBuffer);
            builder.ImportResource("PerObjData", pPerObj);
            builder.ImportResource("Materials", pMaterials);
            builder.ImportResource("TextureTable", pMaterialTextureTable);
            builder.ImportResource("DrawCommands", pGBufferDrawCommands);
            builder.ImportResource("LightCount", pLightCount);
            builder.ImportResource("LightData", pLightData);
            // Image 0 is only the declaration-time placeholder; every frame re-imports the acquired image.
            builder.ImportResource("Present", swapchain.GetImageResource(0));

            // ── GBUFFER + direct OPAQUE_LIGHTING, graph-owned ─────────────────────────────────────────
            const VkExtent2D sceneExtent = GetRenderPassManager()->GetViewportSize();
            const auto addSceneColorTarget = [&builder, sceneExtent](const char *name)
            {
                builder.AddResource(
                    ResourceHandle(name),
                    ImageResourceDesc{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                      .extent = sceneExtent,
                                      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
            };
            addSceneColorTarget("GBufferPositionAO");
            addSceneColorTarget("GBufferAlbedoTransmittance");
            addSceneColorTarget("GBufferNormalRoughness");
            addSceneColorTarget("GbufferMetalnessTranslucency");
            addSceneColorTarget("Composite");
            builder.AddResource("GBufferDepth", ImageResourceDesc{.format = VK_FORMAT_D32_SFLOAT,
                                                                  .extent = sceneExtent,
                                                                  .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                                           VK_IMAGE_USAGE_SAMPLED_BIT});

            Camera *pCamera = GetRenderPassManager()->GetCamera();
            builder.AddNode(RenderGraphNodeCreateInfo{
                .nodeName = "GBUFFER",
                .queueType = QueueType::GRAPHICS,
                .resourceUses =
                    {
                        ResourceUse{.handle = ResourceHandle("PerObjData"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::STORAGE_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("Materials"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::STORAGE_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("TextureTable"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::STORAGE_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        // INDIRECT_BUFFER rather than DRAW_COMMAND_BUFFER: the legacy DrawCommandBuffer
                        // has no SHADER_DEVICE_ADDRESS usage and no shader reads it, so the
                        // draw-command role would acquire an unnecessary heap descriptor and call
                        // vkGetBufferDeviceAddress on a buffer that cannot answer it.
                        ResourceUse{.handle = ResourceHandle("DrawCommands"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::INDIRECT_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("SceneVertices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::VERTEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("SceneIndices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::INDEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("GBufferPositionAO"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::COLOR_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("GBufferAlbedoTransmittance"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::COLOR_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("GBufferNormalRoughness"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::COLOR_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("GbufferMetalnessTranslucency"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::COLOR_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("GBufferDepth"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::DEPTH_STENCIL_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                    },
                .shaderNames = {"GBuffer.vert.heap", "GBuffer.frag.heap"},
                .psoDesc = {.vertexType = VertexType::GENERIC,
                            // The legacy pass did not cull, so neither does this one.
                            .rasterState = {.cullMode = CullMode::NONE},
                            .depthStencilState = {.depthTestEnable = true, .depthWriteEnable = true},
                            // One entry per colour attachment: a mismatch fails pipeline creation
                            // (VUID-VkGraphicsPipelineCreateInfo-renderPass-06055).
                            .blendState = {.attachmentCount = 4,
                                           .attachments = {{{.blendEnable = false},
                                                            {.blendEnable = false},
                                                            {.blendEnable = false},
                                                            {.blendEnable = false}}}}},
                .attachmentClearValues = {VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}},
                                          VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}},
                                          VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}},
                                          VkClearValue{.color = {0.0F, 0.0F, 0.0F, 0.0F}}},
                .execute =
                    [pCamera, sampler = uiSamplerSlot.index, drawCount = gbufferDrawCount](RenderGraphNodeContext &ctx)
                {
                    const std::optional<uint32_t> perObj =
                        ctx.GetHeapIndex("PerObjData", ResourceUsage::STORAGE_BUFFER);
                    const std::optional<uint32_t> materials =
                        ctx.GetHeapIndex("Materials", ResourceUsage::STORAGE_BUFFER);
                    const std::optional<uint32_t> table =
                        ctx.GetHeapIndex("TextureTable", ResourceUsage::STORAGE_BUFFER);
                    if (!perObj.has_value() || !materials.has_value() || !table.has_value())
                    {
                        throw std::runtime_error("helloVulkan: the GBUFFER node's buffers have no heap index.");
                    }
                    GBufferPush push{};
                    push.mView = pCamera->GetViewMat();
                    push.mProj = pCamera->GetProjMat();
                    push.perObjData = glm::uvec2(*perObj, 0);
                    push.materialData = glm::uvec2(*materials, 0);
                    push.textureTable = glm::uvec2(*table, 0);
                    push.sampler = sampler;
                    ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

                    const auto *pVertices = ctx.GetResource<VertexBuffer<Vertex>>("SceneVertices");
                    const auto *pIndices = ctx.GetResource<IndexBuffer>("SceneIndices");
                    const auto *pDrawBuffers =
                        ctx.GetResource<DrawCommandBuffer<VkDrawIndexedIndirectCommand>>("DrawCommands");
                    if (pVertices == nullptr || pIndices == nullptr || pDrawBuffers == nullptr) return;
                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = pVertices->buffer();
                    vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(ctx.commandBuffer, pIndices->buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexedIndirect(ctx.commandBuffer, pDrawBuffers->buffer(), 0, drawCount,
                                             pDrawBuffers->GetStride());
                }});
            builder.AddNode(RenderGraphNodeCreateInfo{
                .nodeName = "LIGHTING",
                .queueType = QueueType::GRAPHICS,
                .resourceUses =
                    {
                        ResourceUse{.handle = ResourceHandle("GBufferPositionAO"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("GBufferAlbedoTransmittance"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("GBufferNormalRoughness"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("GbufferMetalnessTranslucency"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("LightCount"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::UNIFORM_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("LightData"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::STORAGE_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("SceneVertices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::VERTEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("SceneIndices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::INDEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("Composite"),
                                    .io = ResourceIOType::WRITE,
                                    .usage = ResourceUsage::COLOR_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                    },
                .shaderNames = {"triangle.vert", "lighting.frag.heap"},
                .psoDesc = {.vertexType = VertexType::GENERIC,
                            .rasterState = {.cullMode = CullMode::NONE},
                            .depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false},
                            // A full-screen overwrite: no blending, matching the deleted lighting pass.
                            .blendState = {.attachmentCount = 1, .attachments = {{{.blendEnable = false}}}}},
                .execute = [pCamera, sampler = uiSamplerSlot.index, indexCount = quad.m_nIndexCount,
                            indexOffset = quad.m_nIndexOffset](RenderGraphNodeContext &ctx)
                {
                    const std::optional<uint32_t> positionAO =
                        ctx.GetHeapIndex("GBufferPositionAO", ResourceUsage::SAMPLED);
                    const std::optional<uint32_t> albedo =
                        ctx.GetHeapIndex("GBufferAlbedoTransmittance", ResourceUsage::SAMPLED);
                    const std::optional<uint32_t> normal =
                        ctx.GetHeapIndex("GBufferNormalRoughness", ResourceUsage::SAMPLED);
                    const std::optional<uint32_t> metalness =
                        ctx.GetHeapIndex("GbufferMetalnessTranslucency", ResourceUsage::SAMPLED);
                    const std::optional<uint32_t> lightCount =
                        ctx.GetHeapIndex("LightCount", ResourceUsage::UNIFORM_BUFFER);
                    const std::optional<uint32_t> lightData =
                        ctx.GetHeapIndex("LightData", ResourceUsage::STORAGE_BUFFER);
                    if (!positionAO.has_value() || !albedo.has_value() || !normal.has_value() ||
                        !metalness.has_value() || !lightCount.has_value() || !lightData.has_value())
                    {
                        throw std::runtime_error("helloVulkan: the LIGHTING node's inputs have no heap index.");
                    }
                    LightingPush push{};
                    push.mView = pCamera->GetViewMat();
                    push.positionAO = glm::uvec2(*positionAO, 0);
                    push.albedoTransmittance = glm::uvec2(*albedo, 0);
                    push.normalRoughness = glm::uvec2(*normal, 0);
                    push.metalnessTranslucency = glm::uvec2(*metalness, 0);
                    push.lightCount = glm::uvec2(*lightCount, 0);
                    push.lightData = glm::uvec2(*lightData, 0);
                    push.sampler = sampler;
                    ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

                    const auto *pVertices = ctx.GetResource<VertexBuffer<Vertex>>("SceneVertices");
                    const auto *pIndices = ctx.GetResource<IndexBuffer>("SceneIndices");
                    if (pVertices == nullptr || pIndices == nullptr) return;
                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = pVertices->buffer();
                    vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(ctx.commandBuffer, pIndices->buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(ctx.commandBuffer, indexCount, 1, indexOffset, 0, 0);
                }});
            builder.AddNode(RenderGraphNodeCreateInfo{
                .nodeName = "UI",
                .queueType = QueueType::GRAPHICS,
                .resourceUses =
                    {
                        ResourceUse{.handle = ResourceHandle("Font"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("EnvMap"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("UIVertices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::VERTEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("UIIndices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::INDEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("Composite"),
                                    .io = ResourceIOType::READ_WRITE,
                                    .usage = ResourceUsage::COLOR_ATTACHMENT,
                                    .kind = ResourceKind::IMAGE},
                    },
                .shaderNames = {"ui.vert.slang", "ui.frag.heap"},
                .psoDesc = {.vertexType = VertexType::UI,
                            .rasterState = {.cullMode = CullMode::NONE},
                            .depthStencilState = {.depthTestEnable = false, .depthWriteEnable = false},
                            // Alpha blending, matching the legacy UI: the font carries glyph coverage in its
                            // alpha, and the env-map preview is opaque but drawn with the same state.
                            .blendState = {.attachmentCount = 1,
                                           .attachments = {{{.blendEnable = true,
                                                             .srcColor = BlendFactor::SRC_ALPHA,
                                                             .dstColor = BlendFactor::ONE_MINUS_SRC_ALPHA,
                                                             .srcAlpha = BlendFactor::ONE,
                                                             .dstAlpha = BlendFactor::ZERO}}}}},
                .execute = [&overlay, uiSamplerSlot](RenderGraphNodeContext &ctx)
                {
                    struct UIPush
                    {
                        glm::vec2 scale;
                        glm::vec2 translate;
                        uint32_t image;
                        uint32_t sampler;
                    };
                    const std::optional<uint32_t> fontImage = ctx.GetHeapIndex("Font", ResourceUsage::SAMPLED);
                    const std::optional<uint32_t> envMapImage = ctx.GetHeapIndex("EnvMap", ResourceUsage::SAMPLED);
                    if (!fontImage.has_value())
                    {
                        throw std::runtime_error("helloVulkan: the UI node's font has no heap index.");
                    }
                    const ImGuiIO &io = ImGui::GetIO();
                    const glm::vec2 scale = (io.DisplaySize.x > 0.0F && io.DisplaySize.y > 0.0F)
                                                ? glm::vec2(2.0F / io.DisplaySize.x, 2.0F / io.DisplaySize.y)
                                                : glm::vec2(0.0F);
                    const glm::vec2 translate(-1.0F);
                    // The graph requires the push block on every execution, even one that draws nothing.
                    const UIPush fallback{scale, translate, *fontImage, uiSamplerSlot.index};
                    ctx.PushData(0, std::as_bytes(std::span{&fallback, 1}));

                    ImDrawData *pDrawData = overlay.GetDrawData();
                    if (pDrawData == nullptr || pDrawData->TotalVtxCount == 0) return;
                    const auto *pVertices = dynamic_cast<const BufferResource *>(ctx.resolveResource("UIVertices"));
                    const auto *pIndices = dynamic_cast<const BufferResource *>(ctx.resolveResource("UIIndices"));
                    if (pVertices == nullptr || pIndices == nullptr) return;

                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = pVertices->buffer();
                    vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(ctx.commandBuffer, pIndices->buffer(), 0, VK_INDEX_TYPE_UINT16);

                    int32_t vertexOffset = 0;
                    uint32_t indexOffset = 0;
                    for (int n = 0; n < pDrawData->CmdListsCount; ++n)
                    {
                        const ImDrawList *pList = pDrawData->CmdLists[n];
                        for (int j = 0; j < pList->CmdBuffer.Size; ++j)
                        {
                            const ImDrawCmd &drawCmd = pList->CmdBuffer[j];
                            // 0 is the font; 1 is the environment-map preview the debug page asks for.
                            uint32_t image = *fontImage;
                            if (drawCmd.GetTexID() == kEnvMapTextureId && envMapImage.has_value())
                            {
                                image = *envMapImage;
                            }
                            const UIPush push{scale, translate, image, uiSamplerSlot.index};
                            ctx.PushData(0, std::as_bytes(std::span{&push, 1}));
                            const VkRect2D scissor{{static_cast<int32_t>(std::max(drawCmd.ClipRect.x, 0.0F)),
                                                    static_cast<int32_t>(std::max(drawCmd.ClipRect.y, 0.0F))},
                                                   {static_cast<uint32_t>(drawCmd.ClipRect.z - drawCmd.ClipRect.x),
                                                    static_cast<uint32_t>(drawCmd.ClipRect.w - drawCmd.ClipRect.y)}};
                            vkCmdSetScissor(ctx.commandBuffer, 0, 1, &scissor);
                            vkCmdDrawIndexed(ctx.commandBuffer, drawCmd.ElemCount, 1, indexOffset, vertexOffset, 0);
                            indexOffset += drawCmd.ElemCount;
                        }
                        vertexOffset += pList->VtxBuffer.Size;
                    }
                }});
            builder.AddNode(RenderGraphNodeCreateInfo{
                .nodeName = "Final",
                .queueType = QueueType::GRAPHICS,
                .resourceUses =
                    {
                        ResourceUse{.handle = ResourceHandle("Composite"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::SAMPLED,
                                    .kind = ResourceKind::IMAGE},
                        ResourceUse{.handle = ResourceHandle("SceneVertices"),
                                    .io = ResourceIOType::READ,
                                    .usage = ResourceUsage::VERTEX_BUFFER,
                                    .kind = ResourceKind::BUFFER},
                        ResourceUse{.handle = ResourceHandle("SceneIndices"),
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
                            uiSamplerSlot](RenderGraphNodeContext &ctx)
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
                    const FinalPush push{*composite, uiSamplerSlot.index};
                    // The graph requires the push block on every execution, even one that draws nothing.
                    ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

                    const auto *pVertices = dynamic_cast<const BufferResource *>(ctx.resolveResource("SceneVertices"));
                    const auto *pIndices = dynamic_cast<const BufferResource *>(ctx.resolveResource("SceneIndices"));
                    if (pVertices == nullptr || pIndices == nullptr) return;
                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = pVertices->buffer();
                    vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                    vkCmdBindIndexBuffer(ctx.commandBuffer, pIndices->buffer(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(ctx.commandBuffer, indexCount, 1, indexOffset, 0, 0);
                }});
            // Ordering is explicit; sharing a resource does not order the nodes by itself.
            builder.AddDependency("GBUFFER", "LIGHTING");
            builder.AddDependency("LIGHTING", "UI");
            builder.AddDependency("UI", "Final");

            // Drain, recreate the swapchain at the window's new size, and rebuild the graph's per-execution
            // device objects. The graph's scene targets stay at their creation extent (the manager's
            // viewport); FINAL stretches the composite into the new swapchain, and #186 owns the camera
            // aspect.
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
                ensureUIBuffers();
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

                // BeginFrame waits the slot's previous execution, which is what makes rebuilding the UI
                // draw data and re-uploading `perView` safe with one slot in flight.
                const std::optional<FrameSync::Frame> frame = frameSync.BeginFrame();
                if (!frame.has_value())
                {
                    // Out of date (a resize).
                    recreateSwapchain();
                    continue;
                }

                // The legacy manager now records only IBL/cubemap (and the unported RT pass); the scene
                // itself runs as graph nodes. The overlay builds the debug UI's draw data, and the graph's
                // UI node composites it over the lighting output.
                GetRenderPassManager()->UpdatePerViewData();
                DebugUIFrame uiFrame;
                uiFrame.width = GetRenderPassManager()->GetViewportSize().width;
                uiFrame.height = GetRenderPassManager()->GetViewportSize().height;
                uiFrame.camera = GetRenderPassManager()->GetCamera();
                uiFrame.reloadEnvironmentMap = [](const std::string &path)
                { GetRenderPassManager()->ReloadEnvironmentMap(path); };
                uiFrame.textureId = [](const std::string &name) -> ImTextureID
                { return name == "EnvMap" ? kEnvMapTextureId : kFontTextureId; };
                overlay.BuildFrame(uiFrame);

                // The environment page can reload the map during BuildFrame, so resolve the current texture
                // after it and re-import it for this execution.
                const TextureResource *pFrameEnvMap =
                    GetRenderResourceManager()->GetResource<TextureResource>("EnvMap");
                if (pFrameEnvMap == nullptr)
                {
                    throw std::runtime_error("helloVulkan: the environment map disappeared during the UI frame.");
                }
                ImDrawData *pDrawData = overlay.GetDrawData();
                VertexBuffer<ImDrawVert> *pFrameVertices = pUIVertices[frame->imageIndex];
                IndexBuffer *pFrameIndices = pUIIndices[frame->imageIndex];
                if (pDrawData != nullptr && pDrawData->TotalVtxCount > 0)
                {
                    pFrameVertices->SetData(nullptr, pDrawData->TotalVtxCount * sizeof(ImDrawVert));
                    pFrameIndices->SetData(nullptr, pDrawData->TotalIdxCount * sizeof(ImDrawIdx));
                    auto *pVertexDst = static_cast<ImDrawVert *>(pFrameVertices->Map());
                    auto *pIndexDst = static_cast<ImDrawIdx *>(pFrameIndices->Map());
                    for (int n = 0; n < pDrawData->CmdListsCount; ++n)
                    {
                        const ImDrawList *pList = pDrawData->CmdLists[n];
                        std::memcpy(pVertexDst, pList->VtxBuffer.Data, pList->VtxBuffer.size_in_bytes());
                        std::memcpy(pIndexDst, pList->IdxBuffer.Data, pList->IdxBuffer.size_in_bytes());
                        pVertexDst += pList->VtxBuffer.Size;
                        pIndexDst += pList->IdxBuffer.Size;
                    }
                    pFrameVertices->Unmap();
                    pFrameIndices->Unmap();
                }

                GetRenderPassManager()->SubmitSceneCommandBuffers(frame->imageAvailable);

                builder.ImportResource("Present", swapchain.GetImageResource(frame->imageIndex));
                builder.ImportResource("UIVertices", pFrameVertices);
                builder.ImportResource("UIIndices", pFrameIndices);
                builder.ImportResource("EnvMap", pFrameEnvMap);
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
    cleanup();
    return 0;
}
