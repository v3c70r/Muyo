// Persistent render resource names shared by the app and the engine

#pragma once
#include <string>
namespace Muyo
{
// Buffers the app's graph nodes import. Moved here from src/RenderPasses/ when the legacy renderer was
// deleted (#51).
static const std::string LIGHT_COUNT = "light count";
static const std::string LIGHT_DATA = "light data";

};  // namespace Muyo
