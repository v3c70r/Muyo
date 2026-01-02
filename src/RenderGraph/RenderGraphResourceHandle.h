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

namespace Muyo::RenderGraph
{

//class ResourceHandle
//{
//public:
//    ResourceHandle(const ResourceHandle&) = default;
//    ResourceHandle(ResourceHandle&&) = default;
//    ResourceHandle& operator=(const ResourceHandle&) = delete;
//    ResourceHandle& operator=(ResourceHandle&&) = delete;
//    explicit ResourceHandle(const std::string& name) : m_name(name), m_nameHash(Hash::StrFnV1A64(name)) {}
//    bool operator==(const ResourceHandle& other) const
//    {
//        return m_nameHash == other.m_nameHash;
//    }
//    std::string_view GetName() const { return m_name; }
//
//private:
//    const std::string m_name;
//    const uint64_t m_nameHash;
//};
//
using ResourceHandle = std::string;

};  // namespace Muyo
