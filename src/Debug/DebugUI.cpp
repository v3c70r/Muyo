#include "DebugUI.h"
#include <unordered_map>

#include "DescriptorManager.h"
#include "LightSceneNode.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderPassManager.h"
#include "RenderResourceManager.h"
#include "SceneManager.h"
#include "imgui.h"
#include "imgui_internal.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <ImGuizmo.h>
#include <imnodes.h>

#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Muyo
{

void ResourceManagerDebugPage::Render() const
{
    ImGui::Begin(m_sName.c_str());
    {
        const auto& resourceMap = GetRenderResourceManager()->GetResourceMap();
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
        ImGuizmo::BeginFrame();
        ImGuizmo::SetRect(0.0f, 0.0f,
                          static_cast<float>(GetRenderPassManager()->GetViewportSize().width),
                          static_cast<float>(GetRenderPassManager()->GetViewportSize().height));

        glm::mat4 mProj = GetRenderPassManager()->GetCamera()->GetProjMat();
        glm::mat4 mView = GetRenderPassManager()->GetCamera()->GetViewMat();
        mProj[1][1] *= -1.0f;

        const auto& sceneMap = GetSceneManager()->GetAllScenes();
        for (const auto& scenePair : sceneMap)
        {
            const auto& pRoot = scenePair.second.GetRoot();
            std::function<void(const SceneNode*, const glm::mat4&)>
                DisplayNodesRecursive = [&](const SceneNode* pSceneNode, const glm::mat4& mCurrentTrans)
            {
                glm::mat4 mWorld = mCurrentTrans * pSceneNode->GetMatrix();
                static ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

                bool bIsTreeOpened = ImGui::TreeNodeEx(pSceneNode->GetName().c_str(), base_flags);
                bool bIsTreeNodeSelected = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
                if (bIsTreeNodeSelected)
                {
                    m_pSelectedNode = pSceneNode;
                }
                if (m_pSelectedNode == pSceneNode)
                {
                    DrawGizmoOnSceneNode(pSceneNode, mWorld, mView, mProj);
                }

                if (bIsTreeOpened)
                {
                    // Display info for leaves
                    if (pSceneNode->GetChildren().size() == 0)
                    {
                        DisplaySceneNodeInfo(*pSceneNode);
                    }
                    for (const auto& child : pSceneNode->GetChildren())
                    {
                        DisplayNodesRecursive(child.get(), mWorld);
                    }
                    ImGui::TreePop();
                }
            };
            DisplayNodesRecursive(pRoot.get(), pRoot->GetMatrix());
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

void SceneDebugPage::DrawGizmoOnSceneNode(const SceneNode* pSceneNode, glm::mat4& mWorld, const glm::mat4& mView, const glm::mat4& mProj) const
{
    if (const GeometrySceneNode* pGeometryNode = dynamic_cast<const GeometrySceneNode*>(pSceneNode))
    {
        // construct bound
        auto AABB = pSceneNode->GetAABB();
        float bounds[6] =
            {
                AABB.vMin.x,
                AABB.vMin.y,
                AABB.vMin.z,
                AABB.vMax.x,
                AABB.vMax.y,
                AABB.vMax.z,
            };

        ImGuizmo::Manipulate(glm::value_ptr(mView), glm::value_ptr(mProj), ImGuizmo::OPERATION::UNIVERSAL, ImGuizmo::MODE::WORLD, glm::value_ptr(mWorld), NULL, NULL, bounds, NULL);
    }
    else if (const LightSceneNode* pLightNode = dynamic_cast<const LightSceneNode*>(pSceneNode))
    {
        // ImGuizmo::DrawCubes(glm::value_ptr(mView), glm::value_ptr(mProj), glm::value_ptr(mWorld), 1);
        ImGuizmo::Manipulate(glm::value_ptr(mView), glm::value_ptr(mProj), ImGuizmo::OPERATION::UNIVERSAL, ImGuizmo::MODE::WORLD, glm::value_ptr(mWorld), NULL, NULL, NULL, NULL);
    }
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));  // No padding

    bool bOpen = true;
    ImGui::Begin(m_sName.c_str(), &bOpen, WINDOW_FLAG);
    ImGui::PopStyleVar(3);

    // Create a DockSpace node where any window can be docked
    ImGuiID dockSpaceId = ImGui::GetID(m_sName.c_str());
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void EnvironmentMapDebugPage::Render() const
{
    ImGui::Begin(m_sName.c_str());
    {
        // Draw HDR selector
        struct Funcs
        {
            static bool ItemGetter(void* data, int n, const char** out_str)
            {
                *out_str = ((const std::string*)data)[n].c_str();
                return true;
            }
        };

        int nPrevSelection = m_nCurrentHDRIndex;

        ImGui::Combo("Current HDR", &m_nCurrentHDRIndex, &Funcs::ItemGetter, (void*)m_vHDRImagePatheStrings.data(), static_cast<int>(m_vHDRImagePathes.size()));

        if (nPrevSelection != m_nCurrentHDRIndex)
        {
            GetRenderPassManager()->ReloadEnvironmentMap(m_vHDRImagePathes[m_nCurrentHDRIndex].string());
        }

        // Draw environmap texture
        {
            ImGuiIO& io = ImGui::GetIO();
            ImTextureID my_tex_id = (void*)GetDescriptorManager()->GetImGuiTextureId("EnvMap");
            float my_tex_w = 512.0f;
            float my_tex_h = 256.0f;
            {
                ImGui::Text("%.0fx%.0f", my_tex_w, my_tex_h);
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 uv_min = ImVec2(0.0f, 0.0f);                  // Top-left
                ImVec2 uv_max = ImVec2(1.0f, 1.0f);                  // Lower-right
                ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);    // No tint
                ImVec4 border_col = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);  // 50% opaque white
                ImGui::Image(my_tex_id, ImVec2(my_tex_w, my_tex_h), uv_min, uv_max, tint_col, border_col);
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    float region_sz = 32.0f;
                    float region_x = io.MousePos.x - pos.x - region_sz * 0.5f;
                    float region_y = io.MousePos.y - pos.y - region_sz * 0.5f;
                    float zoom = 4.0f;
                    if (region_x < 0.0f)
                    {
                        region_x = 0.0f;
                    }
                    else if (region_x > my_tex_w - region_sz)
                    {
                        region_x = my_tex_w - region_sz;
                    }
                    if (region_y < 0.0f)
                    {
                        region_y = 0.0f;
                    }
                    else if (region_y > my_tex_h - region_sz)
                    {
                        region_y = my_tex_h - region_sz;
                    }
                    ImGui::Text("Min: (%.2f, %.2f)", region_x, region_y);
                    ImGui::Text("Max: (%.2f, %.2f)", region_x + region_sz, region_y + region_sz);
                    ImVec2 uv0 = ImVec2((region_x) / my_tex_w, (region_y) / my_tex_h);
                    ImVec2 uv1 = ImVec2((region_x + region_sz) / my_tex_w, (region_y + region_sz) / my_tex_h);
                    ImGui::Image(my_tex_id, ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
                    ImGui::EndTooltip();
                }
            }
        }
    }
    ImGui::End();
}

LightsDebugPage::LightsDebugPage(const std::string& sName)
    : IDebugUIPage(sName)
{
    DrawLists dl = GetSceneManager()->GatherDrawLists();
    const std::vector<const SceneNode*> lightNodes = dl.m_aDrawLists[DrawLists::DL_LIGHT];
    for (const SceneNode* lightNode : lightNodes)
    {
        m_vpLightNodes.push_back(static_cast<const LightSceneNode*>(lightNode));
    }
}

void LightsDebugPage::Render() const
{
    ImGui::Begin("Lights");
    for (const LightSceneNode* pLightNode : m_vpLightNodes)
    {
        switch (pLightNode->GetLightType())
        {
            case LIGHT_TYPE_POINT:
                ImGui::Text("%s\tPoint Light", pLightNode->GetName().c_str());
                break;
            case LIGHT_TYPE_SPOT:
                ImGui::Text("%s\tSpot Light", pLightNode->GetName().c_str());
                break;
            case LIGHT_TYPE_DIRECTIONAL:
                ImGui::Text("%s\tDirectional Light", pLightNode->GetName().c_str());
                break;
        }
        pLightNode->GetColor();
        ImGui::ColorEdit3("MyColor##1", (float*)glm::value_ptr(pLightNode->GetColor()));
    }
    ImGui::End();
}

void CameraDebugPage::Render() const
{
    // m_pCamera could be unavailable before render pass manager initialization
    if (m_pCamera)
    {
        ImGui::GetWindowViewport();
        ImGui::SetNextWindowPos(ImVec2(-10.0f, static_cast<float>(GetRenderPassManager()->GetViewportSize().height) - 30.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(GetRenderPassManager()->GetViewportSize().width), 30.0f));
        float fRatio = m_pCamera->GetLeftSplitScreenRatio();
        static ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("Camera", nullptr, flags);
        ImGui::PushItemWidth(static_cast<float>(GetRenderPassManager()->GetViewportSize().width));
        // Set ImGui slider to transparent
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1, 0, 0, 0.5));
        ImGui::SliderFloat("##", &fRatio, 0.0f, 1.0f, "");
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        m_pCamera->SetLeftSplitScreenRatio(fRatio);
        ImGui::End();
    }
}

