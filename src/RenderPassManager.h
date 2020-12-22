#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <vector>

class RenderPass;
class Geometry;
enum RenderPassNames
{
    RENDERPASS_GBUFFER,
    RENDERPASS_IBL,
    RENDERPASS_UI,
    RENDERPASS_FINAL,

    RENDERPASS_COUNT
};
class RenderPassManager
{
public:
    void Initialize(uint32_t uWidth, uint32_t uHeight, VkFormat swapchainFormat);
    void SetSwapchainImageViews(std::vector<VkImageView> &vImageViews, VkImageView depthImageView);
    void OnResize(uint32_t uWidth, uint32_t uHeight);
    void Unintialize();
    void RecordCmdBuffers(const std::vector<const Geometry*>& vpGeometries);
    std::vector<VkCommandBuffer> GetCommandBuffers(uint32_t uImgIdx);

private:
    std::array<std::unique_ptr<RenderPass>, RENDERPASS_COUNT> m_vpRenderPasses = {nullptr, nullptr, nullptr, nullptr};
    uint32_t m_uWidth = 0;
    uint32_t m_uHeight = 0;
    bool m_bIsIrradianceGenerated = false;
};

RenderPassManager* GetRenderPassManager();
