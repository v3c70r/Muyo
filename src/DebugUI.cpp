#include "DebugUI.h"
#include "RenderResourceManager.h"
#include "SceneManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
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
                        // Display info for leaves
                        if (pSceneNode->GetChildren().size() == 0)
                        {
                            DisplaySceneNodeInfo(*pSceneNode);
                        }
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

void SceneDebugPage::DisplaySceneNodeInfo(const SceneNode& sceneNode) const
{
    glm::vec3 vScale;
    glm::quat qRotation;
    glm::vec3 vTranslation;
    glm::vec3 vSkew;
    glm::vec4 vPerspective;
    glm::decompose(sceneNode.GetMatrix(), vScale, qRotation, vTranslation, vSkew, vPerspective);
    ImGui::InputFloat3("Scale", glm::value_ptr(vScale), "%.3f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Translation", glm::value_ptr(vTranslation), "%.3f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat4("Rotation Quat", glm::value_ptr(qRotation), "%.3f", ImGuiInputTextFlags_ReadOnly);
}

void VerticalTabsPage::Render() const
{
    // Render left tabs
}


#undef GLM_ENABLE_EXPERIMENTAL

