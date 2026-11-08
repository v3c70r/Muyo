#pragma once

#include <vector>

namespace Muyo
{
class VkRenderDevice;

/// Initializes a device with the validation layer and debug messenger - this repository's host default.
///
/// Validation enablement is host policy, not device behaviour (#58): `VkRenderDevice::Initialize`
/// appends neither `VK_LAYER_KHRONOS_validation` nor `VK_EXT_debug_utils`, and creates no messenger.
/// The engine's apps and the test host call this function so they keep the previous behaviour, where a
/// validation error aborts the run (AGENTS.md section 4). A consumer that does not want the layer calls
/// `VkRenderDevice::Initialize` directly with an empty layer list.
///
/// The messenger is destroyed by `VkRenderDevice::Unintialize`, so no matching teardown call is needed.
///
/// @param pDevice Device to initialize. Must be non-null.
/// @param vInstanceExtensions Extensions the host needs; `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` is appended
///        unless it is already present.
/// @throws std::invalid_argument if `pDevice` is null; std::runtime_error if the validation layer or an
///         instance extension is unavailable, the loader is older than Vulkan 1.4, or no physical device
///         is found.
void InitializeDeviceWithValidation(VkRenderDevice* pDevice, const std::vector<const char*>& vInstanceExtensions);
}  // namespace Muyo
