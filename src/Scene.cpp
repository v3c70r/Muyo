#include "Scene.h"
#include "Geometry.h"
#include <imgui.h>
#include <functional>

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

std::vector<const SceneNode *> Scene::FlattenTree() const
{
    std::vector<const SceneNode *> vNodes;
    std::function<void(const std::unique_ptr<SceneNode> &, const glm::mat4 &)>
        FlattenTreeRecursive = [&](const std::unique_ptr<SceneNode> &pNode,
                                   const glm::mat4 &mCurrentTrans) {
            glm::mat4 mWorldMatrix = mCurrentTrans * pNode->GetMatrix();
            assert(IsMat4Valid(mWorldMatrix));
            if (GeometrySceneNode *pGeometryNode = dynamic_cast<GeometrySceneNode *>(pNode.get()))
            {
                vNodes.push_back(pNode.get());
                Geometry *pGeometry = pGeometryNode->GetGeometry();
                pGeometry->SetWorldMatrix(mWorldMatrix);
            }
            for (const auto &pChild : pNode->GetChildren())
            {
                FlattenTreeRecursive(pChild, mWorldMatrix);
            }
        };
    FlattenTreeRecursive(GetRoot(), GetRoot()->GetMatrix());
    return vNodes;
}
