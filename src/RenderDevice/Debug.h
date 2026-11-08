#pragma once
#include <vulkan/vulkan.h>

#include <source_location>
#include <string>
#include <utility>
#include <vector>

namespace Muyo
{
/// Owns a `VK_EXT_debug_utils` messenger. Creation is explicit; the device never installs one itself.
class DebugUtilsMessenger
{
public:
    /// Creates the messenger on `instance`.
    /// @param instance Instance with `VK_EXT_debug_utils` enabled.
    /// @note Must not already have created a messenger; call `Uninitialize` first. The device's
    ///       `InitializeDebugMessenger` enforces that for its own hook.
    /// @note Aborts via `VK_ASSERT` if `vkCreateDebugUtilsMessengerEXT` fails or the loader does not
    ///       provide it; a missing messenger would silently discard every validation message.
    void Initialize(const VkInstance& instance);

    /// Destroys the messenger if one exists. Safe to call more than once.
    /// @param instance The instance the messenger was created on.
    void Uninitialize(const VkInstance& instance);

    /// @return True when `Initialize` has created a messenger and it has not been destroyed.
    bool IsCreated() const { return m_debugUtilsMessenger != VK_NULL_HANDLE; }

private:
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger = VK_NULL_HANDLE;
};

/// @return `VK_EXT_DEBUG_UTILS_EXTENSION_NAME`, the instance extension a debug messenger needs.
///
/// The device does not enable it by itself; this names it for a host that chooses to (#58).
const char* GetValidationExtensionName();

/// @return `"VK_LAYER_KHRONOS_validation"`, the validation layer's name.
///
/// Requesting it is the host's decision. `Muyo::InitializeDeviceWithValidation` is the engine hosts'
/// opt-in default; a library client may pass an empty layer list instead.
const char* GetValidationLayerName();

/// Names a Vulkan object on the process-wide device singleton.
///
/// **Singleton-only.** This overload resolves `GetRenderDevice()` for both the device and the instance,
/// so it names the object on whatever device the process-wide singleton happens to be. It exists for
/// the engine's own resource classes, which use the singleton throughout. A library client - or any
/// caller that owns a `VkRenderDevice` - must use the explicit overload below instead; the debug-marker
/// path deliberately does not use this one (#274).
/// @param objectHandle The object's handle reinterpreted as `uint64_t`; must belong to the singleton.
/// @param objectType The object's `VkObjectType`.
/// @param sName The name to attach.
/// @return The Vulkan result, or `VK_ERROR_EXTENSION_NOT_PRESENT` when the instance has no
///         `vkSetDebugUtilsObjectNameEXT`.
VkResult setDebugUtilsObjectName(uint64_t objectHandle, VkObjectType objectType, const std::string& sName);

/// Names a Vulkan object on an explicitly supplied device.
///
/// Used by `VkRenderDevice::CreateDevice` so a device created by a library client names its objects on
/// its own instance rather than on the process-wide singleton. A caller that already has a device handle
/// should prefer this form.
/// @param device Device that owns the object.
/// @param instance The device's instance.
/// @param objectHandle The object's handle reinterpreted as `uint64_t`.
/// @param objectType The object's `VkObjectType`.
/// @param sName The name to attach.
/// @return The Vulkan result, or `VK_ERROR_EXTENSION_NOT_PRESENT` when the instance has no
///         `vkSetDebugUtilsObjectNameEXT`.
VkResult setDebugUtilsObjectName(VkDevice device, VkInstance instance, uint64_t objectHandle, VkObjectType objectType,
                                 const std::string& sName);

/// Begins a debug-utils label on a queue. Does nothing when the device has no
/// `vkQueueBeginDebugUtilsLabelEXT` entry point.
///
/// The owning device is a parameter rather than a lookup: a client-constructed `VkRenderDevice` owns a
/// device the process-wide singleton does not know about, and resolving the singleton here labelled the
/// wrong device (or nothing, before the singleton was initialized) (#274).
/// @param device Device that owns @p queue.
/// @param queue Queue to label.
/// @param name Label text; consumed by the call.
/// @param color Unused; the label is colourless.
void beginMarker(VkDevice device, VkQueue queue, std::string&& name, uint64_t color);

/// Ends the innermost debug-utils label on a queue.
/// @param device Device that owns @p queue.
/// @param queue Queue whose label is closed.
void endMarker(VkDevice device, VkQueue queue);

/// Begins a debug-utils label on a command buffer. Does nothing when the device has no
/// `vkCmdBeginDebugUtilsLabelEXT` entry point.
/// @param device Device that owns @p cmd.
/// @param cmd Command buffer being recorded.
/// @param name Label text; consumed by the call.
/// @param color Unused; the label is colourless.
void beginMarker(VkDevice device, VkCommandBuffer cmd, std::string&& name, uint64_t color);

/// Ends the innermost debug-utils label on a command buffer.
/// @param device Device that owns @p cmd.
/// @param cmd Command buffer whose label is closed.
void endMarker(VkDevice device, VkCommandBuffer cmd);

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
