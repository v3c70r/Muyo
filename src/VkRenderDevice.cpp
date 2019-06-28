#include "VkRenderDevice.h"

static VkRenderDevice renderDevice;

VkRenderDevice* GetRenderDevice()
{
    return &renderDevice;
}

