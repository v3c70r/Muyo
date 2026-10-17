#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "SceneImporter.h"

namespace Muyo
{
namespace
{
std::filesystem::path WriteTempGltf(const std::string& name, const std::string& json)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out << json;
    return path;
}

/// @return The importer's message, or empty if it did not throw. The malformed fixtures below fail
/// during parsing, before any mesh, material or Vulkan resource is created, so no device is needed.
std::string ImportError(const std::filesystem::path& path)
{
    GLTFImporter importer;
    try
    {
        importer.ImportScene(path.string());
    }
    catch (const std::runtime_error& e)
    {
        return e.what();
    }
    return {};
}
}  // namespace

// The fixtures load `assets/...` relative to the working directory. When that path is missing the
// importer used to return an empty scene list, which surfaced much later as `SetData(nullptr)` inside
// the per-object upload - a misleading abort that was mistaken for a render bug twice. It must fail
// here, naming the file, and this test needs no device because the check precedes any Vulkan call.
TEST_CASE("GLTFImporter: a missing scene file throws, naming the path", "[SceneImporter]")
{
    GLTFImporter importer;
    CHECK_THROWS_AS(importer.ImportScene("assets/__missing_scene__.gltf"), std::runtime_error);

    std::string message;
    try
    {
        importer.ImportScene("assets/__missing_scene__.gltf");
    }
    catch (const std::runtime_error& e)
    {
        message = e.what();
    }
    CHECK(message.find("__missing_scene__.gltf") != std::string::npos);
    CHECK(message.find("does not exist") != std::string::npos);
}

// The POSITION layout checks used to be asserts, compiled out under NDEBUG. Each malformed asset here
// would then have been read into the wrong vertex layout silently, so these are the teeth for the
// conversion: without it, the first fails to throw in Release and the second memcpys past the buffer.
TEST_CASE("GLTFImporter: a non-VEC3 POSITION accessor throws, naming the attribute", "[SceneImporter]")
{
    const auto path = WriteTempGltf("muyo_vec2_position.gltf", R"({
      "asset": {"version": "2.0"},
      "scene": 0,
      "scenes": [{"nodes": [0]}],
      "nodes": [{"mesh": 0}],
      "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
      "accessors": [{"bufferView": 0, "componentType": 5126, "count": 1, "type": "VEC2"}],
      "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 8}],
      "buffers": [{"byteLength": 8, "uri": "data:application/octet-stream;base64,AAAAAAAAAAA="}]
    })");
    const std::string message = ImportError(path);
    std::filesystem::remove(path);
    INFO(message);
    CHECK(message.find("POSITION accessor type is not VEC3") != std::string::npos);
}

TEST_CASE("GLTFImporter: a POSITION buffer shorter than the accessor range throws", "[SceneImporter]")
{
    const auto path = WriteTempGltf("muyo_short_position.gltf", R"({
      "asset": {"version": "2.0"},
      "scene": 0,
      "scenes": [{"nodes": [0]}],
      "nodes": [{"mesh": 0}],
      "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
      "accessors": [{"bufferView": 0, "componentType": 5126, "count": 2, "type": "VEC3",
                     "min": [0, 0, 0], "max": [0, 0, 0]}],
      "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 12}],
      "buffers": [{"byteLength": 12, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAA"}]
    })");
    const std::string message = ImportError(path);
    std::filesystem::remove(path);
    INFO(message);
    CHECK(message.find("POSITION buffer is shorter than the accessor range") != std::string::npos);
}

}  // namespace Muyo
