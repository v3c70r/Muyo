#include "SceneImporter.h"
#include <iostream>

namespace Muyo {


int main()
{

    GLTFImporter importer;
    std::vector<Scene> vScenes = importer.ImportScene("assets/mazda_mx-5/scene.gltf");
    for (const auto &scene : vScenes)
    {
        std::cout << scene.ConstructDebugString() << std::endl;
    }
    return 0;
}
} // namespace Muyo
