#pragma once
#include <memory>
#include <array>
class RenderPass;
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
    void Initialize();
    void OnResize();
private:
    std::array<std::unique_ptr<RenderPass>, RENDERPASS_COUNT> m_vpRenderPasses;
};
