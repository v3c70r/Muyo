#pragma once
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

static const uint32_t TRANSPARENT_FLAG = 1;

struct AABB
{
    glm::vec3 vMin = glm::vec3(0.0);
    glm::vec3 vMax = glm::vec3(0.0);
};

class SceneNode
{
public:
    SceneNode(const std::string& name) : m_sName(name) {}
    SceneNode() : m_sName("Root") {}
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

    void SetAABB(AABB aabb)
    {
        m_AABB = aabb;
    }
    AABB GetAABB() const
    {
        return m_AABB;
    }

protected:
    std::string m_sName;
    std::vector<std::unique_ptr<SceneNode>> m_vpChildren;
    glm::mat4 m_mTransformation = glm::mat4(1.0);
    AABB m_AABB;
    uint32_t m_uFlag = 0;
};

struct DrawLists
{
public:
    enum Lists
    {
        DL_TRANSPARENT,
        DL_OPAQUE,
        DL_LIGHT,
        DL_COUNT
    };
    std::array<std::vector<const SceneNode*>, DL_COUNT> m_aDrawLists;
};

template<typename T> class StorageBuffer;

class Scene
{
public:
    Scene() : m_sName("sans_nom") {}
    explicit Scene(const std::string& sName) : m_sName(sName) {}
    SceneNode* GetRoot() { return m_pRoot.get(); }
    const std::unique_ptr<SceneNode>& GetRoot() const { return m_pRoot; }
    const DrawLists& GatherDrawLists();
    std::string ConstructDebugString() const;

    static bool IsMat4Valid(const glm::mat4& mat)
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
    void SetName(const std::string& sName) { m_sName = sName; }
    const std::string& GetName() const { return m_sName; }

protected:
    std::unique_ptr<SceneNode> m_pRoot = std::make_unique<SceneNode>();
    DrawLists m_drawLists;
    std::string m_sName;
    bool m_bAreDrawListsDirty = true;
};

class Geometry;
class GeometrySceneNode : public SceneNode
{
public:
    void SetTransparent() { m_uFlag |= TRANSPARENT_FLAG; }
    bool IsTransparent() const { return m_uFlag & TRANSPARENT_FLAG; }
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

