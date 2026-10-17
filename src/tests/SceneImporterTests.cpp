#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

#include "SceneImporter.h"

namespace Muyo
{

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

}  // namespace Muyo
