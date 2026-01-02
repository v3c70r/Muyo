#pragma once
#include <spirv_reflect.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "ShaderReflection.h"

namespace Muyo
{
ShaderReflection FetchShaderReflection(const SpirvCode& spirvCode);

ShaderReflection MergeShaderReflections(const std::vector<ShaderReflection>& reflections);

}  // namespace Muyo
