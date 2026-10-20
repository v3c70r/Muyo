#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>
#include <stdexcept>

#include "MeshVertex.h"
#include "RenderGraph/PSODesc.h"

using namespace Muyo::RenderGraph;

// #174. Every conversion in PSODesc.h used to be an exhaustive-looking `switch` with no `return` after it, so a value
// outside the enum fell off the end of a non-void function - undefined behaviour - and `-Wreturn-type` had been
// reporting all seven unnoticed. They are tables indexed by the enum now, with a `COUNT` that makes a table which does
// not cover its enum a compile error.
//
// Both directions are checked here, because either alone proves little: a mapping table can be wrong for every
// in-range value while still throwing correctly, and a bounds check can be absent while every in-range value happens
// to be right.

TEST_CASE("PSODesc: every enumerator maps to the documented Vulkan value", "[PSODesc]")
{
    CHECK(ToVkInputAssembly(PrimitiveTopology::TRIANGLES) == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    CHECK(ToVkInputAssembly(PrimitiveTopology::TRIANGLE_STRIP) == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    CHECK(ToVkInputAssembly(PrimitiveTopology::LINES) == VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    CHECK(ToVkInputAssembly(PrimitiveTopology::LINE_STRIP) == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
    CHECK(ToVkInputAssembly(PrimitiveTopology::POINTS) == VK_PRIMITIVE_TOPOLOGY_POINT_LIST);

    CHECK(ToVkPolygonMode(FillMode::SOLID) == VK_POLYGON_MODE_FILL);
    CHECK(ToVkPolygonMode(FillMode::WIREFRAME) == VK_POLYGON_MODE_LINE);

    CHECK(ToVkCullMode(CullMode::NONE) == VK_CULL_MODE_NONE);
    CHECK(ToVkCullMode(CullMode::FRONT) == VK_CULL_MODE_FRONT_BIT);
    CHECK(ToVkCullMode(CullMode::BACK) == VK_CULL_MODE_BACK_BIT);

    CHECK(ToVkFrontFace(FrontFace::CW) == VK_FRONT_FACE_CLOCKWISE);
    CHECK(ToVkFrontFace(FrontFace::CCW) == VK_FRONT_FACE_COUNTER_CLOCKWISE);

    CHECK(ToVkCompareOp(CompareOp::NEVER) == VK_COMPARE_OP_NEVER);
    CHECK(ToVkCompareOp(CompareOp::LESS) == VK_COMPARE_OP_LESS);
    CHECK(ToVkCompareOp(CompareOp::EQUAL) == VK_COMPARE_OP_EQUAL);
    CHECK(ToVkCompareOp(CompareOp::LESS_EQUAL) == VK_COMPARE_OP_LESS_OR_EQUAL);
    CHECK(ToVkCompareOp(CompareOp::GREATER) == VK_COMPARE_OP_GREATER);
    CHECK(ToVkCompareOp(CompareOp::NOT_EQUAL) == VK_COMPARE_OP_NOT_EQUAL);
    CHECK(ToVkCompareOp(CompareOp::GREATER_EQUAL) == VK_COMPARE_OP_GREATER_OR_EQUAL);
    CHECK(ToVkCompareOp(CompareOp::ALWAYS) == VK_COMPARE_OP_ALWAYS);

    CHECK(ToVkBlendFactor(BlendFactor::ONE) == VK_BLEND_FACTOR_ONE);
    CHECK(ToVkBlendFactor(BlendFactor::ZERO) == VK_BLEND_FACTOR_ZERO);
    CHECK(ToVkBlendFactor(BlendFactor::SRC_ALPHA) == VK_BLEND_FACTOR_SRC_ALPHA);
    CHECK(ToVkBlendFactor(BlendFactor::ONE_MINUS_SRC_ALPHA) == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);

    CHECK(ToVkBlendOp(BlendOp::ADD) == VK_BLEND_OP_ADD);
    CHECK(ToVkBlendOp(BlendOp::SUBTRACT) == VK_BLEND_OP_SUBTRACT);
}

TEST_CASE("PSODesc: a value outside an enum throws instead of being undefined behaviour", "[PSODesc]")
{
    // `COUNT` first, because it is the boundary every table has to reject and the easiest one to get wrong by
    // indexing without a bounds check.
    CHECK_THROWS_AS(ToVkInputAssembly(PrimitiveTopology::COUNT), std::invalid_argument);
    CHECK_THROWS_AS(ToVkPolygonMode(FillMode::COUNT), std::invalid_argument);
    CHECK_THROWS_AS(ToVkCullMode(CullMode::COUNT), std::invalid_argument);
    CHECK_THROWS_AS(ToVkFrontFace(FrontFace::COUNT), std::invalid_argument);
    CHECK_THROWS_AS(ToVkCompareOp(CompareOp::COUNT), std::invalid_argument);
    CHECK_THROWS_AS(ToVkBlendFactor(BlendFactor::COUNT), std::invalid_argument);
    CHECK_THROWS_AS(ToVkBlendOp(BlendOp::COUNT), std::invalid_argument);

    // And a value far outside, which is what a cast from configuration or reflection data looks like. This is the
    // input that used to be undefined behaviour: with the table indexed and no bounds check it reads past the end.
    CHECK_THROWS_AS(ToVkInputAssembly(static_cast<PrimitiveTopology>(200)), std::invalid_argument);
    CHECK_THROWS_AS(ToVkCompareOp(static_cast<CompareOp>(255)), std::invalid_argument);

    // The message names the type and the value: an exception that says only "invalid argument" leaves the reader to
    // work out which of seven conversions produced it.
    CHECK_THROWS_WITH(
        ToVkInputAssembly(static_cast<PrimitiveTopology>(200)),
        Catch::Matchers::ContainsSubstring("PrimitiveTopology") && Catch::Matchers::ContainsSubstring("200"));
}

// #178. The eighth conversion-shaped site in this header was different in kind: a void-context `switch` on
// `VertexType`. A value outside the enum matched no case, both vectors stayed empty, and the pipeline was built with
// no vertex input - creation succeeded and the mistake surfaced at draw time as a validation error far from the value
// that caused it. It is a table now, and both directions are checked for the same reason as above: a total mapping
// that throws can still map every in-range value wrongly, and a correct mapping can be indexed without a bound.

TEST_CASE("PSODesc: every vertex type maps to its documented input layout", "[PSODesc]")
{
    const VertexInputLayout generic = ToVertexLayout(VertexType::GENERIC);
    CHECK(generic.binding.binding == 0);
    CHECK(generic.binding.stride == sizeof(Muyo::Vertex));
    CHECK(generic.binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX);
    REQUIRE(generic.attributes.size() == 3);
    CHECK(generic.attributes[0].location == 0);
    CHECK(generic.attributes[0].binding == 0);
    CHECK(generic.attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT);
    CHECK(generic.attributes[0].offset == offsetof(Muyo::Vertex, pos));
    CHECK(generic.attributes[1].location == 1);
    CHECK(generic.attributes[1].format == VK_FORMAT_R32G32B32_SFLOAT);
    CHECK(generic.attributes[1].offset == offsetof(Muyo::Vertex, normal));
    CHECK(generic.attributes[2].location == 2);
    CHECK(generic.attributes[2].format == VK_FORMAT_R32G32B32A32_SFLOAT);
    CHECK(generic.attributes[2].offset == offsetof(Muyo::Vertex, textureCoord));

    const VertexInputLayout ui = ToVertexLayout(VertexType::UI);
    CHECK(ui.binding.binding == 0);
    CHECK(ui.binding.stride == sizeof(ImDrawVert));
    CHECK(ui.binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX);
    REQUIRE(ui.attributes.size() == 3);
    CHECK(ui.attributes[0].location == 0);
    CHECK(ui.attributes[0].format == VK_FORMAT_R32G32_SFLOAT);
    CHECK(ui.attributes[0].offset == offsetof(ImDrawVert, pos));
    CHECK(ui.attributes[1].location == 1);
    CHECK(ui.attributes[1].format == VK_FORMAT_R32G32_SFLOAT);
    CHECK(ui.attributes[1].offset == offsetof(ImDrawVert, uv));
    CHECK(ui.attributes[2].location == 2);
    CHECK(ui.attributes[2].format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(ui.attributes[2].offset == offsetof(ImDrawVert, col));

    // The two layouts must be different, or the table could be returning one of them for both enumerators and every
    // check above would still pass for whichever it returned first.
    CHECK(generic.binding.stride != ui.binding.stride);
}

TEST_CASE("PSODesc: a vertex type outside the enum throws rather than building an empty layout", "[PSODesc]")
{
    // `COUNT` first: it is the boundary every table has to reject and the easiest one to get wrong by indexing
    // without a bounds check. 200 is what a cast from configuration or reflection data looks like.
    CHECK_THROWS_AS(ToVertexLayout(VertexType::COUNT), std::invalid_argument);
    CHECK_THROWS_AS(ToVertexLayout(static_cast<VertexType>(200)), std::invalid_argument);
    CHECK_THROWS_WITH(ToVertexLayout(static_cast<VertexType>(200)),
                      Catch::Matchers::ContainsSubstring("VertexType") && Catch::Matchers::ContainsSubstring("200"));

    // And through the real entry point, with a null device: the throw has to happen before any Vulkan call, which is
    // what makes the failure land at the bad value rather than at pipeline creation. The old `switch` fell through
    // to a pipeline with zero vertex bindings instead.
    PSODesc desc{};
    desc.vertexType = static_cast<VertexType>(200);
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    CHECK_THROWS_AS(CreatePipelineFromPSODesc(desc, VK_NULL_HANDLE, {}, VK_NULL_HANDLE, renderingInfo),
                    std::invalid_argument);
}
