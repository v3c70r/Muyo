#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace Hash
{
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

namespace Muyo
{

class RenderGraphResourceHandle
{
public:
    RenderGraphResourceHandle(const RenderGraphResourceHandle&) = default;
    RenderGraphResourceHandle(RenderGraphResourceHandle&&) = default;
    RenderGraphResourceHandle& operator=(const RenderGraphResourceHandle&) = delete;
    RenderGraphResourceHandle& operator=(RenderGraphResourceHandle&&) = delete;
    explicit RenderGraphResourceHandle(std::string_view name) : m_name(name), m_nameHash(Hash::StrFnV1A64(name)) {}
    bool operator==(const RenderGraphResourceHandle& other) const
    {
        return m_nameHash == other.m_nameHash && m_nVersion == other.m_nVersion;
    }
    std::string_view GetName() const { return m_name; }

private:
    const std::string m_name;
    const uint64_t m_nameHash;
    uint32_t m_nVersion = 0;
};

};  // namespace Muyo