RenderPassDebugPage::RenderPassDebugPage(const std::string& sName) : IDebugUIPage(sName)
{
    ImNodes::CreateContext();
}
RenderPassDebugPage::~RenderPassDebugPage()
{
    ImNodes::DestroyContext();
}
void RenderPassDebugPage::Render() const
{
    static ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("RenderPasses", nullptr, flags);
    if (m_pRenderPassManager)
    {
        for (const auto* pRenderPass : m_pRenderPassManager->GetRenderPasses())
        {
            if (pRenderPass)
            {
                ImGui::Text("%s", pRenderPass->GetName().c_str());
                const auto& resourceMap = GetRenderResourceManager()->GetResourceMap();
                for (const auto* inputResource : pRenderPass->GetInputResources())
                {
                    // find key in resource map
                    for (const auto& resMap : resourceMap)
                    {
                        if (resMap.second.get() == inputResource)
                        {
                            ImGui::Text("\tInput: %s", resMap.first.c_str());
                        }
                    }
                }
                for (const auto* outputResource : pRenderPass->GetOutputResources())
                {
                    // find key in resource map
                    for (const auto& resMap : resourceMap)
                    {
                        if (resMap.second.get() == outputResource)
                        {
                            ImGui::Text("\tOutput: %s", resMap.first.c_str());
                        }
                    }
                }
            }
        }
    }
    ImGui::End();


    std::vector<const RenderGraphNode*> rgNodes = m_pRDG->TopologicalSort();


    ImGui::Begin("Render Passes");
    ImNodes::BeginNodeEditor();
    // Construct nodes
    int nodeId = 0;
    // output pin has node id + 1 
    // input pin has node id
    std::unordered_map<const RenderGraphNode*, int> rgnToNodeId;
    for (const auto* rgNode : rgNodes)
    {

        rgnToNodeId[rgNode] = nodeId;
        ImNodes::BeginNode(nodeId);
        ImGui::Text("%s", rgNode->m_pRenderPass->GetName().c_str());

        ImNodes::BeginInputAttribute(nodeId);
        ImGui::Text("InputPass");
        ImNodes::EndInputAttribute();

        ImNodes::BeginOutputAttribute(nodeId+1);
        ImGui::Text("OutputPass");
        ImNodes::EndOutputAttribute();
        ImNodes::EndNode();
        nodeId += 2;
    }

    // construct links
    for (const auto* rgNode : rgNodes)
    {
        for (const auto* neighbor : rgNodes)
        {
            if (rgNode != neighbor && m_pRDG->IsAdjacentTo(rgNode, neighbor))
            {
                ImNodes::Link(rgnToNodeId[rgNode], rgnToNodeId[rgNode]+1,rgnToNodeId[neighbor]);
            }
        }
    }


    ImNodes::EndNodeEditor();
    ImGui::End();


    //ImGui::Begin("node editor");
    //const int hardcoded_node_id = 1;

    //ImNodes::BeginNodeEditor();
    //ImNodes::BeginNode(hardcoded_node_id);

    //const int output_attr_id = 2;
    //ImNodes::BeginOutputAttribute(output_attr_id);
    //// in between Begin|EndAttribute calls, you can call ImGui
    //// UI functions
    //ImGui::Text("output pin");
    //ImNodes::EndOutputAttribute();

    //ImNodes::EndNode();
    //ImNodes::EndNodeEditor();

    //ImGui::End();
}

#undef GLM_ENABLE_EXPERIMENTAL

}  // namespace Muyo
