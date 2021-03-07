#include "RayTracingUtils.h"
#include "Scene.h"
#include "Geometry.h"
#include "MeshVertex.h"
#include "VkRenderDevice.h"
#include <functional>

//#ifdef ENABLE_RAY_TRACINE
BLASInput ConstructBLASInput(const Scene& Scene)
{
    std::function<void(const SceneNode *, BLASInput &)> ConstructBLASInputRecursive = [&](const SceneNode *pNode, BLASInput &blasInput) {
        const GeometrySceneNode *pGeometryNode = dynamic_cast<const GeometrySceneNode *>(pNode);
        if (pGeometryNode)
        {
            for (const auto &primitive : pGeometryNode->GetGeometry()->getPrimitives())
            {
                VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
                // Vertex data
                triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                triangles.vertexData.deviceAddress = GetRenderDevice()->GetBufferDeviceAddress(primitive->getVertexDeviceBuffer());
                // Index data
                triangles.vertexStride = sizeof(Vertex);
                triangles.indexType = VK_INDEX_TYPE_UINT32;
                triangles.indexData.deviceAddress = GetRenderDevice()->GetBufferDeviceAddress(primitive->getIndexDeviceBuffer());
                // misc
                triangles.transformData = {};
                triangles.maxVertex = primitive->getVertexCount();

                VkAccelerationStructureGeometryKHR geometry ={};
                geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                geometry.pNext = nullptr;
                geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                geometry.geometry.triangles = triangles;

                VkAccelerationStructureBuildRangeInfoKHR range = {};
                range.firstVertex = 0;
                range.primitiveCount = primitive->getIndexCount() / 3;
                range.primitiveOffset = 0;
                range.transformOffset = 0;
                blasInput.m_vGeometries.emplace_back(geometry);
                blasInput.m_vRangeInfo.emplace_back(range);
            }
        }
        for (const auto& child : pNode->GetChildren())
        {
            ConstructBLASInputRecursive(child.get(), blasInput);
        }
    };
    BLASInput input;
    ConstructBLASInputRecursive(Scene.GetRoot().get(), input);
    return input;
}
//#endif