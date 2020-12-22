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

const std::vector<const SceneNode *> &Scene::FlattenTree()
{
    if (m_bIsTreeDirty)
    {
        m_vpFlattenedNodes.clear();
        std::function<void(const std::unique_ptr<SceneNode> &, const glm::mat4 &, std::vector<const SceneNode*>&)>
            FlattenTreeRecursive = [&](const std::unique_ptr<SceneNode> &pNode,
                                       const glm::mat4 &mCurrentTrans, std::vector<const SceneNode*>& vFlattenedTree) {
                glm::mat4 mWorldMatrix = mCurrentTrans * pNode->GetMatrix();
                assert(IsMat4Valid(mWorldMatrix));
                if (GeometrySceneNode *pGeometryNode = dynamic_cast<GeometrySceneNode *>(pNode.get()))
                {
                    vFlattenedTree.push_back(pNode.get());
                    Geometry *pGeometry = pGeometryNode->GetGeometry();
                    pGeometry->SetWorldMatrix(mWorldMatrix);
                }
                for (const auto &pChild : pNode->GetChildren())
                {
                    FlattenTreeRecursive(pChild, mWorldMatrix, vFlattenedTree);
                }
            };
        FlattenTreeRecursive(m_pRoot, m_pRoot->GetMatrix(), m_vpFlattenedNodes);
        m_bIsTreeDirty = false;
    }
    return m_vpFlattenedNodes;
}
