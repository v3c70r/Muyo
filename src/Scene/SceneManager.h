#pragma once
#include <optional>
#include <unordered_map>

#include "Scene.h"

namespace Muyo
{

#include <string>
struct LightData;
class SceneManager
{
    using SceneMap = std::unordered_map<std::string, Scene>;

public:
    const Scene& GetScene(const std::string& sSceneName) const { return m_mScenes.at(sSceneName); }
    const SceneMap& GetAllScenes() const { return m_mScenes; }
    void LoadSceneFromFile(const std::string& sPath);
    DrawLists GatherDrawLists();

    /// @return The world-space bounds of every loaded scene, or `std::nullopt` if none holds geometry.
    ///
    /// The app frames its camera from this, so a scene's scale stops being a constant in the app.
    std::optional<AABB> GetWorldAABB() const;
    static StorageBuffer<LightData>* ConstructLightBufferFromDrawLists(const DrawLists& dl);

private:
    SceneMap m_mScenes;
};

struct DrawData
{
};

SceneManager* GetSceneManager();
}  // namespace Muyo
