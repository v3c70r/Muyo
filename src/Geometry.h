#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

#include "MeshVertex.h"
#include "UniformBuffer.h"
#include "VertexBuffer.h"
class Material;
class Primitive
{
public:
    Primitive(const std::vector<Vertex>& vertices,
              const std::vector<Index>& indices)
    {
        m_vertexBuffer.setData(reinterpret_cast<const void*>(vertices.data()),
                               sizeof(Vertex) * vertices.size());
        m_indexBuffer.setData(reinterpret_cast<const void*>(indices.data()),
                              sizeof(Index) * indices.size());
        m_nIndexCount = (uint32_t)indices.size();
    }
    VkBuffer getVertexDeviceBuffer() const
    {
        return m_vertexBuffer.buffer();
    }

    VkBuffer getIndexDeviceBuffer() const
    {
        return m_indexBuffer.buffer();
    }

    uint32_t getIndexCount() const
    {
        return m_nIndexCount;
    }

    void SetMaterial(Material* pMaterial) { m_pMaterial = pMaterial; }
    const Material* GetMaterial() const { return m_pMaterial; }

private:
    VertexBuffer m_vertexBuffer;
    IndexBuffer m_indexBuffer;
    uint32_t m_nIndexCount;
    Material* m_pMaterial = nullptr;
};

// Simplify the types
using PrimitiveList = std::vector<std::unique_ptr<Primitive>>;
using PrimitiveListRef = PrimitiveList&;
using PrimitiveListConstRef = const PrimitiveList&;
class Geometry
{
public:
    Geometry(std::vector<std::unique_ptr<Primitive>>& primitives)
    {
        for (auto& prim : primitives)
        {
            m_vPrimitives.push_back(std::move(prim));
        }
    }
    Geometry(std::unique_ptr<Primitive> pPrimitive)
    {
        m_vPrimitives.push_back(std::move(pPrimitive));
    }
    void appendPrimitive(std::unique_ptr<Primitive> pPrimitive)
    {
        m_vPrimitives.push_back(std::move(pPrimitive));
    }
    PrimitiveListConstRef getPrimitives() const
    {
        return m_vPrimitives;
    }
    void SetWorldMatrix(const glm::mat4 &mObjectToWorld)
    {
        assert(m_mWorldMatrix != nullptr);
        m_mWorldMatrix->setData(mObjectToWorld);
    }

    const UniformBuffer<glm::mat4>* GetWorldMatrixBuffer() const
    {
        return m_mWorldMatrix;
    }

    void SetWorldMatrixUniformBuffer(UniformBuffer<glm::mat4>* pWorldMatBuffer)
    {
        m_mWorldMatrix = pWorldMatBuffer;
    }

private:
    std::vector<std::unique_ptr<Primitive>> m_vPrimitives;
    UniformBuffer<glm::mat4>* m_mWorldMatrix = nullptr;
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

std::unique_ptr<Geometry> loadObj(const std::string& path, glm::mat4 mTransformation = glm::mat4(1.0));

std::unique_ptr<Geometry> getSkybox();

