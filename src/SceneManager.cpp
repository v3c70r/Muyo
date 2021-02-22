#include "SceneManager.h"
#include "SceneImporter.h"
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
    return dls;
}
