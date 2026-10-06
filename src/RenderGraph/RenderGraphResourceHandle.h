#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace Hash
{
/// FNV-1a 64-bit string hash. Reserved for a future hashed resource handle.
constexpr uint64_t StrFnV1A64(std::string_view str)
{
    uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (char c : str)
    {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}
}  // namespace Hash

namespace Muyo::RenderGraph
{
/// Stable string name identifying a resource, node or imported object in the graph.
///
/// The same name used in `AddResource` / `ImportResource` is how nodes refer to it in their
/// `ResourceUse` lists.
using ResourceHandle = std::string;

}  // namespace Muyo::RenderGraph
