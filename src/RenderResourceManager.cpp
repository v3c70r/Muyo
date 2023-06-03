#include "RenderResourceManager.h"

namespace Muyo
{

static RenderResourceManager resourceManager;

RenderResourceManager* GetRenderResourceManager()
{
    return &resourceManager;
}

}  // namespace Muyo
