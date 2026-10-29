#pragma once
#include <imgui.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "DebugUI.h"

namespace Muyo
{
class TextureResource;

/// Host half of the debug UI.
///
/// Owns the ImGui context, the registered pages and the font texture, and builds one frame's draw data.
/// It draws nothing itself: a graph node composites the draw data, so the overlay is host-side and works
/// for the legacy app until its UI pass is ported too.
class DebugUIOverlay
{
public:
    DebugUIOverlay();
    ~DebugUIOverlay();
    DebugUIOverlay(const DebugUIOverlay&) = delete;
    DebugUIOverlay& operator=(const DebugUIOverlay&) = delete;

    /// Set the display size, start ImGui's frame, render every registered page with `frame`, and end it.
    /// @param frame The viewport, camera and reload/texture hooks the pages may use this frame.
    void BuildFrame(const DebugUIFrame& frame);

    /// @return The draw data built by the last `BuildFrame`, or null before the first.
    ImDrawData* GetDrawData() const { return ImGui::GetDrawData(); }

    /// @return The font texture the draw data samples; owned by the resource manager.
    TextureResource* GetFontTexture() const { return m_pFont; }

    /// Register a page; the overlay owns it and renders it in registration order.
    template <typename PageType, typename... Args>
    PageType* RegisterPage(Args&&... args)
    {
        auto pPage = std::make_unique<PageType>(std::forward<Args>(args)...);
        PageType* pRaw = pPage.get();
        m_vpPages.push_back(std::move(pPage));
        return pRaw;
    }

private:
    std::vector<std::unique_ptr<IDebugUIPage>> m_vpPages;
    TextureResource* m_pFont = nullptr;
};
}  // namespace Muyo
