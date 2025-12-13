#include <catch2/catch_test_macros.hpp>

#include "RenderGraph/ShaderReflectionFetcher.h"
#include "catch2/catch_message.hpp"
#include <filesystem>
#include <fstream>
namespace Muyo::RenderGraph
{

inline std::vector<char> ReadSpv(const std::filesystem::path& spvPath)
{
    std::ifstream file(spvPath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open file");
    return std::vector<char>(std::istreambuf_iterator<char>(file), {});
}

TEST_CASE("ShaderReflectionFetcher: SPIR-V Reflection from file", "[ShaderReflectionFetcher]")
{
    namespace fs = std::filesystem;
    // Path to the compiled SPIR-V file
    fs::path spirvPath = fs::path("shaders") / "copyBuffer.comp.slang.spv";
    auto spirv_bytes = ReadSpv(spirvPath);

    REQUIRE(spirv_bytes.size() % 4 == 0); // SPIR-V must be uint32-aligned
    const uint32_t* spirv_code = reinterpret_cast<const uint32_t*>(spirv_bytes.data());
    size_t spirv_nbytes = spirv_bytes.size();

    Muyo::RenderGraph::ShaderReflectionFetcher fetcher(spirv_code, spirv_nbytes);

    const auto& descriptorSets = fetcher.GetDescriptorSets();
    const auto& pushConstantRanges = fetcher.GetPushConstantRanges();

    // Validate descriptor sets and bindings
    // According to copyBuffer.comp.slang:
    // - set 0, binding 0: ByteAddressBuffer (should be STORAGE_BUFFER)
    // - set 1, binding 0: RWByteAddressBuffer (should be STORAGE_BUFFER or STORAGE_BUFFER/UNIFORM_BUFFER depending on reflection)
    // - push constant block: 4 bytes

    // There should be at least 2 sets (set 0 and set 1)
    REQUIRE(descriptorSets.size() >= 2);

    // Set 0, binding 0
    REQUIRE(descriptorSets[0].size() >= 1);
    CHECK(descriptorSets[0][0].binding == 0);
    CHECK(descriptorSets[0][0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    // Set 1, binding 0
    REQUIRE(descriptorSets[1].size() >= 1);
    CHECK(descriptorSets[1][0].binding == 0);
    CHECK(descriptorSets[1][0].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    // Push constant block
    REQUIRE(pushConstantRanges.size() == 1);
    CHECK(pushConstantRanges[0].size == 4);
}
} // namespace Muyo::RenderGraph
