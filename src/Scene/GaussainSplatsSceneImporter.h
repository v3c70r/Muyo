#pragma once
#include <vector>

#include "SceneImporter.h"

namespace Muyo
{
class SPZImporter : public ISceneImporter
{
public:
    std::vector<Scene> ImportScene(const std::string& sSceneFile) override;
};
}  // namespace Muyo

