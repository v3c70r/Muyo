#include "SceneImporter.h"

#include <tiny_gltf.h>

#include "SceneImporter.h"

std::vector<Scene> GLTFImporter::ImportScene(const std::string& sSceneFile)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    bool ret =
        loader.LoadASCIIFromFile(&model, &err, &warn, sSceneFile.c_str());

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

        // tinygltf::Node root = model.nodes[model.scenes[i].nodes[0]];
        // root.children;

        // auto insertNode = [](SceneNode* pSceneRoot, tinygltf::Node
        // pGLTFNode)->void
        //{
        //    for (int child: pGLTFNode.children)
        //    {
        //    }
        //};
    }
    return res;
}

void GLTFImporter::CopyGLTFNode(SceneNode& sceneNode,
                                const tinygltf::Node& gltfNode)
{
    sceneNode.SetName(gltfNode.name);
    // TODO: construct mesh, transformations, etc
}

void GLTFImporter::CopyGLTFNodeIterative(SceneNode& sceneNode, const tinygltf::Node& gltfNode,
                           const std::vector<tinygltf::Node>& vNodes)
{
    CopyGLTFNode(sceneNode, gltfNode);
    for (int nNodeIdx : gltfNode.children)
    {
        const tinygltf::Node& node = vNodes[nNodeIdx];
        if (node.mesh != -1)
        {
            SceneNode* pSceneNode = new GeometrySceneNode;
            CopyGLTFNodeIterative(*pSceneNode, node, vNodes);
            sceneNode.AppendChild(pSceneNode);
        }
    }
}

