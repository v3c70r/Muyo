// The version macros are generated from `project(Muyo VERSION ...)`; this test checks the generated
// header against that project version, so a header that drifts from the declaration fails here instead
// of making a consumer's `#if` a statement about a version the library is not (#87).
//
// The expected values are passed by CMake from `PROJECT_VERSION_*`, independently of the configured
// header. The test lives in `rg_tests`, which links only `muyo_rg` and `muyo_device`, so it also
// proves the generated header is reachable through the target's public include interface rather than
// through the source tree or an engine-only include path.

#include <muyo_rg/version.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string_view>

#include "RenderGraph/MuyoVersion.h"

namespace
{
constexpr std::uint32_t PackVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
{
    return (major << 22) | (minor << 12) | patch;
}
}  // namespace

TEST_CASE("Version: the public macros agree with the CMake project version", "[Version]")
{
    STATIC_REQUIRE(MUYO_RG_VERSION_MAJOR == MUYO_RG_EXPECTED_MAJOR);
    STATIC_REQUIRE(MUYO_RG_VERSION_MINOR == MUYO_RG_EXPECTED_MINOR);
    STATIC_REQUIRE(MUYO_RG_VERSION_PATCH == MUYO_RG_EXPECTED_PATCH);
    STATIC_REQUIRE(std::string_view(MUYO_RG_VERSION_STRING) == std::string_view(MUYO_RG_EXPECTED_STRING));

    STATIC_REQUIRE(MUYO_RG_VERSION == PackVersion(MUYO_RG_VERSION_MAJOR, MUYO_RG_VERSION_MINOR, MUYO_RG_VERSION_PATCH));
    STATIC_REQUIRE(MUYO_RG_VERSION == MUYO_RG_MAKE_VERSION(MUYO_RG_VERSION_VARIANT, MUYO_RG_VERSION_MAJOR,
                                                           MUYO_RG_VERSION_MINOR, MUYO_RG_VERSION_PATCH));

    // The point of the combined integer is that a consumer can compare it in `#if`. Compile such a
    // comparison here so the macro is checked by the preprocessor, not only by the C++ compiler: a
    // cast in the expansion would make this the only form that fails.
#if !(MUYO_RG_VERSION >= MUYO_RG_MAKE_VERSION(0, 0, 0, 0))
#error "MUYO_RG_VERSION must remain usable in a preprocessor comparison"
#endif

    // The documented C++ constants are the same values as the macros.
    STATIC_REQUIRE(Muyo::RenderGraph::VERSION_MAJOR == MUYO_RG_VERSION_MAJOR);
    STATIC_REQUIRE(Muyo::RenderGraph::VERSION_MINOR == MUYO_RG_VERSION_MINOR);
    STATIC_REQUIRE(Muyo::RenderGraph::VERSION_PATCH == MUYO_RG_VERSION_PATCH);
    STATIC_REQUIRE(Muyo::RenderGraph::VERSION == MUYO_RG_VERSION);
    CHECK(std::string_view(Muyo::RenderGraph::VERSION_STRING) == std::string_view(MUYO_RG_EXPECTED_STRING));
}
