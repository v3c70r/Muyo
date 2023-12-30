#include "RenderPassOpaqueLighting.h"

namespace Muyo
{

void RenderPassOpaqueLighting::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_renderArea);
}
}
