#include "RayTracingSceneManager.h"

#include <glm/gtc/type_ptr.hpp>

#include "DescriptorManager.h"
#include "Geometry.h"
#include "Scene.h"
#include "VkExtFuncsLoader.h"

namespace Muyo
{

AccelerationStructure* RayTracingSceneManager::BuildBLASfromNode(const SceneNode& sceneNode, VkAccelerationStructureCreateFlagsKHR flags)
{
    const GeometrySceneNode& geometryNode = dynamic_cast<const GeometrySceneNode&>(sceneNode);

    m_mSubmeshBeginIndices[&sceneNode] = static_cast<uint32_t>(m_vSubmeshDescs.size());

    std::vector<VkAccelerationStructureGeometryKHR> vGeometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> vRangeInfo;

    for (const auto& submesh : geometryNode.GetGeometry()->getSubmeshes())
    {
        VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        // Vertex data
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = GetRenderDevice()->GetBufferDeviceAddress(submesh->getVertexDeviceBuffer());
        // Index data
        triangles.vertexStride = sizeof(Vertex);
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = GetRenderDevice()->GetBufferDeviceAddress(submesh->getIndexDeviceBuffer());
        // misc
        triangles.transformData = {};
        triangles.maxVertex = submesh->getVertexCount();

        VkAccelerationStructureGeometryKHR geometry = {};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.pNext = nullptr;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles = triangles;

        VkAccelerationStructureBuildRangeInfoKHR range = {};
        range.firstVertex = 0;
        range.primitiveCount = submesh->getIndexCount() / 3;
        range.primitiveOffset = 0;
        range.transformOffset = 0;
        vGeometries.emplace_back(geometry);
        vRangeInfo.emplace_back(range);

        const Material* pMaterial = submesh->GetMaterial();
        VkDeviceAddress pbrFactorAdd = pMaterial->GetPBRFactorsDeviceAdd();
        SubmeshDescription submeshDesc = {triangles.vertexData.deviceAddress, triangles.indexData.deviceAddress, pbrFactorAdd, {}};
        pMaterial->FillPbrTextureIndices(submeshDesc.m_aPbrTextureIndices);
        m_vSubmeshDescs.emplace_back(submeshDesc);
    }

    // allocate memory for BLAS

    const std::string sAccStructName = "acStruct_" + geometryNode.GetName();

    std::vector<uint32_t> vPrimCounts;
    vPrimCounts.reserve(vRangeInfo.size());
    for (const auto& range : vRangeInfo)
    {
        vPrimCounts.push_back(range.primitiveCount);
    }

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr, 0, 0, 0};

    VkAccelerationStructureBuildGeometryInfoKHR geometryBuildInfo =
        {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,  // sType
            nullptr,                                                           // pNext
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,                   // type
            flags,                                                             // flags
            VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,                    // mode
            VK_NULL_HANDLE,                                                    // srcAccelerationStructure
            VK_NULL_HANDLE,                                                    // dstAccelerationStructure
            static_cast<uint32_t>(vGeometries.size()),                         // geometryCount
            vGeometries.data(),                                                // ppGeometries
            nullptr,                                                           // ppGeometries
            {}                                                                 // scratchData
        };

    VkExt::vkGetAccelerationStructureBuildSizesKHR(GetRenderDevice()->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &geometryBuildInfo, vPrimCounts.data(), &sizeInfo);

    AccelerationStructure* pAccelerationStructure = GetRenderResourceManager()->CreateBLAS(sAccStructName, sizeInfo.accelerationStructureSize);

    geometryBuildInfo.dstAccelerationStructure = pAccelerationStructure->GetAccelerationStructure();

    AccelerationStructureBuffer scratchBuffer(sizeInfo.buildScratchSize);
    VkDeviceAddress scratchAddress = GetRenderDevice()->GetBufferDeviceAddress(scratchBuffer.buffer());

    geometryBuildInfo.scratchData.deviceAddress = scratchAddress;

    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> vpBuildOffsets;
    vpBuildOffsets.reserve(vRangeInfo.size());
    for (const auto& rangeInfo : vRangeInfo)
    {
        vpBuildOffsets.push_back(&rangeInfo);
    }

