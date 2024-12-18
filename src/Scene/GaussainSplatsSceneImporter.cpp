#include "GaussainSplatsSceneImporter.h"
#include <load-spz.h>
#include "GaussianSplattingSceneNode.h"
namespace Muyo
{
std::vector<Scene> SPZImporter::ImportScene(const std::string& sSceneFile)
{
    std::vector<Scene> spzScene;
    spz::GaussianCloud cloud = spz::loadSpz(sSceneFile);
    if (cloud.numPoints > 0)
    {
        auto* pSpzSceneNode  = new GaussianSplatsSceneNode;
        pSpzSceneNode->SetData(cloud);

        spzScene.resize(1);
        Scene& scene = spzScene.back();
        scene.SetName(sSceneFile);
        scene.GetRoot()->AppendChild(pSpzSceneNode);
    }
    return spzScene;
}
}

