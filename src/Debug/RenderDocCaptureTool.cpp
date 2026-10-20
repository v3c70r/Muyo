#include "RenderDocCaptureTool.h"

namespace Muyo
{
void RenderDocCaptureTool::BeginMarker(const std::string&)
{
    // The Vulkan debug label is already visible to RenderDoc, so there is nothing to mirror here.
}

void RenderDocCaptureTool::Annotate(const std::string&, const std::string&) {}
}  // namespace Muyo
