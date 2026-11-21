#include "Scene.h"

#include <imgui.h>

#include <functional>
#include <stdexcept>

#include "Geometry.h"
#include "LightSceneNode.h"
#include "PerObjResourceManager.h"
#include "RenderResourceManager.h"

namespace Muyo
{

void SceneNode::AppendChild(SceneNode *node) { m_vpChildren.emplace_back(node); }

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

std::optional<AABB> Scene::GetWorldAABB() const
{
    std::optional<AABB> bounds;
    std::function<void(const std::unique_ptr<SceneNode> &, const glm::mat4 &)> Walk =
        [&](const std::unique_ptr<SceneNode> &pNode, const glm::mat4 &mParentToWorld)
    {
        const glm::mat4 mNodeToWorld = mParentToWorld * pNode->GetMatrix();
        // Only geometry nodes carry bounds. Every other node defaults to a zero-sized box at the
        // origin, which would silently drag the result towards the world origin.
        if (dynamic_cast<const GeometrySceneNode *>(pNode.get()) != nullptr)
        {
            const AABB local = pNode->GetAABB();
            // All eight corners: transforming min and max alone is wrong for any rotation, which is
            // the case an axis-aligned box cannot express.
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 vLocal(corner & 1 ? local.vMax.x : local.vMin.x,
                                       corner & 2 ? local.vMax.y : local.vMin.y,
                                       corner & 4 ? local.vMax.z : local.vMin.z);
                const glm::vec3 vWorld = glm::vec3(mNodeToWorld * glm::vec4(vLocal, 1.0f));
                if (!bounds.has_value())
                {
                    bounds = AABB{vWorld, vWorld};
                }
                else
                {
                    bounds->vMin = glm::min(bounds->vMin, vWorld);
                    bounds->vMax = glm::max(bounds->vMax, vWorld);
                }
            }
        }
        for (const auto &pChild : pNode->GetChildren())
        {
            Walk(pChild, mNodeToWorld);
        }
    };
    // Identity is the root's parent, so a root transform is applied exactly once. (`GatherDrawLists`
    // hands the root matrix in as the parent and then multiplies by it again; the root is identity in
    // every scene the importer builds, so the two agree today, and this walk is the correct one if that
    // ever stops being true.)
    Walk(m_pRoot, glm::mat4(1.0f));
    return bounds;
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
        std::function<void(const std::unique_ptr<SceneNode> &, const glm::mat4 &, DrawLists &)> FlattenTreeRecursive =
            [&](const std::unique_ptr<SceneNode> &pNode, const glm::mat4 &mCurrentTrans, DrawLists &drawLists)
        {
            glm::mat4 mWorldMatrix = mCurrentTrans * pNode->GetMatrix();
            // Node transforms are asset data: a malformed scene must fail in Release too, not just
            // under an assert that NDEBUG removes.
            if (!IsMat4Valid(mWorldMatrix))
            {
                throw std::runtime_error("Scene: node '" + pNode->GetName() + "' has a non-finite world matrix.");
            }
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

                for (auto &submesh : pGeometry->getSubmeshes())
                {
                    if (nSubmeshCount >= MAX_NUM_SUBMESHES)
                    {
                        throw std::runtime_error("Scene: node '" + pNode->GetName() + "' has more than " +
                                                 std::to_string(MAX_NUM_SUBMESHES) + " submeshes.");
                    }
                    if (!submesh->HasMaterial())
                    {
                        throw std::runtime_error("Scene: node '" + pNode->GetName() + "' submesh " +
                                                 std::to_string(nSubmeshCount) + " has no material.");
                    }
                    // The shader indexes the material buffer with this value; the mesh index is a
                    // different table and happens to differ for most of the Mazda scene (#267).
                    aSubmeshDatas[nSubmeshCount++].nMaterialIndex = static_cast<uint32_t>(submesh->GetMaterialIndex());
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

            // Setup per obj data
            if (pNode->GetPerObjId() == -1)
            {
                PerObjData perObjData{};

                perObjData.mWorldMatrix = mWorldMatrix;
                perObjData.nSubmeshCount = nSubmeshCount;
                memcpy(perObjData.vSubmeshDatas, aSubmeshDatas.data(), nSubmeshCount * sizeof(PerSubmeshData));
                // Local-space bounds, used by the GPU culling pass (transformed by mWorldMatrix).
                const AABB aabb = pNode->GetAABB();
                perObjData.vAABBMin[0] = aabb.vMin.x;
                perObjData.vAABBMin[1] = aabb.vMin.y;
                perObjData.vAABBMin[2] = aabb.vMin.z;
                perObjData.vAABBMax[0] = aabb.vMax.x;
                perObjData.vAABBMax[1] = aabb.vMax.y;
                perObjData.vAABBMax[2] = aabb.vMax.z;
                pNode->SetPerObjId(static_cast<int>(GetPerObjResourceManager()->AppendPerObjData(perObjData)));
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
