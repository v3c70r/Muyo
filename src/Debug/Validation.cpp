#include "Debug/Validation.h"

#include <algorithm>
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
    if (std::find(vValidationExtensions.begin(), vValidationExtensions.end(), sExtension) ==
        vValidationExtensions.end())
    {
        vValidationExtensions.push_back(sExtension);
    }

    pDevice->Initialize(vValidationExtensions, {GetValidationLayerName()});
    pDevice->InitializeDebugMessenger();
}
}  // namespace Muyo
