#pragma once

#include <muyo_rg/version.h>

#include <cstdint>

/// @file
/// Compile-time version of the `muyo_rg` RenderGraph library.
///
/// This is the Doxygen-documented face of the generated `<muyo_rg/version.h>`. The macros there are
/// the form a consumer uses in `#if`; the constants below are the same values as typed C++, so the
/// API reference can state them and a program can read them without the preprocessor.
///
/// The version is declared once, by `project(Muyo VERSION ...)` in `CMakeLists.txt`, and both forms
/// are generated from it. `src/tests/MuyoVersionTest.cpp` fails the build if they disagree.

namespace Muyo::RenderGraph
{
/// Major version component (`MUYO_RG_VERSION_MAJOR`).
inline constexpr std::uint32_t VERSION_MAJOR = MUYO_RG_VERSION_MAJOR;

/// Minor version component (`MUYO_RG_VERSION_MINOR`).
inline constexpr std::uint32_t VERSION_MINOR = MUYO_RG_VERSION_MINOR;

/// Patch version component (`MUYO_RG_VERSION_PATCH`).
inline constexpr std::uint32_t VERSION_PATCH = MUYO_RG_VERSION_PATCH;

/// The whole version as one comparable integer (`MUYO_RG_VERSION`), in Vulkan's layout.
inline constexpr std::uint32_t VERSION = MUYO_RG_VERSION;

/// The whole version as a `"major.minor.patch"` string (`MUYO_RG_VERSION_STRING`).
inline constexpr const char* VERSION_STRING = MUYO_RG_VERSION_STRING;
}  // namespace Muyo::RenderGraph
