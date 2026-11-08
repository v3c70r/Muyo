#include "Debug/Validation.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "Debug.h"
#include "VkRenderDevice.h"

namespace Muyo
{
void InitializeDeviceWithValidation(VkRenderDevice* pDevice, const std::vector<const char*>& vInstanceExtensions)
{
    // Runtime check rather than an assert: a null device is caller input, and NDEBUG must not turn it
    // into a null dereference.
    if (pDevice == nullptr)
    {
        throw std::invalid_argument("InitializeDeviceWithValidation: the render device is null.");
    }

    std::vector<const char*> vValidationExtensions = vInstanceExtensions;
    const char* sExtension = GetValidationExtensionName();
    // Compare contents, not pointers: a caller may pass a different `const char*` holding the same
    // extension name, and enabling it twice is not what the deduplication is for.
    const bool bAlreadyRequested =
        std::any_of(vValidationExtensions.begin(), vValidationExtensions.end(), [sExtension](const char* sRequested)
                    { return sRequested != nullptr && std::strcmp(sRequested, sExtension) == 0; });
    if (!bAlreadyRequested)
    {
        vValidationExtensions.push_back(sExtension);
    }

    pDevice->Initialize(vValidationExtensions, {GetValidationLayerName()});
    pDevice->InitializeDebugMessenger();
}
}  // namespace Muyo
