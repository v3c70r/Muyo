#include "../SceneImporter.h"
#include <iostream>

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