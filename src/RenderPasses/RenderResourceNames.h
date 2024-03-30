// Persistent render resource names that are used across mutliple render passes

#pragma once
#include <string>
namespace Muyo
{
// Images
static const std::string OPAQUE_LIGHTING_OUTPUT_ATTACHMENT_NAME = "opaqueLightingOutput";

// Buffers
static const std::string PER_VIEW = "perView";
static const std::string LIGHT_COUNT = "light count";
static const std::string LIGHT_DATA = "light data";



};
