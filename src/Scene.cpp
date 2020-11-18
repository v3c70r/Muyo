#include "Scene.h"
#include <imgui.h>

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