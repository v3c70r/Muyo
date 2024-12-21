#include "Scene.h"

#include <imgui.h>

#include <functional>

#include "GaussianSplattingSceneNode.h"
#include "Geometry.h"
#include "LightSceneNode.h"
#include "RenderResourceManager.h"
#include "PerObjResourceManager.h"

namespace Muyo
{

void SceneNode::AppendChild(SceneNode *node)
{
    m_vpChildren.emplace_back(node);
}

std::string Scene::ConstructDebugString() const
{
    std::stringstream ss;
    for (const auto &node : GetRoot()->GetChildren())
    {
        std::function<void(const SceneNode *, int)> ConstructStringRecursive = [&](const SceneNode *node, int layer)
        {
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
        for (auto &dl : m_drawLists.m_aDrawLists)
        {
            dl.clear();
        }
        uint32_t nShadowMapIndex = 0;
        std::function<void(const std::unique_ptr<SceneNode> &, const glm::mat4 &, DrawLists &)>
            FlattenTreeRecursive = [&](const std::unique_ptr<SceneNode> &pNode,
                                       const glm::mat4 &mCurrentTrans, DrawLists &drawLists)
        {
            glm::mat4 mWorldMatrix = mCurrentTrans * pNode->GetMatrix();
            assert(IsMat4Valid(mWorldMatrix));
            uint32_t nSubmeshCount = 0;
            std::array<PerSubmeshData, MAX_NUM_SUBMESHES> aSubmeshDatas;
            if (GeometrySceneNode *pGeometryNode = dynamic_cast<GeometrySceneNode *>(pNode.get()))
            {
                if (pGeometryNode->IsTransparent())
                {
                    drawLists.m_aDrawLists[DrawLists::DL_TRANSPARENT].push_back(pNode.get());
                }
                else
                {
                    drawLists.m_aDrawLists[DrawLists::DL_OPAQUE].push_back(pNode.get());
                }
                Geometry *pGeometry = pGeometryNode->GetGeometry();
                pGeometry->SetWorldMatrix(mWorldMatrix);

                for (auto& submesh : pGeometry->getSubmeshes())
                {
                    // TODO: populate submesh data array

                    aSubmeshDatas[nSubmeshCount++].nMaterialIndex = submesh->GetMeshIndex();
                    //submesh->GetMaterial();
                }
                
            }
            // Gather light sources
            else if (LightSceneNode *pLightSceneNode = dynamic_cast<LightSceneNode *>(pNode.get()))
            {
                // Hack: Set shadow map index on the spot lights
                if (pLightSceneNode->GetLightType() == LIGHT_TYPE_SPOT)
                {
                    pLightSceneNode->SetShadowMapIndex(nShadowMapIndex);
                    nShadowMapIndex++;
                }

                drawLists.m_aDrawLists[DrawLists::DL_LIGHT].push_back(pLightSceneNode);
                pLightSceneNode->SetWorldMatrix(mWorldMatrix);
            }
            // Gather gaussian splats
            else if (GaussianSplatsSceneNode* pGaussianSplatsSceneNode = dynamic_cast<GaussianSplatsSceneNode*>(pNode.get()))
            {
                drawLists.m_aDrawLists[DrawLists::DL_GS].push_back(pNode.get());
            }

            // Setup per obj data
            if (pNode->GetPerObjId() == -1)
            {
                PerObjData perObjData;

                perObjData.mWorldMatrix = mWorldMatrix;
                perObjData.nSubmeshCount = nSubmeshCount;
                memcpy(perObjData.vSubmeshDatas, aSubmeshDatas.data(), nSubmeshCount * sizeof(PerSubmeshData));
                pNode->SetPerObjId(static_cast<int>(GetPerObjResourceManager()->AppendPerObjData(perObjData)));
            }
            else
            {
                assert("TODO: update node date");
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

}  // namespace Muyo
