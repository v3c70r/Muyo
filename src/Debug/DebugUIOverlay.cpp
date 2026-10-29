#include "DebugUIOverlay.h"

#include "ImGuiControl.h"
#include "RenderResourceManager.h"
#include "Texture.h"

namespace Muyo
{
DebugUIOverlay::DebugUIOverlay()
{
    ImGui::CreateContext();
    ImGui::Init();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    unsigned char* pFontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pFontPixels, &fontWidth, &fontHeight);
    m_pFont = GetRenderResourceManager()->GetTexture("ui_font_texture", pFontPixels, fontWidth, fontHeight);
    // Texture id 0 is the font; the host's UI node maps any other id (the environment preview) itself.
    ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(0));
}

DebugUIOverlay::~DebugUIOverlay()
{
    ImGui::Shutdown();
    ImGui::DestroyContext();
}

void DebugUIOverlay::BuildFrame(const DebugUIFrame& frame)
{
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(frame.width), static_cast<float>(frame.height));
    ImGui::NewFrame();
    for (const auto& pPage : m_vpPages)
    {
        if (pPage->ShouldRender())
        {
            pPage->SetFrame(frame);
            pPage->Render();
        }
    }
    ImGui::Render();
}
}  // namespace Muyo
