#pragma once
#include <memory>

#include "Geometry.h"

namespace Muyo
{
    
enum SimpleMeshes
{
    SIMPLE_MESH_QUAD = 0,
    SIMPLE_MESH_CUBE = 1,
    SIMPLE_MESH_COUNT

};
// MeshResourceManager contains all the vertex and index buffers. As well as buffers for meshlets.

struct MeshVertexResources
{
    // CPU Data
    std::vector<Vertex> m_vVertices;
    std::vector<Index> m_vIndices;

    // GPU Data
    VertexBuffer<Vertex>* m_pVertexBuffer = nullptr;
    IndexBuffer* m_pIndexBuffer = nullptr;
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

struct MeshletMesh
{
};

class MeshResourceManager
{
public:
    size_t AppendMesh(const std::vector<Vertex>& vVertices, const std::vector<Index>& vIndices);
    void UploadMeshData();
    void PrepareSimpleMeshes();

    const Mesh& GetMesh(size_t index) const
    {
        return m_vMeshes[index];
    }
    
    const Mesh& GetQuad() const
    {
        return GetMesh(m_aSimpleMeshes[SIMPLE_MESH_QUAD]);
    }

    const Mesh& GetCube() const
    {
        return GetMesh(m_aSimpleMeshes[SIMPLE_MESH_CUBE]);
    }

    const MeshVertexResources& GetMeshVertexResources()
    {
        return m_MeshVertexResources;
    }

private:
    std::vector<Mesh> m_vMeshes;
    MeshVertexResources m_MeshVertexResources;

    const std::string m_sVertexBufferName = "MeshVertexBuffer";
    const std::string m_sIndexBufferName = "MeshIndexBuffer";
    
    std::array<size_t, SIMPLE_MESH_COUNT> m_aSimpleMeshes;
    
    bool m_bHasUploaded = false;
};


MeshResourceManager* GetMeshResourceManager();

}  // namespace Muyo
