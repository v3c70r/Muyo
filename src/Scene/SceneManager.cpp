#include "SceneManager.h"

#include "LightSceneNode.h"
#include "MeshResourceManager.h"
#include "PerObjResourceManager.h"
#include "RenderResourceManager.h"
#include "SceneImporter.h"

namespace Muyo
{

static SceneManager s_sceneManager;

SceneManager* GetSceneManager()
{
    return &s_sceneManager;
}

void SceneManager::LoadSceneFromFile(const std::string& sPath)
{
    GLTFImporter importer;
    std::vector<Scene> scenes = importer.ImportScene(sPath);
    for (auto& scene : scenes)
    {
        assert(m_mScenes.find(scene.GetName()) == m_mScenes.end());
        m_mScenes[scene.GetName()] = std::move(scene);
    }
    GetMeshResourceManager()->PrepareSimpleMeshes();
    GetMeshResourceManager()->UploadMeshData();
}

DrawLists SceneManager::GatherDrawLists()
{
    DrawLists dls;
    for (auto& scenePair : m_mScenes)
    {
        Scene& scene = scenePair.second;
        const DrawLists& sceneDLs = scene.GatherDrawLists();
        for (size_t i = 0; i < sceneDLs.m_aDrawLists.size(); i++)
        {
            auto& sceneDL = sceneDLs.m_aDrawLists[i];
            dls.m_aDrawLists[i].insert(dls.m_aDrawLists[i].end(), sceneDL.begin(), sceneDL.end());
        }
    }
    if (!GetPerObjResourceManager()->HasUploaded())
    {
        GetPerObjResourceManager()->Upload();
    }
    return dls;
}

StorageBuffer<LightData>* SceneManager::ConstructLightBufferFromDrawLists(const DrawLists& dl)
{
    const std::vector<const SceneNode*> lightNodes = dl.m_aDrawLists[DrawLists::DL_LIGHT];
    std::vector<LightData> lightData;
    lightData.reserve(lightNodes.size());
    for (const SceneNode* pNode : lightNodes)
    {
        const LightSceneNode* pLightNode = static_cast<const LightSceneNode*>(pNode);
        lightData.push_back(pLightNode->ConstructLightData());
    }
    // Add a dummy light
    if (lightData.size() == 0)
    {
        lightData.push_back(LightData());
    }
    return GetRenderResourceManager()->GetStorageBuffer("light data", lightData);
}

}  // namespace Muyo
