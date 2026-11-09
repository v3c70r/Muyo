#include "RenderGraph/ShaderSourceProvider.h"

#include <utility>

#include "ShaderReflectionFetcher.h"

namespace Muyo::RenderGraph
{
FileShaderSourceProvider::FileShaderSourceProvider(std::filesystem::path searchPath)
    : m_searchPath(std::move(searchPath))
{
}

std::optional<std::vector<uint32_t>> FileShaderSourceProvider::LoadShader(const std::string& shaderName) const
{
    // The same construction and the same reader the graph used before the provider existed, so a caller
    // that supplies none sees byte-for-byte the old behaviour. A missing file throws "Failed to open
    // file"; an empty file produces no words, which is a load failure rather than an exception.
    std::vector<uint32_t> spirv = Muyo::ReadSpirvFile(m_searchPath / (shaderName + ".spv"));
    if (spirv.empty()) return std::nullopt;
    return spirv;
}
}  // namespace Muyo::RenderGraph
