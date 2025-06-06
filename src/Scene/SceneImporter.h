#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "Scene.h"

namespace tinygltf
{
class Node;
struct Mesh;
class Model;
}  // namespace tinygltf

namespace Muyo
{
class Geometry;
class GeometryManager;
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
    // Import GLTF
    // Area lights are constructed from emissive geometries, hinted by their name
    // Rectangle light starts with LIGHT_RECT
    // Sphere light starts with LIGHT_SPHERE
    // tube light starts with LIGHT_TUBE
    virtual std::vector<Scene> ImportScene(const std::string& sSceneFile) override;

private:
    void CopyGLTFNode(SceneNode& sceneNode, const tinygltf::Node& gltfNode);
    void CopyGLTFNodeIterative(SceneNode&, const tinygltf::Node& gltfNode,
                               const std::vector<tinygltf::Node>& vNodes);
    void ConstructGeometryNode(GeometrySceneNode& geomNode, const tinygltf::Mesh& mesh, const tinygltf::Model& model);

private:
    std::filesystem::path m_sceneFile;
};

}  // namespace Muyo
