#pragma once
#include <memory>

#include "Geometry.h"

namespace Muyo
{
// MeshResourceManager contains all the vertex and index buffers. As well as buffers for meshlets.

struct MeshVertexResources
{
    // CPU Data
    std::vector<Vertex> m_vVertices;
    std::vector<Index> m_vIndices;

    // GPU Data
    std::unique_ptr<VertexBuffer<Vertex>> m_pVertexBuffer;
    std::unique_ptr<IndexBuffer> m_pIndexBuffer;
};

struct MeshletDesc
{
    size_t nVertexIndexOffset;
    size_t nVertexIndexCount;

    size_t nTriangleIndexOffset;
    size_t nTriangleIndexCount;

    // TODO: Other meshlet data
};

struct MeshletMeshVertexResources
{
    std::vector<MeshletDesc> m_vMeshlets;

    std::vector<Vertex> m_vVertices;
    std::vector<Index> m_vVertexIndices;
    std::vector<uint8_t> m_vTraingleIndices;    // Index of triangle in each meshlet
};

struct Mesh
{
    uint32_t m_nVertexOffset;
    uint32_t m_nVertexCount;

    uint32_t m_nIndexOffset;
    uint32_t m_nIndexCount;
};

class MeshResourceManager
{
public:
    size_t AppendMesh(const std::vector<Vertex>& vVertices, const std::vector<Index>& vIndices);
    void UploadMeshData();

private:
    std::vector<Mesh> m_vMeshes;
    MeshVertexResources m_MeshVertexResources;

    const std::string m_sVertexBufferName = "MeshVertexBuffer";
    const std::string m_sIndexBufferName = "MeshIndexBuffer";
};


MeshResourceManager* GetMeshResourceManager();

class MeshletMesh
{
};
}  // namespace Muyo
