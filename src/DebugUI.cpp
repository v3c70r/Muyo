#include "DebugUI.h"
#include "RenderResourceManager.h"
#include "SceneManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
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
    glm::vec3 AABBMin = sceneNode.GetAABB().vMin;
    glm::vec3 AABBMax = sceneNode.GetAABB().vMax;
    ImGui::InputFloat3("Scale", glm::value_ptr(vScale), "%.3f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Translation", glm::value_ptr(vTranslation), "%.3f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat4("Rotation Quat", glm::value_ptr(qRotation), "%.3f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("AABB Min", glm::value_ptr(AABBMin), "%.3f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("AABB Max", glm::value_ptr(AABBMax), "%.3f", ImGuiInputTextFlags_ReadOnly);
}

void DemoDebugPage::Render() const
{
    ImGui::ShowDemoWindow();
}

void VerticalTabsPage::Render() const
{
    // Render left tabs
}

void DockSpace::Render() const
{
    // TODO: move this part outside
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));   // No padding

    bool bOpen = true;
    ImGui::Begin(m_sName.c_str(), &bOpen, WINDOW_FLAG);
    ImGui::PopStyleVar(3);

    // Create a DockSpace node where any window can be docked
    ImGuiID dockSpaceId = ImGui::GetID(m_sName.c_str());
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();

}


#undef GLM_ENABLE_EXPERIMENTAL

