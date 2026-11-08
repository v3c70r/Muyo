#pragma once
#include <spirv_reflect.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "ShaderReflection.h"

namespace Muyo
{
ShaderReflection FetchShaderReflection(const SpirvCode& spirvCode);

ShaderReflection MergeShaderReflections(const std::vector<ShaderReflection>& reflections);

/// Read a SPIR-V file into 32-bit words.
///
/// The device layer reads shader files because both the pipeline-state builder and the graph need the
/// words; before it moved here it lived in the engine's shader-asset header, which is what kept `muyo_rg`
/// on the engine's headers (#272).
/// @param spvPath Path to a `.spv` file.
/// @return Its words.
/// @throws std::runtime_error when the file cannot be opened, read, or is not a whole number of words.
inline SpirvCode ReadSpirvFile(const std::filesystem::path& spvPath)
{
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Failed to open file");
    const std::streamsize size = file.tellg();
    if (size % static_cast<std::streamsize>(sizeof(uint32_t)) != 0)
        throw std::runtime_error("SPIR-V file size is not a multiple of 4");
    file.seekg(0, std::ios::beg);
    SpirvCode code(static_cast<size_t>(size) / sizeof(uint32_t));
    if (!file.read(reinterpret_cast<char*>(code.data()), size)) throw std::runtime_error("Failed to read file");
    return code;
}

}  // namespace Muyo
