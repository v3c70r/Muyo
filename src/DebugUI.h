#pragma once
#include <string>
#include <imgui.h>
// ImGui Debug UI pages
class SceneNode;
class IDebugUIPage
{
public:
    IDebugUIPage() = delete;
    explicit IDebugUIPage(const std::string& sName) : m_sName(sName) {}
    virtual void Render() const = 0;
    virtual bool ShouldRender() const = 0;
    virtual ~IDebugUIPage() {}
protected:
    std::string m_sName;
};

class ResourceManagerDebugPage : public IDebugUIPage
{
public:
    ResourceManagerDebugPage() : IDebugUIPage("Default Resource Manager Debug Page") {}
    explicit ResourceManagerDebugPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~ResourceManagerDebugPage() override{};
};

class SceneDebugPage : public IDebugUIPage
{
public:
    SceneDebugPage() : IDebugUIPage("Default Scene Debug Page") {}
    explicit SceneDebugPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~SceneDebugPage() override{};
private:
    void DisplaySceneNodeInfo(const SceneNode& sceneNode) const;
};

class DemoDebugPage : public IDebugUIPage
{
public:
    DemoDebugPage() : IDebugUIPage("ImGui Demo") {}
    explicit DemoDebugPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~DemoDebugPage() override{};
};

// Home of all vertical tabs on the left side
class VerticalTabsPage : public IDebugUIPage
{
public:
    VerticalTabsPage() : IDebugUIPage("Vertical Tabs") {}
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
    DockSpace() : IDebugUIPage("DockSpace") {}
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
    EnvironmentMapDebugPage() : IDebugUIPage("Default Resource Manager Debug Page") {}
    explicit EnvironmentMapDebugPage(const std::string& sName) : IDebugUIPage(sName) {}
    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~EnvironmentMapDebugPage() override{};
};


