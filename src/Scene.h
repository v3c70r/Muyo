#pragma once
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <string>
#include <sstream>

class SceneNode
{
public:
    SceneNode(const std::string& name) : m_sName(name){}
    SceneNode() : m_sName("Root"){}
    virtual ~SceneNode() {}
    void SetName(const std::string& name) { m_sName = name; }
    const std::string& GetName() const { return m_sName; }

    void SetMatrix(const glm::mat4& mMat)
    {
        m_mTransformation = mMat;
    }
    virtual void AppendChild(SceneNode*);
    const std::vector<std::unique_ptr<SceneNode>>& GetChildren() const
    {
        return m_vpChildren;
    }

protected:
    std::string m_sName;
    std::vector<std::unique_ptr<SceneNode>> m_vpChildren;
    glm::mat4 m_mTransformation = glm::mat4(1.0);
};

class Scene
{
public:
    SceneNode* GetRoot() { return m_pRoot.get(); }
    const SceneNode* GetRoot() const { return m_pRoot.get(); }
    std::string ConstructDebugString() const;
protected:
    void ConstructDebugStringRecursive(const SceneNode* node, int layer, std::stringstream &ss) const;
    std::unique_ptr<SceneNode> m_pRoot = std::make_unique<SceneNode>();
};

class Geometry;
class GeometrySceneNode : public SceneNode
{
protected:
    Geometry* pGeometry;
};
