// Persistent render resource names that are used across mutliple render passes

#pragma once
#include <string>
namespace Muyo
{
// Buffers. The app's graph nodes import these; they live here until #51 moves the last shared names
// out of src/RenderPasses/.
static const std::string LIGHT_COUNT = "light count";
static const std::string LIGHT_DATA = "light data";

};  // namespace Muyo
