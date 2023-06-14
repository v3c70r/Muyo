#include "MeshProcessor.h"

#include "Geometry.h"
#include "MeshVertex.h"

//void MeshProcessor::ProcessSubmesh(Muyo::Submesh& submesh)
//{
//    size_t nMaxMeshlets = meshopt_buildMeshletsBound(submesh.GetIndexCount(), MAX_VERTICES, MAX_TRIANGLES);
//
//    std::vector<meshopt_Meshlet> vMeshlets(nMaxMeshlets);
//    std::vector<Muyo::Index> vMeshletVertices(nMaxMeshlets * MAX_VERTICES);
//    std::vector<uint8_t> vMeshletTriangles(nMaxMeshlets * MAX_TRIANGLES * 3);
//    
//
//    // Construct meshlets
//    size_t nMeshletCount = meshopt_buildMeshlets(vMeshlets.data(), vMeshletVertices.data(), vMeshletTriangles.data(), submesh.GetIndices().data(), submesh.GetIndexCount(), &(submesh.GetVertices()[0].pos.x), submesh.GetVertexCount(), sizeof(Muyo::Vertex), MAX_VERTICES, MAX_TRIANGLES, 0);
//
//    // Trim meshlet array
//    const meshopt_Meshlet& lastMeshlet = vMeshlets[nMeshletCount - 1];
//    vMeshletVertices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
//    vMeshletTriangles.resize(lastMeshlet.triangle_offset + lastMeshlet.triangle_count);
//    vMeshlets.resize(nMeshletCount);
//
//    // TODO: Handle bounds
//
//    // TODO: Move meshlet info to MeshletSubmesh
//
//
//}
