#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "CaptureToolRecorder.h"
#include "Debug/CaptureTool.h"
#include "Debug/RenderDocCaptureTool.h"

using namespace Muyo;

namespace
{
using Muyo::TestSupport::InstalledCaptureTool;
using Muyo::TestSupport::RecordingCaptureTool;
}  // namespace

TEST_CASE("CaptureTool: the default backend is a silent no-op", "[CaptureTool]")
{
    // No call site checks for a tool, so the default has to be safe to call. This is the property that lets
    // `ScopedMarker` route unconditionally in a build where nothing installed a backend.
    CHECK_FALSE(GetCaptureTool().IsAvailable());
    CHECK_FALSE(IsCaptureToolAvailable());

    // Emitting a marker through the default does nothing and cannot fail; there is no assertion to make beyond "it
    // returns", which is the point.
    {
        ScopedCaptureMarker marker("nothing-happens");
    }
    GetCaptureTool().Annotate("resource", "text");

    // Installing a backend that reports itself unavailable leaves `IsCaptureToolAvailable` false while still
    // forwarding calls - the two are different questions ("is one installed" vs "can it capture").
    RecordingCaptureTool unavailable(/*bAvailable=*/false);
    InstalledCaptureTool installed(&unavailable);
    CHECK_FALSE(IsCaptureToolAvailable());
    {
        ScopedCaptureMarker marker("still-forwarded");
    }
    CHECK(unavailable.calls == std::vector<std::string>{"begin:still-forwarded", "end"});
}

TEST_CASE("CaptureTool: markers nest and close in reverse order", "[CaptureTool]")
{
    // The property a backend needs to draw ranges: a marker that closes out of order produces a malformed trace in
    // every profiler. RAII gives it by construction, and this asserts the construction does what it claims.
    RecordingCaptureTool tool;
    InstalledCaptureTool installed(&tool);

    {
        ScopedCaptureMarker outer("segment 0");
        {
            ScopedCaptureMarker inner("shadow");
        }
        ScopedCaptureMarker sibling("gbuffer");
    }

    CHECK(tool.calls ==
          std::vector<std::string>{"begin:segment 0", "begin:shadow", "end", "begin:gbuffer", "end", "end"});
}

TEST_CASE("CaptureTool: installing a backend is what routes markers, and uninstalling stops it", "[CaptureTool]")
{
    // `SetCaptureTool(nullptr)` has to restore the default rather than leaving a dangling pointer, because a backend
    // owned by a scope that has ended is exactly what a test - or a shutdown path - does.
    RecordingCaptureTool first;
    {
        InstalledCaptureTool installed(&first);
        {
            ScopedCaptureMarker marker("one");
        }
    }
    CHECK(first.calls.size() == 2);

    // Back to the default: nothing receives the marker, and nothing crashes.
    {
        ScopedCaptureMarker marker("two");
    }
    CHECK(first.calls.size() == 2);
    CHECK_FALSE(GetCaptureTool().IsAvailable());

    RecordingCaptureTool second;
    {
        InstalledCaptureTool installed(&second);
        {
            ScopedCaptureMarker marker("three");
        }
    }
    CHECK(first.calls.size() == 2);  // the previous backend is not still installed
    CHECK(second.calls == std::vector<std::string>{"begin:three", "end"});
}

TEST_CASE("CaptureTool: the RenderDoc backend reports availability from the injection, not a flag", "[CaptureTool]")
{
    // RenderDoc is not injected into a test process, so the backend must say so rather than pretending to capture.
    // This is also the check that the vendor header is reachable from `muyo` (it used to compile only in `tests`).
    RenderDocCaptureTool renderDoc("test_capture");
    CHECK(renderDoc.GetCaptureName() == "test_capture");
    CHECK(renderDoc.IsAvailable() == (GetRenderDocAPI() != nullptr));

    // Begin/End on an unavailable backend must be harmless: the engine calls them without checking availability when a
    // tool is installed, and a host that installs RenderDoc unconditionally is the normal case.
    renderDoc.BeginCapture();
    renderDoc.EndCapture();
    // Markers are deliberately no-ops in this backend; the Vulkan debug labels already carry the names into a capture.
    renderDoc.BeginMarker("shadow");
    renderDoc.EndMarker();
    renderDoc.Annotate("shadow.depth", "2048x2048 D32");
}
