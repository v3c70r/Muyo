// helloVulkanRG — the render graph's first consumer.
//
// The same kind of frame as helloVulkan, but the loop is the graph's: `FrameSync` acquires and presents,
// and every pass is a graph node. This is the design note's layer (b) — the render pipeline written
// against the graph's public API — so anything it cannot express is a surface gap rather than a
// workaround.
//
// The UI pass is the first one ported: the graph draws ImGui's draw data over a cleared frame.

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include <optional>
#include <stdexcept>
#include <vector>

#if defined(USE_SDL)
#include "WindowSDL.h"
#else
#include "WindowGLFW.h"
#endif

#include "Debug/RenderGraphTracePage.h"
#include "DescriptorHeapService.h"
#include "DescriptorManager.h"
#include "FrameSync.h"
#include "ImGuiControl.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "Swapchain.h"
#include "Texture.h"
#include "VertexBuffer.h"
#include "VkExtFuncsLoader.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

using namespace Muyo;
using namespace Muyo::RenderGraph;

namespace
{
constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kImagesInFlight = 2;

/// @return The window's instance extensions plus what the renderer needs.
std::vector<const char*> InstanceExtensions()
{
    std::vector<const char*> extensions;
    if (Window::GetVulkanInstanceExtensions(extensions) == 0 || extensions.empty())
    {
        throw std::runtime_error("helloVulkanRG: the window reported no Vulkan instance extensions.");
    }
    extensions.push_back("VK_KHR_get_physical_device_properties2");
    return extensions;
}

/// @return A surface format the swapchain supports.
VkSurfaceFormatKHR ChooseSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
    if (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        return VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    return formats[0];
}
}  // namespace

