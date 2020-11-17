#include "Scene.h"
#include <imgui.h>

void SceneNode::AppendChild(SceneNode* node)
{
    m_vpChildren.emplace_back(node);
}

void Scene::DrawSceneDebug() const
{
    for (const auto& node : GetRoot()->GetChildren())
    {
        ImGui::Text("Scene Roots: %s", node->GetName().c_str());
        DrawSceneDebugRecursive(node.get(), 0);
    }
}
void Scene::DrawSceneDebugRecursive(const SceneNode* node, int layer) const 
{
    std::string spacing("-", layer);
    ImGui::Text("%s Node: %s", spacing.c_str(), node->GetName().c_str());
    for (const auto& pChild : node->GetChildren())
    {
        DrawSceneDebugRecursive(pChild.get(), layer+1);
    }
}
