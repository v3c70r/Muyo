#pragma once
#include <imgui.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// ImGui Debug UI pages
namespace Muyo
{
class SceneNode;
class Camera;
class IDebugUIPage
{
public:
    IDebugUIPage() = delete;
    explicit IDebugUIPage(const std::string& sName) : m_sName(sName) {}
    virtual void Render() const = 0;
    virtual bool ShouldRender() const = 0;
    virtual ~IDebugUIPage() {}
    std::string GetName() const { return m_sName; }

protected:
    std::string m_sName;
};

class ResourceManagerDebugPage : public IDebugUIPage
{
public:
    explicit ResourceManagerDebugPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~ResourceManagerDebugPage() override{};
};

class SceneDebugPage : public IDebugUIPage
{
public:
    explicit SceneDebugPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~SceneDebugPage() override{};

private:
    void DisplaySceneNodeInfo(const SceneNode& sceneNode) const;
    void DrawGizmoOnSceneNode(const SceneNode* pSceneNode, glm::mat4& mWorld, const glm::mat4& mView, const glm::mat4& mProj) const;
    mutable const SceneNode* m_pSelectedNode = nullptr;
};

class DemoDebugPage : public IDebugUIPage
{
public:
    explicit DemoDebugPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~DemoDebugPage() override{};
};

// Home of all vertical tabs on the left side
class VerticalTabsPage : public IDebugUIPage
{
public:
    explicit VerticalTabsPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~VerticalTabsPage() override{};

private:
    // A list of tabs
    uint32_t uCurrentSelection = 0;
};

// A full scree dock space page
class DockSpace : public IDebugUIPage
{
public:
    explicit DockSpace(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~DockSpace() override {}

private:
    const ImGuiWindowFlags WINDOW_FLAG = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
};

class EnvironmentMapDebugPage : public IDebugUIPage
{
public:
    explicit EnvironmentMapDebugPage(const std::string& sName) : IDebugUIPage(sName)
    {
        const std::string sRootPath = "assets/hdr";
        for (const auto& hdrEntry : std::filesystem::directory_iterator(sRootPath))
        {
            if (hdrEntry.path().extension() == ".hdr")
            {
                m_vHDRImagePathes.push_back(hdrEntry);
                m_vHDRImagePatheStrings.push_back(m_vHDRImagePathes.back().filename().string());
            }
        }
    }
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~EnvironmentMapDebugPage() override{};

private:
    std::vector<std::filesystem::path> m_vHDRImagePathes;
    std::vector<std::string> m_vHDRImagePatheStrings;
    mutable int m_nCurrentHDRIndex = 0;
};

class LightSceneNode;
class LightsDebugPage : public IDebugUIPage
{
public:
    explicit LightsDebugPage(const std::string& sName);
    void UpdateLightNodes(const std::vector<const LightSceneNode*> vpLightNodes)
    {
        m_vpLightNodes = vpLightNodes;
    }
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~LightsDebugPage() override {}

private:
    std::vector<const LightSceneNode*> m_vpLightNodes;
};

class CameraDebugPage : public IDebugUIPage
{
public:
    explicit CameraDebugPage(const std::string& sName)
        : IDebugUIPage(sName) {}
    void SetCamera(Camera* pCamera) { m_pCamera = pCamera; }
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~CameraDebugPage() override {}

private:
    Camera* m_pCamera = nullptr;
};
}  // namespace Muyo
