#include "Scene.h"

#include <imgui.h>

#include <functional>

#include "Geometry.h"

void SceneNode::AppendChild(SceneNode *node)
{
    m_vpChildren.emplace_back(node);
}

std::string Scene::ConstructDebugString() const
{
    std::stringstream ss;
    for (const auto &node : GetRoot()->GetChildren())
    {
        std::function<void(const SceneNode *, int)> ConstructStringRecursive = [&](const SceneNode *node, int layer) {
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

const DrawLists &Scene::GatherDrawLists()
{
    if (m_bAreDrawListsDirty)
    {
        for (auto& dl : m_drawLists.m_aDrawLists)
        {
            dl.clear();
        }
        std::function<void(const std::unique_ptr<SceneNode> &, const glm::mat4 &, DrawLists&)>
            FlattenTreeRecursive = [&](const std::unique_ptr<SceneNode> &pNode,
                                       const glm::mat4 &mCurrentTrans, DrawLists& drawLists) {
                glm::mat4 mWorldMatrix = mCurrentTrans * pNode->GetMatrix();
                assert(IsMat4Valid(mWorldMatrix));
                if (GeometrySceneNode *pGeometryNode = dynamic_cast<GeometrySceneNode *>(pNode.get()))
                {
                    if (pGeometryNode->IsTransparent())
                    {
                        drawLists.m_aDrawLists[DrawLists::DL_TRANSPARENT].push_back(pNode.get());
                    }
                    else
                    {
                        drawLists.m_aDrawLists[DrawLists::DL_OPAUE].push_back(pNode.get());
                    }
                    Geometry *pGeometry = pGeometryNode->GetGeometry();
                    pGeometry->SetWorldMatrix(mWorldMatrix);
                }
                for (const auto &pChild : pNode->GetChildren())
                {
                    FlattenTreeRecursive(pChild, mWorldMatrix, drawLists);
                }
            };
        FlattenTreeRecursive(m_pRoot, m_pRoot->GetMatrix(), m_drawLists);
        m_bAreDrawListsDirty = false;
    }
    return m_drawLists;
}
