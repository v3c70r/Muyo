#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Scene.h"

namespace tinygltf
{
class Node;
}
class Geometry;
class SceneNode;
class Scene;

class ISceneImporter
{
public:
    virtual std::vector<Scene> ImportScene(const std::string& sSceneFile) = 0;
};

class GLTFImporter : public ISceneImporter
{
public:
    virtual std::vector<Scene> ImportScene(const std::string& sSceneFile) override;

private:
    void CopyGLTFNode(SceneNode& sceneNode, const tinygltf::Node& gltfNode);
    void CopyGLTFNodeIterative(SceneNode&, const tinygltf::Node& gltfNode,
                               const std::vector<tinygltf::Node>& vNodes);
};

