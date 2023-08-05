#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

#include "MeshVertex.h"
#include "RenderResourceManager.h"
#include "UniformBuffer.h"
#include "VertexBuffer.h"
namespace Muyo
{
struct Mesh;

class MeshletSubmesh
{
public:
private:
    std::vector<Vertex> m_vVertices;    // All the vertices for the submesh
    std::vector<Index> m_vIndices;      // Index into the m_vVertices
                                        //
    std::vector<uint8_t> m_vTraingleIndices;    // Index into each meshlet
};
class Material;
class Submesh
{
public:
    Submesh(size_t nMeshIndex) : m_nMeshIndex(nMeshIndex) {}

    void SetMaterial(Material* pMaterial) { m_pMaterial = pMaterial; }
    const Material* GetMaterial() const { return m_pMaterial; }

    void SetMeshIndex(size_t index)
    {
        m_nMeshIndex = index;
    }

    size_t GetMeshIndex() const
    {
        return m_nMeshIndex;
    }

private:
    Material* m_pMaterial = nullptr;
    size_t m_nMeshIndex = 0;  // Index in MeshResourceManager
    Mesh* m_pMesh = nullptr;
};

// Simplify the types
using SubmeshList = std::vector<std::unique_ptr<Submesh>>;
using SubmeshListConstRef = const SubmeshList&;
class Geometry
{
public:
    Geometry(std::vector<std::unique_ptr<Submesh>>& submeshs)
    {
        for (auto& prim : submeshs)
        {
            m_vSubmeshes.push_back(std::move(prim));
        }
    }
    Geometry(std::unique_ptr<Submesh> pSubmesh)
    {
        m_vSubmeshes.push_back(std::move(pSubmesh));
    }
    void appendSubmesh(std::unique_ptr<Submesh> pSubmesh)
    {
        m_vSubmeshes.push_back(std::move(pSubmesh));
    }
    SubmeshListConstRef getSubmeshes() const
    {
        return m_vSubmeshes;
    }
    void SetWorldMatrix(const glm::mat4& mObjectToWorld)
    {
        assert(m_mWorldMatrixBuffer != nullptr);
        m_mWorldMatrixBuffer->SetData(mObjectToWorld);
        m_mWorldMatrix = mObjectToWorld;
    }

    const glm::mat4& GetWorldMatrix() const
    {
        return m_mWorldMatrix;
    }

    const UniformBuffer<glm::mat4>* GetWorldMatrixBuffer() const
    {
        return m_mWorldMatrixBuffer;
    }

    void SetWorldMatrixUniformBuffer(UniformBuffer<glm::mat4>* pWorldMatBuffer)
    {
        m_mWorldMatrixBuffer = pWorldMatBuffer;
    }

private:
    std::vector<std::unique_ptr<Submesh>> m_vSubmeshes;
    UniformBuffer<glm::mat4>* m_mWorldMatrixBuffer = nullptr;
    glm::mat4 m_mWorldMatrix = glm::mat4(1.0);  // Cached world matrix
};

class GeometryManager
{
public:
    std::vector<std::unique_ptr<Geometry>> vpGeometries;
    void Destroy() { vpGeometries.clear(); }
};

GeometryManager* GetGeometryManager();

}  // namespace Muyo
