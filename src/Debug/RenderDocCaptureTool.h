#pragma once
#include <optional>
#include <string>
#include <utility>

#include "CaptureTool.h"
#include "RenderDoc.h"

namespace Muyo
{
/// RenderDoc as a capture backend.
///
/// The capture itself is the same `RenderDocScopedCapture` the engine already used, so nesting, naming and the
/// "only the outermost scope owns the frame" rule are unchanged - this class only fits it behind the interface.
///
/// Markers are deliberately no-ops here: RenderDoc reads the Vulkan debug labels the engine already emits
/// (`ScopedMarker`), so mirroring them through this interface would duplicate what the capture already shows. A backend
/// that has its own marker API (a vendor profiler's user ranges) implements `BeginMarker` instead.
class RenderDocCaptureTool final : public ICaptureTool
{
public:
    /// @param captureName File-path template and capture title, as RenderDoc takes them.
    explicit RenderDocCaptureTool(std::string captureName = "") : m_captureName(std::move(captureName)) {}

    /// @return The capture name used for the next capture.
    const std::string& GetCaptureName() const { return m_captureName; }

    /// @param captureName File-path template and capture title for the next capture.
    void SetCaptureName(std::string captureName) { m_captureName = std::move(captureName); }

    /// @return True when RenderDoc is injected into this process.
    bool IsAvailable() const override { return GetRenderDocAPI() != nullptr; }

    /// Start a capture; a nested begin is a no-op, as `RenderDocScopedCapture` treats it.
    void BeginCapture() override
    {
        if (!m_pCapture.has_value())
        {
            m_pCapture.emplace(m_captureName);
        }
    }

    /// Finish the capture started by `BeginCapture`.
    void EndCapture() override { m_pCapture.reset(); }

    /// @param name Ignored; the Vulkan debug labels already carry it into the capture.
    void BeginMarker(const std::string& name) override;
    /// @brief Does nothing, for the reason above.
    void EndMarker() override {}
    /// @param name Ignored.
    /// @param text Ignored.
    void Annotate(const std::string& name, const std::string& text) override;

private:
    std::string m_captureName;                         ///< Applied to the next capture.
    std::optional<RenderDocScopedCapture> m_pCapture;  ///< Live while a capture is open.
};
}  // namespace Muyo
