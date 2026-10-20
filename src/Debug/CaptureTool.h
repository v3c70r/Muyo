#pragma once
#include <string>
#include <string_view>

namespace Muyo
{
/// A capture or profiling backend: begin and end a capture, and label the work inside it.
///
/// The point of the interface is that a *host* chooses a tool and the engine does not know which one: RenderDoc
/// covers general validation but its replay shows neither ray tracing nor queue scheduling, so a per-tool backend is
/// the shape that scales to the vendor profilers (Radeon GPU Profiler, Nsight, ...) without either of them reaching
/// into `src/RenderGraph/`.
///
/// Lives in `muyo`, not `muyo_rg`: choosing a tool is host policy, the same split #58 makes for validation policy.
class ICaptureTool
{
public:
    /// @brief Required so a backend can be destroyed through the interface.
    virtual ~ICaptureTool();

    /// @return True when this backend can actually capture in this process.
    virtual bool IsAvailable() const = 0;

    /// Start a capture. Called once per captured scope; an implementation may ignore nesting it cannot express.
    virtual void BeginCapture() = 0;

    /// Finish the capture started by `BeginCapture`.
    virtual void EndCapture() = 0;

    /// @param name Marker name, e.g. a node or segment name.
    virtual void BeginMarker(const std::string& name) = 0;

    /// Close the innermost marker opened by `BeginMarker`.
    virtual void EndMarker() = 0;

    /// @param name Resource or pass the annotation belongs to.
    /// @param text What to say about it.
    virtual void Annotate(const std::string& name, const std::string& text) = 0;
};

/// The default backend: every call does nothing.
///
/// Installed by default so no call site needs a null check, which is the same reason `GetCaptureTool()` returns a
/// reference rather than a pointer.
class NoopCaptureTool final : public ICaptureTool
{
public:
    /// @return Always false: there is nothing to capture with.
    bool IsAvailable() const override { return false; }
    /// @brief Does nothing.
    void BeginCapture() override {}
    /// @brief Does nothing.
    void EndCapture() override {}
    /// @param name Ignored.
    void BeginMarker(const std::string& name) override;
    /// @brief Does nothing.
    void EndMarker() override {}
    /// @param name Ignored.
    /// @param text Ignored.
    void Annotate(const std::string& name, const std::string& text) override;
};

/// Install the process-wide capture tool.
///
/// Process-wide rather than passed through every call: markers are emitted from deep inside the graph's recording,
/// and threading a tool pointer through the recording API for a diagnostic backend would put a host concern into the
/// library's signatures.
/// @param pTool Backend to install. `nullptr` restores the no-op default.
void SetCaptureTool(ICaptureTool* pTool);

/// @return The installed tool, or the no-op default. Never null.
ICaptureTool& GetCaptureTool();

/// @return True when a backend other than the no-op default is installed *and* reports itself available.
bool IsCaptureToolAvailable();

/// Opens a marker on the installed tool and closes it on destruction.
///
/// Split out of the Vulkan `ScopedMarker` so the pairing and order are testable: `ScopedMarker` emits Vulkan debug
/// labels, which need a real device, while this needs nothing. `ScopedMarker` composes one of these, so routing a
/// marker to a backend is a one-line composition that a review can read.
class ScopedCaptureMarker
{
public:
    /// @param name Marker name; copied, because the tool may outlive the caller's string.
    explicit ScopedCaptureMarker(std::string_view name);
    /// Closes the marker.
    ~ScopedCaptureMarker();

    /// Owning the marker is the whole point, so copying would double-close it.
    ScopedCaptureMarker(const ScopedCaptureMarker&) = delete;
    /// Deleted for the same reason as the copy constructor.
    ScopedCaptureMarker& operator=(const ScopedCaptureMarker&) = delete;
    /// Deleted for the same reason as the copy constructor.
    ScopedCaptureMarker(ScopedCaptureMarker&&) = delete;
    /// Deleted for the same reason as the copy constructor.
    ScopedCaptureMarker& operator=(ScopedCaptureMarker&&) = delete;

private:
    std::string m_name;  ///< Kept so `EndMarker` can be matched in a trace of calls, and for debugging.
};
}  // namespace Muyo
