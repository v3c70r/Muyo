#pragma once
#include <string>
// ImGui Debug UI pages
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
};


