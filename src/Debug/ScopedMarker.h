#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <utility>

#include "Debug.h"
#include "Debug/CaptureTool.h"

namespace Muyo
{
/// Opens a Vulkan debug label and a capture-tool marker, and closes both on destruction.
///
/// The Vulkan label closes first, then the capture marker, so the two scopes nest the same way they
/// were opened. @p T is `VkQueue` or `VkCommandBuffer`.
///
/// This lives in the engine rather than `muyo_device` because it composes two concerns: the Vulkan
/// label belongs to the device layer, and the capture backend is host policy (`CaptureTool.h`). Keeping
/// it out of `Debug.h` is what lets a consumer link `muyo_device` alone without unresolved
/// `ScopedCaptureMarker` symbols; the graph and the engine include this header explicitly (#275).
template <class T>
class ScopedMarker
{
public:
    /// @param vkObj Queue or command buffer to label.
    /// @param marker Label text.
    ScopedMarker(T vkObj, std::string&& marker) : m_captureMarker(marker)
    {
        m_markedVkObject = vkObj;
        beginMarker(vkObj, std::forward<std::string>(marker), uint64_t(0));
    }
    /// Closes the Vulkan label, then the capture marker.
    ~ScopedMarker() { endMarker(m_markedVkObject); }

private:
    T m_markedVkObject = VK_NULL_HANDLE;
    // Routed through the capture-tool interface, so every backend receives node and segment markers for free. The
    // Vulkan label above is unchanged; this is the *other* consumer of the same name.
    ScopedCaptureMarker m_captureMarker;
};
}  // namespace Muyo

// Create a macro to generate local variables
#define TOKENPASTE(x, y) x##y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define SCOPED_MARKER(OBJ, MESSAGE) auto TOKENPASTE2(scoped_marker_, __LINE__) = ScopedMarker(OBJ, MESSAGE)
