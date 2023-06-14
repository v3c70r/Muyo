#include "MeshResourceManager.h"
#include "RenderResourceManager.h"

#include <algorithm>
#include <initializer_list>

namespace Muyo
{

static MeshResourceManager s_MeshResourceManager;

MeshResourceManager *GetMeshResourceManager()
{
    return &s_MeshResourceManager;
}


// Implement MeshResourceManager functions
size_t MeshResourceManager::AppendMesh(const std::vector<Vertex>& vVertices, const std::vector<Index>& vIndices)
{
    auto& meshIndices = m_MeshVertexResources.m_vIndices;
    auto& meshVertices = m_MeshVertexResources.m_vVertices;

    const uint32_t indexOffset = static_cast<uint32_t>(meshIndices.size());
    const uint32_t vertexOffset = static_cast<uint32_t>(meshVertices.size());

    // Insert indices into the mesh indices vector and add the index by offset
    auto firstInserted = meshIndices.insert(meshIndices.end(), vIndices.begin(), vIndices.end());
    for (auto it = firstInserted; it < meshIndices.end(); it++)
    {
        *it += vertexOffset;
    }

    // Insert vertices
    meshVertices.insert(meshVertices.end(), vVertices.begin(), vVertices.end());

    m_vMeshes.push_back(Mesh({vertexOffset, static_cast<uint32_t>(vVertices.size()), indexOffset, static_cast<uint32_t>(vIndices.size())}));
    return m_vMeshes.size() - 1;
}

void MeshResourceManager::UploadMeshData()
{
    m_MeshVertexResources.m_pVertexBuffer = GetRenderResourceManager()->GetVertexBuffer(m_sVertexBufferName, m_MeshVertexResources.m_vVertices);
    m_MeshVertexResources.m_pIndexBuffer = GetRenderResourceManager()->GetIndexBuffer(m_sIndexBufferName, m_MeshVertexResources.m_vIndices);
    m_bHasUploaded = true;
}

void MeshResourceManager::PrepareSimpleMeshes()
{
    static std::vector<Vertex> vVertices = {
        {{-1.0f, -1.0f, 0.0}, {0.0, 0.0, 1.0}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0}, {0.0, 0.0, 1.0}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0}, {0.0, 0.0, 1.0}, {1.0f, 1.0f, 1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0}, {0.0, 0.0, 1.0}, {0.0f, 1.0f, 1.0f, 0.0f}}};

    static std::vector<uint32_t> vIndices = {0, 1, 2, 2, 3, 0};
    m_aSimpleMeshes[SIMPLE_MESH_QUAD] = AppendMesh(vVertices, vIndices);

    vVertices = {
        {{-0.5, 0.5, -0.5}, {0, 1, 0}, {0, 1, 0, 0}},
        {{-0.5, 0.5, 0.5}, {0, 1, 0}, {0, 0, 0, 0}},
        {{0.5, 0.5, 0.5}, {0, 1, 0}, {1, 0, 0, 0}},
        {{-0.5, 0.5, -0.5}, {0, 1, 0}, {0, 1, 0, 0}},
        {{0.5, 0.5, 0.5}, {0, 1, 0}, {1, 0, 0, 0}},
        {{0.5, 0.5, -0.5}, {0, 1, 0}, {1, 1, 0, 0}},
        {{-0.5, 0.5, -0.5}, {-1, 0, 0}, {0, 1, 0, 0}},
        {{-0.5, -0.5, -0.5}, {-1, 0, 0}, {0, 0, 0, 0}},
        {{-0.5, -0.5, 0.5}, {-1, 0, 0}, {1, 0, 0, 0}},
        {{-0.5, 0.5, -0.5}, {-1, 0, 0}, {0, 1, 0, 0}},
        {{-0.5, -0.5, 0.5}, {-1, 0, 0}, {1, 0, 0, 0}},
        {{-0.5, 0.5, 0.5}, {-1, 0, 0}, {1, 1, 0, 0}},
        {{0.5, 0.5, 0.5}, {1, 0, 0}, {0, 1, 0, 0}},
        {{0.5, -0.5, 0.5}, {1, 0, 0}, {0, 0, 0, 0}},
        {{0.5, -0.5, -0.5}, {1, 0, 0}, {1, 0, 0, 0}},
        {{0.5, 0.5, 0.5}, {1, 0, 0}, {0, 1, 0, 0}},
        {{0.5, -0.5, -0.5}, {1, 0, 0}, {1, 0, 0, 0}},
        {{0.5, 0.5, -0.5}, {1, 0, 0}, {1, 1, 0, 0}},
        {{0.5, 0.5, -0.5}, {0, 0, -1}, {0, 1, 0, 0}},
        {{0.5, -0.5, -0.5}, {0, 0, -1}, {0, 0, 0, 0}},
        {{-0.5, -0.5, -0.5}, {0, 0, -1}, {1, 0, 0, 0}},
        {{0.5, 0.5, -0.5}, {0, 0, -1}, {0, 1, 0, 0}},
        {{-0.5, -0.5, -0.5}, {0, 0, -1}, {1, 0, 0, 0}},
        {{-0.5, 0.5, -0.5}, {0, 0, -1}, {1, 1, 0, 0}},
        {{-0.5, 0.5, 0.5}, {0, 0, 1}, {0, 1, 0, 0}},
        {{-0.5, -0.5, 0.5}, {0, 0, 1}, {0, 0, 0, 0}},
        {{0.5, -0.5, 0.5}, {0, 0, 1}, {1, 0, 0, 0}},
        {{-0.5, 0.5, 0.5}, {0, 0, 1}, {0, 1, 0, 0}},
        {{0.5, -0.5, 0.5}, {0, 0, 1}, {1, 0, 0, 0}},
        {{0.5, 0.5, 0.5}, {0, 0, 1}, {1, 1, 0, 0}},
        {{-0.5, -0.5, 0.5}, {0, -1, 0}, {0, 1, 0, 0}},
        {{-0.5, -0.5, -0.5}, {0, -1, 0}, {0, 0, 0, 0}},
        {{0.5, -0.5, -0.5}, {0, -1, 0}, {1, 0, 0, 0}},
        {{-0.5, -0.5, 0.5}, {0, -1, 0}, {0, 1, 0, 0}},
        {{0.5, -0.5, -0.5}, {0, -1, 0}, {1, 0, 0, 0}},
        {{0.5, -0.5, 0.5}, {0, -1, 0}, {1, 1, 0, 0}},
    };
    
    vIndices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35 };
    m_aSimpleMeshes[SIMPLE_MESH_CUBE] = AppendMesh(vVertices, vIndices);
}

}  // namespace Muyo
