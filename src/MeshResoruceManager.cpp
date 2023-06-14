#include "MeshResoruceManager.h"
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

    const uint32_t indexOffset = meshIndices.size();
    const uint32_t vertexOffset = meshVertices.size();

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
    GetRenderResourceManager()->GetVertexBuffer(m_sVertexBufferName, m_MeshVertexResources.m_vVertices);
    GetRenderResourceManager()->GetIndexBuffer(m_sIndexBufferName, m_MeshVertexResources.m_vIndices);
}

}  // namespace Muyo
