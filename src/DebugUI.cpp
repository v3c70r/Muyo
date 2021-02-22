#include "DebugUI.h"
#include "RenderResourceManager.h"
#include "SceneManager.h"

#include <imgui.h>
#include <functional>

void ResourceManagerDebugPage::Render() const
{
    ImGui::Begin(m_sName.c_str());
    {
        const auto& resourceMap = GetRenderResourceManager()->getResourceMap();
        for (const auto& resMap : resourceMap)
        {
            ImGui::Text("%s", resMap.first.c_str());
        }
    }
    ImGui::End();
}

void SceneDebugPage::Render() const 
{
    ImGui::Begin(m_sName.c_str());
    {
        const auto& sceneMap = GetSceneManager()->GetAllScenes();
        for (const auto& scenePair : sceneMap)
        {
            const auto& pRoot = scenePair.second.GetRoot();
            std::function<void(const SceneNode*)>
                DisplayNodesRecursive = [&](const SceneNode* pSceneNode) {
                    if (ImGui::TreeNode(pSceneNode->GetName().c_str()))
                    {
                        for (const auto& child : pSceneNode->GetChildren())
                        {
                            DisplayNodesRecursive(child.get());
                        }
                        ImGui::TreePop();
                    }
                };
            DisplayNodesRecursive(pRoot.get());
        }
    }
    ImGui::End();
}

