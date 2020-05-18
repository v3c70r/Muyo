#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

#include "MeshVertex.h"
#include "VertexBuffer.h"
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
        m_nIndexCount = indices.size();
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

private:
    VertexBuffer m_vertexBuffer;
    IndexBuffer m_indexBuffer;
    uint32_t m_nIndexCount;
    glm::mat4 m_mLocalTrans = glm::mat4(1.0);
};

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
    const std::vector<std::unique_ptr<Primitive>>& getPrimitives() const
    {
        return m_vPrimitives;
    }

private:
    std::vector<std::unique_ptr<Primitive>> m_vPrimitives;
    glm::mat4 m_mModel = glm::mat4(1.0);
};

class Scene
{
private:
    std::unordered_map<std::string, Geometry> m_mScene;
};

std::unique_ptr<Geometry> getQuad();

Geometry loadObj(const std::string& path);

