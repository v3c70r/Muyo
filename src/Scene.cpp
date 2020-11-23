#include "Scene.h"
#include <imgui.h>
#include <functional>

void SceneNode::AppendChild(SceneNode* node)
{
    m_vpChildren.emplace_back(node);
}

std::string Scene::ConstructDebugString() const
{
    std::stringstream ss;
    for (const auto &node : GetRoot()->GetChildren())
    {
        std::function<void(const SceneNode*, int)> ConstructStringRecursive = [&](const SceneNode *node, int layer) {
            std::string filler(layer, '-');
            ss << filler;
            ss << node->GetName() << std::endl;
            for (const auto &child : node->GetChildren())
            {
                ConstructStringRecursive(child.get(), layer + 1);
            }
        };
        ConstructStringRecursive(node.get(), 0);
    }
    return ss.str();
}

std::vector<const SceneNode*> Scene::FlattenTree() const
{
    std::vector<const SceneNode*> vNodes;
    std::function<void(const std::unique_ptr<SceneNode>&)>
        FlattenTreeRecursive = [&](const std::unique_ptr<SceneNode>& pNode) {
            if (dynamic_cast<GeometrySceneNode*>(pNode.get()))
            {
                vNodes.push_back(pNode.get());
            }
            for (const auto& pChild : pNode->GetChildren())
            {
                FlattenTreeRecursive(pChild);
            }
        };
    FlattenTreeRecursive(GetRoot());
    return vNodes;
}
