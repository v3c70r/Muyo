#include "SceneImporter.h"

#include <tiny_gltf.h>
#include <cassert>

#include "SceneImporter.h"
#include <glm/gtc/quaternion.hpp>

std::vector<Scene> GLTFImporter::ImportScene(const std::string& sSceneFile)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    bool ret =
        loader.LoadASCIIFromFile(&model, &err, &warn, sSceneFile.c_str());
    assert(ret);

    std::vector<Scene> res;
    res.resize(model.scenes.size());
    for (size_t i = 0; i < model.scenes.size(); i++)
    {
        tinygltf::Scene& tinyScene = model.scenes[i];
        SceneNode* pSceneRoot = res[i].GetRoot();
        // For each root node in scene
        for (int nNodeIdx : tinyScene.nodes)
        {
            tinygltf::Node node = model.nodes[nNodeIdx];
            SceneNode* pSceneNode = new SceneNode(node.name);
            CopyGLTFNodeIterative(*pSceneNode, node, model.nodes);
            pSceneRoot->AppendChild(pSceneNode);
        }
    }
    return res;
}

void GLTFImporter::CopyGLTFNode(SceneNode& sceneNode,
                                const tinygltf::Node& gltfNode)
{
    sceneNode.SetName(gltfNode.name);

    {
        glm::vec3 vT(0.0f);
        vT.x = gltfNode.translation[1];
        vT.y = gltfNode.translation[2];
        vT.z = gltfNode.translation[3];

        glm::quat qR;
        qR.x = gltfNode.rotation[0];
        qR.y = gltfNode.rotation[1];
        qR.z = gltfNode.rotation[2];
        qR.w = gltfNode.rotation[3];

        glm::vec3 vS(0.0f);
        vS.x = gltfNode.scale[0];
        vS.x = gltfNode.scale[1];
        vS.x = gltfNode.scale[2];

        glm::mat4 mMat;
        glm::translate(mMat, vT);
        mMat = mMat * glm::mat4_cast(qR);
        glm::scale(mMat, vS);
        sceneNode.SetMatrix(mMat);
    }
}

void GLTFImporter::CopyGLTFNodeIterative(SceneNode& sceneNode, const tinygltf::Node& gltfNode,
                           const std::vector<tinygltf::Node>& vNodes)
{
    CopyGLTFNode(sceneNode, gltfNode);
    for (int nNodeIdx : gltfNode.children)
    {
        const tinygltf::Node& node = vNodes[nNodeIdx];
        SceneNode* pSceneNode = nullptr;
        if (node.mesh != -1)
        {
            pSceneNode = new GeometrySceneNode;
        }
        else
        {
            pSceneNode = new SceneNode;
        }

        CopyGLTFNodeIterative(*pSceneNode, node, vNodes);
        sceneNode.AppendChild(pSceneNode);
    }
}

