#pragma once
#include <string>
#include <utility>
#include <vector>

#include "Debug/CaptureTool.h"

namespace Muyo::TestSupport
{
/// A capture backend that records what it was told, in order.
///
/// Shared by two cases in different translation units: the routing logic (`ScopedCaptureMarker`) and the composition
/// (`ScopedMarker` -> tool) are different properties, and the second one needs a device because `ScopedMarker` emits a
/// Vulkan label as well.
class RecordingCaptureTool final : public ICaptureTool
{
public:
    /// @param bAvailable What `IsAvailable` reports; lets a test cover "installed but not usable".
    explicit RecordingCaptureTool(bool bAvailable = true) : m_bAvailable(bAvailable) {}

    bool IsAvailable() const override { return m_bAvailable; }
    void BeginCapture() override { calls.emplace_back("begin-capture"); }
    void EndCapture() override { calls.emplace_back("end-capture"); }
    void BeginMarker(const std::string& name) override { calls.emplace_back("begin:" + name); }
    void EndMarker() override { calls.emplace_back("end"); }
    void Annotate(const std::string& name, const std::string& text) override
    {
        calls.emplace_back("annotate:" + name + "=" + text);
    }

    /// @return True when any recorded call contains `text`.
    bool SawCallContaining(const std::string& text) const
    {
        for (const std::string& call : calls)
        {
            if (call.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    /// Recorded calls, in order.
    std::vector<std::string> calls;

private:
    bool m_bAvailable = true;
};

/// Installs a backend for the scope and restores the no-op default however the scope exits.
struct InstalledCaptureTool
{
    /// @param pTool Backend to install; must outlive this object.
    explicit InstalledCaptureTool(ICaptureTool* pTool) { SetCaptureTool(pTool); }
    ~InstalledCaptureTool() { SetCaptureTool(nullptr); }
};
}  // namespace Muyo::TestSupport
