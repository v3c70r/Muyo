#pragma once
#include <imgui.h>  // for ImDrawVert structure
#include <vulkan/vulkan.h>

#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

#include "RenderGraph/PSODesc.h"

namespace Muyo
{
using Index = uint32_t;

/// The engine's scene vertex.
///
/// Its input layout lives in the graph (`RenderGraph::ToVertexLayout`, `VertexType::GENERIC`) because a
/// graph public header may not include this one (#272). The assertions below are the check that the two
/// descriptions still agree: the graph states the bytes, and this struct has to have them in that shape.
struct Vertex
{
    glm::vec3 pos;           ///< Position.
    glm::vec3 normal;        ///< Normal.
    glm::vec4 textureCoord;  ///< Texture coordinate.

    /// @return The graph-declared binding for this vertex shape.
    static VkVertexInputBindingDescription getBindingDescription()
    {
        return RenderGraph::ToVertexLayout(RenderGraph::VertexType::GENERIC).binding;
    }

    /// @return The graph-declared attributes for this vertex shape.
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
    {
        return RenderGraph::ToVertexLayout(RenderGraph::VertexType::GENERIC).attributes;
    }
};

/// The engine's UI vertex, which is ImGui's `ImDrawVert`.
///
/// Its layout is graph-declared for the same reason as `Vertex`'s; the assertions tie the graph's
/// hard-coded stride and offsets to the real struct.
struct UIVertex
{
    /// @return The graph-declared binding for the UI vertex shape.
    static VkVertexInputBindingDescription getBindingDescription()
    {
        return RenderGraph::ToVertexLayout(RenderGraph::VertexType::UI).binding;
    }
    /// @return The graph-declared attributes for the UI vertex shape.
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
    {
        return RenderGraph::ToVertexLayout(RenderGraph::VertexType::UI).attributes;
    }
};
}  // namespace Muyo

// The graph's `ToVertexLayout` states these numbers without this header; if either side changes, the
// mismatch has to be a compile error here rather than a wrong pipeline at draw time.
static_assert(sizeof(Muyo::Vertex) == 40, "RenderGraph::VertexType::GENERIC declares a 40-byte stride");
static_assert(offsetof(Muyo::Vertex, pos) == 0, "RenderGraph::VertexType::GENERIC declares pos at offset 0");
static_assert(offsetof(Muyo::Vertex, normal) == 12, "RenderGraph::VertexType::GENERIC declares normal at offset 12");
static_assert(offsetof(Muyo::Vertex, textureCoord) == 24,
              "RenderGraph::VertexType::GENERIC declares textureCoord at offset 24");
static_assert(sizeof(ImDrawVert) == 20, "RenderGraph::VertexType::UI declares a 20-byte stride");
static_assert(offsetof(ImDrawVert, pos) == 0, "RenderGraph::VertexType::UI declares pos at offset 0");
static_assert(offsetof(ImDrawVert, uv) == 8, "RenderGraph::VertexType::UI declares uv at offset 8");
static_assert(offsetof(ImDrawVert, col) == 16, "RenderGraph::VertexType::UI declares col at offset 16");
