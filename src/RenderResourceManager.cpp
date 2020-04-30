#include "RenderResourceManager.h"

static RenderResourceManager resourceManager;

RenderResourceManager* GetRenderResourceManager()
{
    return &resourceManager;
}
