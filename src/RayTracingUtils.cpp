#include "RayTracingUtils.h"

#include <functional>

#include "Geometry.h"
#include "MeshVertex.h"
#include "Scene.h"
#include "VkRenderDevice.h"
#include "RenderResourceManager.h"

BLASInput ConstructBLASInput(const Scene &Scene)
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

                VkAccelerationStructureGeometryKHR geometry = {};
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
        for (const auto &child : pNode->GetChildren())
        {
            ConstructBLASInputRecursive(child.get(), blasInput);
        }
    };
    BLASInput input;
    ConstructBLASInputRecursive(Scene.GetRoot().get(), input);
    return input;
}

void BuildBLAS(const std::vector<BLASInput> &vBLASInputs, VkBuildAccelerationStructureFlagsKHR flags)
{
    std::vector<BLASInput> vCpy = vBLASInputs;
    uint32_t nNumBlas = (uint32_t)vCpy.size();
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(nNumBlas);
    for (uint32_t idx = 0; idx < nNumBlas; idx++)
    {
        buildInfos[idx].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfos[idx].flags = flags;
        buildInfos[idx].geometryCount = (uint32_t)vCpy[idx].m_vRangeInfo.size();
        buildInfos[idx].pGeometries = vCpy[idx].m_vGeometries.data();
        buildInfos[idx].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfos[idx].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfos[idx].srcAccelerationStructure = VK_NULL_HANDLE;
    }
    for (size_t idx = 0; idx < nNumBlas; idx++)
    {
        // Query both the size of the finished acceleration structure and the  amount of scratch memory
        // needed (both written to sizeInfo). The `vkGetAccelerationStructureBuildSizesKHR` function
        // computes the worst case memory requirements based on the user-reported max number of
        // primitives. Later, compaction can fix this potential inefficiency.
        std::vector<uint32_t> maxPrimCount(vCpy[idx].m_vRangeInfo.size());
        for (auto tt = 0; tt < vCpy[idx].m_vRangeInfo.size(); tt++)
        {
            maxPrimCount[tt] = vCpy[idx].m_vRangeInfo[tt].primitiveCount;  // Number of primitives/triangles
        }
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};

        static auto func = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(GetRenderDevice()->GetInstance(), "vkGetAccelerationStructureBuildSizesKHR");
        func(GetRenderDevice()->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfos[idx], maxPrimCount.data(), &sizeInfo);

        // Create acceleration structure object. Not yet bound to memory.
        VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        createInfo.size = sizeInfo.accelerationStructureSize;  // Will be used to allocate memory.

        // Actual allocation of buffer and acceleration structure. Note: This relies on createInfo.offset == 0
        // and fills in createInfo.buffer with the buffer allocated to store the BLAS. The underlying
        // vkCreateAccelerationStructureKHR call then consumes the buffer value.
        //GetRenderResourceManager()->GetAccelerationStructureBuffer("scene", 
        //vCpy[idx].as = m_alloc->createAcceleration(createInfo);
        //m_debug.setObjectName(vCpy[idx].as.accel, (std::string("Blas" + std::to_string(idx)).c_str()));
        //buildInfos[idx].dstAccelerationStructure = vCpy[idx].as.accel;  // Setting the where the build lands

        //// Keeping info
        //vCpy[idx].flags = flags;
        //maxScratch = std::max(maxScratch, sizeInfo.buildScratchSize);

        //// Stats - Original size
        //originalSizes[idx] = sizeInfo.accelerationStructureSize;
    }
}
