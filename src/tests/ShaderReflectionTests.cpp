#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "GraphicsTestEnv.h"
#include "ShaderAsset.h"
#include "ShaderReflectionFetcher.h"
#include "catch2/catch_message.hpp"
namespace Muyo
{

namespace fs = std::filesystem;
TEST_CASE("ShaderReflectionFetcher: SPIR-V Reflection from file", "[ShaderReflectionFetcher]")
{
    // Path to the compiled SPIR-V file
    fs::path spirvPath = fs::path("shaders") / "copyBuffer.comp.slang.spv";
    auto spirvCode = ReadSpv(spirvPath);

    ShaderReflection reflection = Muyo::FetchShaderReflection(spirvCode);

    // Check that descriptor sets are extracted
    REQUIRE(reflection.descriptorBindings.size() == 2);

    // Check descriptor set/binding and types
    // gInput: set 0, binding 0, likely VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    REQUIRE(reflection.descriptorBindings[0].name == "gInput");
    REQUIRE(reflection.descriptorBindings[0].set == 0);
    REQUIRE(reflection.descriptorBindings[0].binding == 0);
    REQUIRE(reflection.descriptorBindings[0].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    // gOutput: set 1, binding 0, likely VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    REQUIRE(reflection.descriptorBindings[1].name == "gOutput");
    REQUIRE(reflection.descriptorBindings[1].set == 1);
    REQUIRE(reflection.descriptorBindings[1].binding == 0);
    REQUIRE(reflection.descriptorBindings[1].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    // Check push constant range
    REQUIRE(reflection.pushConstantRanges.size() == 1);
    REQUIRE(reflection.pushConstantRanges[0].size == sizeof(uint32_t));

    // Check entry point
    REQUIRE(reflection.entryPoints.size() == 1);
    REQUIRE(reflection.entryPoints[0].name == "main");
    REQUIRE(reflection.entryPoints[0].stage == VK_SHADER_STAGE_COMPUTE_BIT);

    // Check workgroup size (from [numthreads(256,1,1)])
    REQUIRE(reflection.entryPoints[0].workgroupSize.has_value());
    REQUIRE(reflection.entryPoints[0].workgroupSize->x == 256);
    REQUIRE(reflection.entryPoints[0].workgroupSize->y == 1);
    REQUIRE(reflection.entryPoints[0].workgroupSize->z == 1);
};
TEST_CASE("ShaderReflectionFetcher: Partially build pipeline", "[ShaderReflectionFetcher]")
{
    fs::path vertSpirvPath = fs::path("shaders") / "GBuffer.vert.spv";
    auto vertByteCode = ReadSpv(vertSpirvPath);
    fs::path fragSpirvPath = fs::path("shaders") / "GBuffer.frag.spv";
    auto fragByteCode = ReadSpv(fragSpirvPath);

    std::vector<ShaderReflection> reflections;
    reflections.push_back(Muyo::FetchShaderReflection(vertByteCode));
    reflections.push_back(Muyo::FetchShaderReflection(fragByteCode));
    ShaderReflection mergedReflection = Muyo::MergeShaderReflections(reflections);
    // Check that descriptor sets are merged correctly
    // For GBuffer.vert and GBuffer.frag, expect:
    // - CAMERA_UBO at set=0, binding=0 (from Camera.h)
    // - PerObjData at set=1, binding=0 (from SharedStructures.h)
    // - MATERIAL_SSBO at set=2, binding=0 (from material.h, frag only)
    // - Output variables in vert, input variables in frag

    // CAMERA_UBO (set=0, binding=0) should be present (from Camera.h)
    auto hasCameraUBO =
        std::ranges::any_of(mergedReflection.descriptorBindings,
                            [](const ShaderReflection::DescriptorBinding& b) { return b.set == 0 && b.binding == 0; });
    REQUIRE(hasCameraUBO);

    // PerObjData (set=1, binding=0) should be present (from SharedStructures.h)
    auto hasPerObjData =
        std::ranges::any_of(mergedReflection.descriptorBindings,
                            [](const ShaderReflection::DescriptorBinding& b) { return b.set == 1 && b.binding == 0; });
    REQUIRE(hasPerObjData);

    // MATERIAL_SSBO (set=2, binding=0) should be present (from material.h, frag only)
    auto hasMaterialSSBO =
        std::ranges::any_of(mergedReflection.descriptorBindings,
                            [](const ShaderReflection::DescriptorBinding& b) { return b.set == 2 && b.binding == 0; });
    REQUIRE(hasMaterialSSBO);

    // Output variables from vert shader should match input variables in frag shader
    for (const auto& outVar : reflections[0].outputVariables)
    {
        if (outVar.isBuiltIn)
        {
            // Skip built-in variables as they don't have matching locations
            continue;
        }
        auto match = std::ranges::find_if(reflections[1].inputVariables, [&](const ShaderReflection::IOVariable& inVar)
                                          { return inVar.location == outVar.location; });
        REQUIRE(match != reflections[1].inputVariables.end());
    }
}
}  // namespace Muyo