int main()
{
    if (!Window::Initialize("hello Vulkan (render graph)", kWidth, kHeight, /*bResizable=*/true)) return -1;

    GetRenderDevice()->Initialize(InstanceExtensions(), {});
    VkExt::LoadInstanceFunctions(GetRenderDevice()->GetInstance());
    const VkSurfaceKHR surface = Window::GetVulkanSurface(GetRenderDevice()->GetInstance());

    // The device takes the surface so it can select a present-capable queue family.
    // The graph path takes the Vulkan 1.4 + descriptor-heap floor (B0): requesting the extensions here
    // makes a device without them fail at creation with the missing extension named, rather than at the
    // first heap bind once B1b lands. The feature structs are appended by `CreateDevice`, which also
    // verifies descriptorHeap and shaderUntypedPointers before enabling them.
    GetRenderDevice()->CreateDevice({VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
                                     VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME},
                                    {}, &surface, {});
    GetMemoryAllocator()->Initalize(GetRenderDevice());
    GetRenderDevice()->CreateCommandPools();
    GetDescriptorManager()->CreateDescriptorPool();
    GetDescriptorManager()->CreateDescriptorSetLayouts();
    GetSamplerManager()->createSamplers();
    GetRenderResourceManager()->Initialize();

    // ── UI resources, created once ──────────────────────────────────────────────────────────────────
    ImGui::CreateContext();
    ImGui::Init();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    unsigned char* pFontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pFontPixels, &fontWidth, &fontHeight);
    TextureResource* pFont =
        GetRenderResourceManager()->GetTexture("ui_font_texture", pFontPixels, fontWidth, fontHeight);
    DescriptorHeapService uiHeap;
    // The builder's in-flight slots and the heap's per-execution rings must agree; configuring the heap
    // from the same constant makes that a compile-time fact rather than a default that happens to match.
    uiHeap.Initialize(GetRenderDevice(), DescriptorHeapService::Config{.inFlightCount = kImagesInFlight});
    VkSamplerCreateInfo uiSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    uiSamplerInfo.magFilter = VK_FILTER_LINEAR;
    uiSamplerInfo.minFilter = VK_FILTER_LINEAR;
    uiSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    uiSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    uiSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    uiSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const DescriptorSlot fontSamplerSlot = uiHeap.AcquireSampler(uiSamplerInfo);
    // The UI shader samples one texture, so the id is only a placeholder.
    ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(0));

    // One vertex/index buffer pair per swapchain image: with two executions in flight, frame N+1 must
    // not overwrite the UI data frame N's GPU work is still reading. The graph imports the pair for the
    // acquired image and binds it per execution.
    const std::vector<ImDrawVert> dummyVertices = {ImDrawVert{{0.0F, 0.0F}, {0.0F, 0.0F}, 0}};
    const std::vector<ImDrawIdx> dummyIndices = {0};
    std::vector<VertexBuffer<ImDrawVert>*> pUIVertices;
    std::vector<IndexBuffer*> pUIIndices;

    // Retains the last few hundred executions so the lane view can be scrubbed. Declared outside the swapchain scope
    // because the trace outlives any one swapchain: a resize rebuilds the swapchain but not the history.
    auto pTracePage = std::make_unique<RenderGraphTracePage>("Render graph execution");

    {
        Swapchain swapchain;
        // The window owns the surface, so the swapchain must not destroy it. Wayland lets the app choose
        // the extent, so pass the window's framebuffer size.
        int initialWidth = 0;
        int initialHeight = 0;
        Window::GetFramebufferSize(initialWidth, initialHeight);
        swapchain.CreateSwapchain(
            surface, ChooseSurfaceFormat(GetRenderDevice()->GetPhysicalDevice(), surface), VK_PRESENT_MODE_FIFO_KHR,
            kImagesInFlight, /*bOwnSurface=*/false,
            VkExtent2D{static_cast<uint32_t>(initialWidth), static_cast<uint32_t>(initialHeight)});
        FrameSync frameSync(GetRenderDevice(), &swapchain, kImagesInFlight);

        // The driver may hand back more images than the requested count, so size the pairs from the
        // swapchain after creation and after every recreation.
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

        // One builder for the process. Build() is per frame, and the resource, descriptor and pipeline
        // caches make an unchanged declaration cheap; a builder per frame would discard all three. The
        // acquired swapchain image rotates, so only that import changes per frame.
        // The graph owns the heap binding: it uploads the service once per segment, before any render
        // pass starts (the upload records vkCmdCopyBuffer). Inject the same service the UI sampler is
        // acquired from, or the pushed sampler index and the bound resource heap would be different
        // heaps.
        RenderGraphBuilder builder(
            RenderGraphServices{
                .device = GetRenderDevice(), .resourceManager = GetRenderResourceManager(), .descriptorHeap = &uiHeap},
            kImagesInFlight);
        // Record every execution for the lane view. The sink's presence is what turns tracing on, and it costs
        // one null check per node when the page is not shown.
        builder.SetTraceSink([page = pTracePage.get()](RenderGraph::RenderGraphTrace trace)
                             { page->Push(std::move(trace)); });
        // AddNode validates its uses immediately, and the per-frame import replaces this one before the
        // first Build; image 0 is only the declaration-time placeholder.
        builder.ImportResource("Present", swapchain.GetImageResource(0));
        builder.ImportResource("Font", pFont);
        builder.ImportResource("UIVertices", pUIVertices[0]);
        builder.ImportResource("UIIndices", pUIIndices[0]);
        builder.AddNode(RenderGraphNodeCreateInfo{
            .nodeName = "UI",
            .queueType = QueueType::GRAPHICS,
            .resourceUses =
                {
                    ResourceUse{.handle = ResourceHandle("Font"),
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
                    ResourceUse{.handle = ResourceHandle("Present"),
                                .io = ResourceIOType::WRITE,
                                .usage = ResourceUsage::PRESENT,
                                .kind = ResourceKind::IMAGE},
                },
            .shaderNames = {"ui.vert.slang", "ui.frag.heap"},
            .psoDesc = {.vertexType = VertexType::UI,
                        .rasterState = {.cullMode = CullMode::NONE},
                        // Alpha blending, matching RenderPassUI: the font texture carries glyph
                        // coverage in its alpha, so an opaque write renders white blocks.
                        .blendState = {.attachmentCount = 1,
                                       .attachments = {{{.blendEnable = true,
                                                         .srcColor = BlendFactor::SRC_ALPHA,
                                                         .dstColor = BlendFactor::ONE_MINUS_SRC_ALPHA,
                                                         .srcAlpha = BlendFactor::ONE,
                                                         .dstAlpha = BlendFactor::ZERO}}}}},
            .attachmentClearValues = {{{.color = {0.05F, 0.05F, 0.08F, 1.0F}}}},
            .execute = [fontSamplerSlot](RenderGraphNodeContext& ctx)
            {
                struct PushBlock
                {
                    glm::vec2 scale;
                    glm::vec2 translate;
                    uint32_t fontImage;
                    uint32_t fontSampler;
                };
                const ImGuiIO& io = ImGui::GetIO();
                const glm::vec2 scale = (io.DisplaySize.x > 0.0F && io.DisplaySize.y > 0.0F)
                                            ? glm::vec2(2.0F / io.DisplaySize.x, 2.0F / io.DisplaySize.y)
                                            : glm::vec2(0.0F);
                // The graph requires every heap shader's push block to be covered on every recorded
                // execution, even one that ends up drawing nothing (an empty first frame, or a missing
                // imported buffer). Push first, then decide whether there is work; the values are never
                // read without a draw.
                ImDrawData* pDrawData = ImGui::GetDrawData();
                const auto* pVertices = dynamic_cast<const BufferResource*>(ctx.resolveResource("UIVertices"));
                const auto* pIndices = dynamic_cast<const BufferResource*>(ctx.resolveResource("UIIndices"));
                const std::optional<uint32_t> fontImage = ctx.GetHeapIndex("Font", ResourceUsage::SAMPLED);
                const PushBlock push{scale, glm::vec2(-1.0F), fontImage.value_or(0), fontSamplerSlot.index};
                ctx.PushData(0, std::as_bytes(std::span{&push, 1}));

                if (pDrawData == nullptr || pDrawData->TotalVtxCount == 0) return;
                if (pVertices == nullptr || pIndices == nullptr || !fontImage.has_value())
                {
                    static bool s_warned = false;
                    if (!s_warned)
                    {
                        s_warned = true;
                        std::cerr << "[helloVulkanRG] UI draw skipped: "
                                  << (pVertices == nullptr ? "UIVertices missing " : "")
                                  << (pIndices == nullptr ? "UIIndices missing " : "")
                                  << (!fontImage.has_value() ? "Font heap index missing" : "") << std::endl;
                    }
                    return;
                }

                VkDeviceSize offset = 0;
                VkBuffer vertexBuffer = pVertices->buffer();
                vkCmdBindVertexBuffers(ctx.commandBuffer, 0, 1, &vertexBuffer, &offset);
                vkCmdBindIndexBuffer(ctx.commandBuffer, pIndices->buffer(), 0, VK_INDEX_TYPE_UINT16);

                int32_t vertexOffset = 0;
                uint32_t indexOffset = 0;
                for (int n = 0; n < pDrawData->CmdListsCount; ++n)
                {
                    const ImDrawList* pList = pDrawData->CmdLists[n];
                    for (int j = 0; j < pList->CmdBuffer.Size; ++j)
                    {
                        const ImDrawCmd& drawCmd = pList->CmdBuffer[j];
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

        // Drain, recreate the swapchain at the window's new size, and rebuild the per-image tracking. Used
        // both when an acquire reports out-of-date and when present does, which is how a resize is picked
        // up between frames.
        const auto recreateSwapchain = [&]()
        {
            vkDeviceWaitIdle(GetRenderDevice()->GetDevice());
            const VkSurfaceKHR reclaimed = swapchain.GetSurface();
            swapchain.DestroySwapchain();
            int width = 0;
            int height = 0;
            Window::GetFramebufferSize(width, height);
            swapchain.CreateSwapchain(reclaimed, ChooseSurfaceFormat(GetRenderDevice()->GetPhysicalDevice(), reclaimed),
                                      VK_PRESENT_MODE_FIFO_KHR, kImagesInFlight, /*bOwnSurface=*/false,
                                      VkExtent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
            frameSync.OnSwapchainRecreated();
            ensureUIBuffers();
            // The old images are gone and the device was idle, so the descriptors and pipelines that
            // named them can be released; the next Build recreates what the new swapchain needs.
            builder.ReleaseCachedDeviceObjects();
        };

        while (!Window::ShouldQuit())
        {
            Window::ProcessEvents();
            ImGui::Update();

            // ImGui needs a display size before NewFrame. The engine's wrapper does not set one (the
            // legacy RenderPassUI::NewFrame did), and the push constants below scale by it. The window
            // framebuffer size is the size the UI is laid out for.
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            Window::GetFramebufferSize(framebufferWidth, framebufferHeight);
            ImGui::GetIO().DisplaySize =
                ImVec2(static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight));

            // A platform can resize the surface without the swapchain reporting out-of-date (Wayland may
            // return SUCCESS or SUBOPTIMAL), so compare the window size to the swapchain and recreate when
            // they differ. Otherwise the render area and scissor - both derived from the acquired image's
            // extent - stay at the old size while the UI is laid out for the new one, which crops it.
            const VkExtent2D framebufferExtent{static_cast<uint32_t>(framebufferWidth),
                                               static_cast<uint32_t>(framebufferHeight)};
            if (framebufferWidth > 0 && framebufferHeight > 0 &&
                (framebufferExtent.width != swapchain.GetSwapchainExtent().width ||
                 framebufferExtent.height != swapchain.GetSwapchainExtent().height))
            {
                recreateSwapchain();
            }

            // Build this frame's UI draw data and upload it before the graph records.
            ImGui::NewFrame();
            ImGui::Begin("hello Vulkan (render graph)");
            ImGui::Text("This frame is drawn by the render graph.");
            ImGui::End();
            // The lane view is drawn from the trace of a *previous* execution - this frame has not run yet, because
            // the UI data has to be uploaded before the graph records. One frame of lag is invisible in a window and
            // keeps the page off the execution path entirely.
            pTracePage->Render();
            ImGui::Render();

            const std::optional<FrameSync::Frame> frame = frameSync.BeginFrame();
            if (!frame.has_value())
            {
                // Out of date (a resize).
                recreateSwapchain();
                continue;
            }

            // Upload this frame's UI data into the pair belonging to the acquired image. The draw data
            // was built before BeginFrame; the upload waits for the image's pair because the swapchain
            // image itself is exclusive to this frame.
            ImDrawData* pDrawData = ImGui::GetDrawData();
            VertexBuffer<ImDrawVert>* pFrameVertices = pUIVertices[frame->imageIndex];
            IndexBuffer* pFrameIndices = pUIIndices[frame->imageIndex];
            if (pDrawData != nullptr && pDrawData->TotalVtxCount > 0)
            {
                pFrameVertices->SetData(nullptr, pDrawData->TotalVtxCount * sizeof(ImDrawVert));
                pFrameIndices->SetData(nullptr, pDrawData->TotalIdxCount * sizeof(ImDrawIdx));
                auto* pVertexDst = static_cast<ImDrawVert*>(pFrameVertices->Map());
                auto* pIndexDst = static_cast<ImDrawIdx*>(pFrameIndices->Map());
                for (int n = 0; n < pDrawData->CmdListsCount; ++n)
                {
                    const ImDrawList* pList = pDrawData->CmdLists[n];
                    std::memcpy(pVertexDst, pList->VtxBuffer.Data, pList->VtxBuffer.size_in_bytes());
                    std::memcpy(pIndexDst, pList->IdxBuffer.Data, pList->IdxBuffer.size_in_bytes());
                    pVertexDst += pList->VtxBuffer.Size;
                    pIndexDst += pList->IdxBuffer.Size;
                }
                pFrameVertices->Unmap();
                pFrameIndices->Unmap();
            }

            // The acquired image rotates; re-import it and its UI buffer pair for this frame. The node
            // declaration is fixed and Build() reuses the resource, descriptor and pipeline caches.
            builder.ImportResource("Present", swapchain.GetImageResource(frame->imageIndex));
            builder.ImportResource("UIVertices", pFrameVertices);
            builder.ImportResource("UIIndices", pFrameIndices);
            builder.Build();
            builder.Execute(frameSync.ExecuteInfo(*frame));
            const VkResult presentResult = frameSync.Present(*frame);
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
            {
                recreateSwapchain();
            }
        }

        vkDeviceWaitIdle(GetRenderDevice()->GetDevice());
        swapchain.DestroySwapchain();
    }

    // GLFW creates the VkSurfaceKHR but does not own it, and the swapchain was told not to own it, so the
    // app destroys it - before the instance, or validation reports it as a child object left behind
    // (VUID-vkDestroyInstance-instance-00629).
    vkDestroySurfaceKHR(GetRenderDevice()->GetInstance(), surface, nullptr);

    ImGui::Shutdown();
    ImGui::DestroyContext();
    GetSamplerManager()->destroySamplers();
    GetRenderResourceManager()->Unintialize();
    GetDescriptorManager()->DestroyDescriptorSetLayouts();
    GetDescriptorManager()->DestroyDescriptorPool();
    GetRenderDevice()->DestroyCommandPools();
    GetMemoryAllocator()->Unintialize();
    GetRenderDevice()->DestroyDevice();
    GetRenderDevice()->Unintialize();
    Window::Uninitialize();
    return 0;
}
