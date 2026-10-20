#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>

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