    GetRenderDevice()->ExecuteImmediateCommand([&](VkCommandBuffer cmdBuf)
                                               {
                                                   SCOPED_MARKER(cmdBuf, "[RT] Build Acceleration Struct for " + geometryNode.GetName());
                                                   VkExt::vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &geometryBuildInfo, vpBuildOffsets.data());

                                                   VkMemoryBarrier barrier = {};
                                                   barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                                                   barrier.srcAccessMask =
                                                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                                                   barrier.dstAccessMask =
                                                       VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                                                   vkCmdPipelineBarrier(
                                                       cmdBuf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
                                                       &barrier, 0, nullptr, 0, nullptr); });
    return pAccelerationStructure;
}

AccelerationStructure* RayTracingSceneManager::BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& vInstances)
{
    // upload instances to GPU
    static const std::string TLAS_BUFFER_NAME = "AS instance buffer";
    uint32_t nBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * vInstances.size();
    AccelerationStructureBuffer* instanceBuffer = GetRenderResourceManager()->GetAccelerationStructureBuffer(TLAS_BUFFER_NAME, vInstances.data(), nBufferSize);
    VkDeviceAddress instanceAddress = GetRenderDevice()->GetBufferDeviceAddress(instanceBuffer->buffer());

    VkAccelerationStructureGeometryInstancesDataKHR instancesDataVk = {};
    instancesDataVk.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesDataVk.arrayOfPointers = VK_FALSE;
    instancesDataVk.data.deviceAddress = instanceAddress;

    VkAccelerationStructureGeometryKHR topASGeometry = {};
    topASGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    topASGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    topASGeometry.geometry.instances = instancesDataVk;

    // Query size for TLAS
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &topASGeometry;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;

    uint32_t nCount = (uint32_t)vInstances.size();
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    VkExt::vkGetAccelerationStructureBuildSizesKHR(GetRenderDevice()->GetDevice(),
                                                   VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &nCount, &sizeInfo);

    static const std::string TLAS_NAME = "TLAS";
    AccelerationStructure* pTLAS = GetRenderResourceManager()->CreateTLAS(TLAS_NAME, sizeInfo.accelerationStructureSize);

    // Allocate scratch size to build the structure
    AccelerationStructureBuffer scratchBuffer(sizeInfo.buildScratchSize);
    VkDeviceAddress scratchAddress = GetRenderDevice()->GetBufferDeviceAddress(scratchBuffer.buffer());
    buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = pTLAS->GetAccelerationStructure();
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build Offsets info: n instances
    VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{static_cast<uint32_t>(vInstances.size()), 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

    // Build the TLAS
    GetRenderDevice()->ExecuteImmediateCommand([&](VkCommandBuffer cmdBuf)
                                               {
        SCOPED_MARKER(cmdBuf, "Buld TLAS");
        VkExt::vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, &pBuildOffsetInfo); });

    return pTLAS;
}

uint32_t RayTracingSceneManager::AlignUp(uint32_t nSize, uint32_t nAlignment)
{
    return (nSize + nAlignment - 1) / nAlignment * nAlignment;
}

void RayTracingSceneManager::BuildScene(const std::vector<const SceneNode*>& vpGeometries)
{
    std::vector<VkAccelerationStructureInstanceKHR> vInstances;
    vInstances.reserve(vpGeometries.size());
    for (const SceneNode* pSceneNode : vpGeometries)
    {
        AccelerationStructure* pAcc = BuildBLASfromNode(*pSceneNode, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);

        // Create intance refer to this acceleration structure
        VkAccelerationStructureInstanceKHR instance = {};
        memcpy(&instance.transform, glm::value_ptr(glm::transpose(static_cast<const GeometrySceneNode*>(pSceneNode)->GetGeometry()->GetWorldMatrix())), sizeof(instance.transform));
        instance.instanceCustomIndex = m_mSubmeshBeginIndices.at(pSceneNode);
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.accelerationStructureReference = pAcc->GetAccelerationStructureAddress();

        vInstances.push_back(instance);
    }

    BuildTLAS(vInstances);

    // Allocate submesh description buffer
    GetRenderResourceManager()->GetStorageBuffer("submesh descs", m_vSubmeshDescs);
}

}  // namespace Muyo
