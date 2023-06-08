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
class Material;
class Submesh
{
public:
    // Construct submesh with vertices and indices. This will generate a separated vertex buffer
    Submesh(const std::string& sName, const std::vector<Vertex>& vertices,
              const std::vector<Index>& indices)
    {
        m_pVertexBuffer = GetRenderResourceManager()->GetVertexBuffer<Vertex>(sName + "_vertex", vertices);
        m_nVertexCount = (uint32_t)vertices.size();
        m_pIndexBuffer = GetRenderResourceManager()->GetIndexBuffer(sName + "_index", indices);
        m_nIndexCount = (uint32_t)indices.size();
        m_nFirstIndex = 0;
        m_nFirstVertex = 0;
    }
    VkBuffer GetVertexDeviceBuffer() const
    {
        return m_pVertexBuffer->buffer();
    }

    VkBuffer GetIndexDeviceBuffer() const
    {
        return m_pIndexBuffer->buffer();
    }

    uint32_t GetIndexCount() const
    {
        return m_nIndexCount;
    }

    uint32_t GetVertexCount() const
    {
        return m_nVertexCount;
    }
    
    uint32_t GetFirstIndex() const
    {
        return m_nFirstIndex;
    }
    
    uint32_t GetFirstVertex() const
    {
        return m_nFirstVertex;
    }

    void SetMaterial(Material* pMaterial) { m_pMaterial = pMaterial; }
    const Material* GetMaterial() const { return m_pMaterial; }

private:
    VertexBuffer<Vertex>* m_pVertexBuffer = nullptr;
    IndexBuffer* m_pIndexBuffer = nullptr;
    uint32_t m_nFirstIndex = 0;
    uint32_t m_nIndexCount = 0;
    uint32_t m_nVertexCount = 0;
    uint32_t m_nFirstVertex = 0;
    Material* m_pMaterial = nullptr;
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
    Geometry* GetQuad();
    Geometry* GetCube();

private:
    int m_nQuadIdx = -1;  // Quad geometry idx
    int m_nCubeIdx = -1;  // Cube geometry idx
};

GeometryManager* GetGeometryManager();

std::unique_ptr<Geometry> LoadObj(const std::string& path, glm::mat4 mTransformation = glm::mat4(1.0));

std::unique_ptr<Geometry> GetSkybox();

}  // namespace Muyo
