#pragma once
#include <vulkan/vulkan.h>

#include <source_location>
#include <string>
#include <vector>

namespace Muyo
{
class DebugUtilsMessenger
{
public:
    void Initialize(const VkInstance& instance);
    void Uninitialize(const VkInstance& instance);

private:
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger = VK_NULL_HANDLE;
};

const char* GetValidationExtensionName();
const char* GetValidationLayerName();

VkResult setDebugUtilsObjectName(uint64_t objectHandle, VkObjectType objectType, const std::string& sName);

// Scoped markers
void beginMarker(VkQueue queue, std::string&& name, uint64_t color);
void endMarker(VkQueue queue);

void beginMarker(VkCommandBuffer cmd, std::string&& name, uint64_t color);
void endMarker(VkCommandBuffer cmd);

// Using template to handle command buffer and queue markers
template <class T>
class ScopedMarker
{
public:
    ScopedMarker(T vkObj, std::string&& marker)
    {
        m_markedVkObject = vkObj;
        beginMarker(vkObj, std::forward<std::string>(marker), uint64_t(0));
    }
    ~ScopedMarker() { endMarker(m_markedVkObject); }

private:
    T m_markedVkObject = VK_NULL_HANDLE;
};

/// Reports a failed Vulkan call and terminates the process.
/// @param result The failed result.
/// @param location Call site, captured by default at each call.
[[noreturn]] void VKAssertFailed(VkResult result, const std::source_location& location);

/// Assert that a Vulkan call succeeded, in **every** build configuration.
///
/// Deliberately not `assert`: `NDEBUG` removes asserts, so a release build would ignore the failure
/// and continue with whatever the call left behind - a null handle, an unallocated buffer - and the
/// symptom would surface somewhere unrelated, with no diagnostic. Reports the failing result and the
/// call site (obtained by default from `std::source_location::current()`), then aborts.
///
/// Aborting rather than throwing keeps the ~70 call sites free of exception-safety requirements;
/// every failure it guards (device or memory creation) is unrecoverable anyway. A function rather
/// than a macro so qualified calls such as `Muyo::VK_ASSERT(...)` keep working.
/// @param result Result of the Vulkan call.
/// @param location Call site, supplied by the default argument.
inline void VK_ASSERT(VkResult result, const std::source_location& location = std::source_location::current())
{
    if (result != VK_SUCCESS) VKAssertFailed(result, location);
}

/// Reports a failed precondition and throws, in **every** build configuration.
///
/// The companion to `VK_ASSERT` for conditions that are falsifiable by *input* - a layer or instance
/// extension that was requested and is not there, a machine with no Vulkan device - rather than by
/// construction. Plain `assert` is wrong for those: `NDEBUG` removes it, so a release build proceeds
/// and fails somewhere unrelated, or not at all. Reports the message and the call site, then throws.
///
/// Throws rather than aborting, unlike `VK_ASSERT`: these are reachable at startup from a
/// configuration mistake, where unwinding lets the caller report which request was wrong and lets a
/// test assert on the message. `VK_ASSERT` keeps aborting because its ~70 call sites guard failed
/// Vulkan calls (device, memory, command buffers) that are unrecoverable and that must not impose
/// exception-safety requirements on every caller.
/// @param message What was expected and what was found.
/// @param location Call site, supplied by the default argument.
[[noreturn]] void FatalError(std::string message,
                             const std::source_location& location = std::source_location::current());

/// Reports a failed precondition and aborts, in **every** build configuration.
///
/// The same idea as `FatalError`, for the paths where unwinding is not an option: a Vulkan debug
/// callback runs on the driver's stack inside a C callback, so throwing out of it would propagate an
/// exception through a C ABI boundary, which is worse than the failure being reported. Aborts
/// instead, and is `[[noreturn]]` so a caller cannot ignore it.
/// @param message What failed.
/// @param location Call site, supplied by the default argument.
[[noreturn]] void FatalAbort(std::string message,
                             const std::source_location& location = std::source_location::current());
}  // namespace Muyo

// Create a macro to generate local variables
#define TOKENPASTE(x, y) x##y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define SCOPED_MARKER(OBJ, MESSAGE) auto TOKENPASTE2(scoped_marker_, __LINE__) = ScopedMarker(OBJ, MESSAGE)
