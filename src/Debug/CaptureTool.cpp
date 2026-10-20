#include "CaptureTool.h"

#include <utility>

namespace Muyo
{
namespace
{
/// The no-op default, returned by reference so no caller has to handle a null tool.
NoopCaptureTool& DefaultTool()
{
    static NoopCaptureTool s_noop;
    return s_noop;
}

/// The installed backend, or nullptr for "the default".
/// A raw pointer because the host owns the backend and this module must not extend its lifetime.
ICaptureTool* g_pInstalledTool = nullptr;
}  // namespace

ICaptureTool::~ICaptureTool() = default;

void NoopCaptureTool::BeginMarker(const std::string&)
{
    // Deliberately silent rather than logging: a marker is emitted per node per execution, and a warning per marker
    // would bury the run it is meant to help with.
}

void NoopCaptureTool::Annotate(const std::string&, const std::string&) {}

void SetCaptureTool(ICaptureTool* pTool) { g_pInstalledTool = pTool; }

ICaptureTool& GetCaptureTool() { return g_pInstalledTool != nullptr ? *g_pInstalledTool : DefaultTool(); }

bool IsCaptureToolAvailable() { return g_pInstalledTool != nullptr && g_pInstalledTool->IsAvailable(); }

ScopedCaptureMarker::ScopedCaptureMarker(std::string_view name) : m_name(name) { GetCaptureTool().BeginMarker(m_name); }

ScopedCaptureMarker::~ScopedCaptureMarker() { GetCaptureTool().EndMarker(); }
}  // namespace Muyo
