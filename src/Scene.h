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
    const glm::mat4& GetMatrix() const
    {
        return m_mTransformation;
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
    //const SceneNode* GetRoot() const { return m_pRoot.get(); }
    const std::unique_ptr<SceneNode>& GetRoot() const { return m_pRoot; }
    const std::vector<const SceneNode*>& FlattenTree();
    std::string ConstructDebugString() const;

    static bool IsMat4Valid(const glm::mat4 &mat)
    {
        bool valid = true;
        valid &=
            !glm::any(glm::isnan(mat[0])) &&
            !glm::any(glm::isnan(mat[1])) &&
            !glm::any(glm::isnan(mat[2])) &&
            !glm::any(glm::isnan(mat[3]));
        valid &=
            !glm::any(glm::isinf(mat[0])) &&
            !glm::any(glm::isinf(mat[1])) &&
            !glm::any(glm::isinf(mat[2])) &&
            !glm::any(glm::isinf(mat[3]));
        return valid;
    }

protected:
    std::unique_ptr<SceneNode> m_pRoot = std::make_unique<SceneNode>();
    std::vector<const SceneNode*> m_vpFlattenedNodes;
    bool m_bIsTreeDirty = true;
};

class Geometry;
class GeometrySceneNode : public SceneNode
{
public:
    void SetGeometry(Geometry* pGeometry)
    {
        m_pGeometry = pGeometry;
    }
    const Geometry* GetGeometry() const 
    {
        return m_pGeometry;
    }
    Geometry* GetGeometry()
    {
        return m_pGeometry;
    }

protected:
    Geometry* m_pGeometry;
};

