#include "RayTracingUtils.h"

#include <cassert>
#include <functional>
#include <algorithm>
#include <string>
#include <vulkan/vulkan_core.h>
#include <glm/gtc/type_ptr.hpp>

#include "Geometry.h"
#include "MeshVertex.h"
#include "Scene.h"
#include "VkRenderDevice.h"
#include "RenderResourceManager.h"
#include "Debug.h"
#include "PipelineStateBuilder.h"
#include "PushConstantBlocks.h"
#include "DescriptorManager.h"
#include "ResourceBarrier.h"
#include "glm/matrix.hpp"

int32_t AlignUp(uint32_t nSize, uint32_t nAlignment)
{
    return (nSize + nAlignment - 1) / nAlignment * nAlignment;
}

RTBuilder::RTBuilder()
{

#define GET_VK_INSTANCE_FUNC(FUNC_NAME) \
FUNC_NAME = (PFN_##FUNC_NAME)vkGetInstanceProcAddr(GetRenderDevice()->GetInstance(), #FUNC_NAME);

    // Get function pointers from Vulkan library
    vkCreateAccelerationStructureKHR =
        (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(
            GetRenderDevice()->GetInstance(),
            "vkCreateAccelerationStructureKHR");

    vkDestroyAccelerationStructureKHR =
        (PFN_vkDestroyAccelerationStructureKHR)vkGetInstanceProcAddr(
            GetRenderDevice()->GetInstance(),
            "vkDestroyAccelerationStructureKHR");

    vkCmdBuildAccelerationStructuresKHR =
        (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(
            GetRenderDevice()->GetInstance(),
            "vkCmdBuildAccelerationStructuresKHR");

    vkCmdWriteAccelerationStructuresPropertiesKHR =
        (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR)
            vkGetInstanceProcAddr(
                GetRenderDevice()->GetInstance(),
                "vkCmdWriteAccelerationStructuresPropertiesKHR");

    vkGetAccelerationStructureBuildSizesKHR =
        (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(
            GetRenderDevice()->GetInstance(),
            "vkGetAccelerationStructureBuildSizesKHR");

    vkCmdCopyAccelerationStructureKHR =
        (PFN_vkCmdCopyAccelerationStructureKHR)vkGetInstanceProcAddr(
            GetRenderDevice()->GetInstance(),
            "vkCmdCopyAccelerationStructureKHR");

    vkGetAccelerationStructureDeviceAddressKHR =
        (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetInstanceProcAddr(
            GetRenderDevice()->GetInstance(),
            "vkGetAccelerationStructureDeviceAddressKHR");

    vkCreateRayTracingPipelinesKHR =
        (PFN_vkCreateRayTracingPipelinesKHR)vkGetInstanceProcAddr (
            GetRenderDevice()->GetInstance(),
            "vkCreateRayTracingPipelinesKHR");
    assert(vkCreateRayTracingPipelinesKHR != nullptr);


    GET_VK_INSTANCE_FUNC(vkGetRayTracingShaderGroupHandlesKHR);
    GET_VK_INSTANCE_FUNC(vkCmdTraceRaysKHR);
    GET_VK_INSTANCE_FUNC(vkCmdPipelineBarrier2KHR);
}

RTInputs ConstructRTInputsFromDrawLists(const DrawLists& dls)
{
    std::vector<BLASInput> BLASs;
    std::vector<Instance> vInstances;
    std::vector<PrimitiveDescription> vPrimitiveDescs;
    // Just gather opaque nodes for now
    for (const SceneNode* pSceneNode : dls.m_aDrawLists[DrawLists::DL_OPAQUE])
    {

        const GeometrySceneNode *pGeometryNode = dynamic_cast<const GeometrySceneNode *>(pSceneNode);
        if (pGeometryNode)
        {
            BLASInput blasInput;
            for (const auto &primitive : pGeometryNode->GetGeometry()->getPrimitives())
            {
                VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
                triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
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

                const Material* pMaterial = primitive->GetMaterial();
                VkDeviceAddress pbrFactorAdd = pMaterial->GetPBRFactorsDeviceAdd();
				PrimitiveDescription primDesc = { triangles.vertexData.deviceAddress, triangles.indexData.deviceAddress, pbrFactorAdd};
                pMaterial->FillPbrTextureIndices(primDesc.m_aPbrTextureIndices);
                vPrimitiveDescs.emplace_back(primDesc);
            }

            if (blasInput.m_vGeometries.size() > 0)
            {

                BLASs.push_back(blasInput);
                Instance instance;

                instance.transform = pGeometryNode->GetGeometry()->GetWorldMatrix();  // Position of the instance
				instance.instanceCustomId = vPrimitiveDescs.size() - blasInput.m_vGeometries.size();     // use custom index to address begining of primitive description
                instance.blasId = BLASs.size() -  1;
                instance.hitGroupId = 0;  // We will use the same hit group for all objects
                instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                vInstances.push_back(instance);
            }
        }
    }

    return {BLASs, vInstances, vPrimitiveDescs};
}

void RTBuilder::BuildBLAS(const std::vector<BLASInput> &vBLASInputs, VkBuildAccelerationStructureFlagsKHR flags)
{
    m_blas = vBLASInputs;
    uint32_t nNumBlas = (uint32_t)m_blas.size();
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos(nNumBlas);
    std::vector<VkDeviceSize> vOriginalSizes(nNumBlas);
    for (size_t idx = 0; idx < nNumBlas; idx++)
    {
        buildInfos[idx].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfos[idx].flags = flags;
        buildInfos[idx].geometryCount = (uint32_t)m_blas[idx].m_vRangeInfo.size();
        buildInfos[idx].pGeometries = m_blas[idx].m_vGeometries.data();
        buildInfos[idx].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfos[idx].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfos[idx].srcAccelerationStructure = VK_NULL_HANDLE;
    }
    VkDeviceSize nMaxScratchSize = 0;
    for (size_t idx = 0; idx < nNumBlas; idx++)
    {
        // Query both the size of the finished acceleration structure and the  amount of scratch memory
        // needed (both written to sizeInfo). The `vkGetAccelerationStructureBuildSizesKHR` function
        // computes the worst case memory requirements based on the user-reported max number of
        // primitives. Later, compaction can fix this potential inefficiency.
        const std::string sAccBufName = "acBuf_" + std::to_string(idx);
        const std::string sAccStructName = "acStruct_" + std::to_string(idx);
        std::vector<uint32_t> maxPrimCount(m_blas[idx].m_vRangeInfo.size());
        for (size_t tt = 0; tt < m_blas[idx].m_vRangeInfo.size(); tt++)
        {
            maxPrimCount[tt] = m_blas[idx].m_vRangeInfo[tt].primitiveCount;  // Number of primitives/triangles
        }
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(
            GetRenderDevice()->GetDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfos[idx],
            maxPrimCount.data(), &sizeInfo);

        // Create Acceleration strucutre
        VkAccelerationStructureCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        createInfo.size = sizeInfo.accelerationStructureSize;
        // Allocate acceleration structure buffer 
        AccelerationStructureBuffer* bufAcc = GetRenderResourceManager()->GetAccelerationStructureBuffer(sAccBufName, createInfo.size);
        createInfo.buffer = bufAcc->buffer();

        vkCreateAccelerationStructureKHR(GetRenderDevice()->GetDevice(),
                                         &createInfo, nullptr,
                                         &m_blas[idx].m_ac);
        // Assign debug name to the acceleration structure
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_blas[idx].m_ac), VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, sAccStructName.c_str());
        buildInfos[idx].dstAccelerationStructure = m_blas[idx].m_ac;

        m_blas[idx].m_flags = flags;
        nMaxScratchSize = std::max(nMaxScratchSize, sizeInfo.buildScratchSize);
        vOriginalSizes[idx] = sizeInfo.accelerationStructureSize;
    }

    // Allocate scratch size to build the structure
    AccelerationStructureBuffer scratchBuffer(nMaxScratchSize);
    VkDeviceAddress scratchAddress = GetRenderDevice()->GetBufferDeviceAddress(scratchBuffer.buffer());

    // Is compaction requested?
    bool bDoCompaction = (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
                        == VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

    // Allocate a query pool for storing the needed size for every BLAS compaction.
    VkQueryPoolCreateInfo qpci = {};
    qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryCount = nNumBlas;
    qpci.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    VkQueryPool queryPool;
    vkCreateQueryPool(GetRenderDevice()->GetDevice(), &qpci, nullptr, &queryPool);

    std::vector<VkCommandBuffer> cmdBuffers;
    for (size_t idx = 0; idx < nNumBlas; idx++)
    {
        const BLASInput &blas = m_blas[idx];
        VkCommandBuffer cmdBuf = GetRenderDevice()->AllocateImmediateCommandBuffer();

        VkCommandBufferBeginInfo cmdBeginInfo = {};
        cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        cmdBeginInfo.pInheritanceInfo = nullptr;
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(cmdBuf), VK_OBJECT_TYPE_COMMAND_BUFFER, (std::string("[CB] build blas " + std::to_string(idx)).c_str()));

        buildInfos[idx].scratchData.deviceAddress = scratchAddress;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> vpBuildOffsets(blas.m_vRangeInfo.size());
        for (size_t infoIdx = 0; infoIdx < vpBuildOffsets.size(); infoIdx++)
        {
            vpBuildOffsets[infoIdx] = &blas.m_vRangeInfo[infoIdx];
        }

        vkBeginCommandBuffer(cmdBuf, &cmdBeginInfo);
        {
            SCOPED_MARKER(cmdBuf, "[RT] Build Acc Struct");
            vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfos[idx],
                                                vpBuildOffsets.data());

            VkMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask =
                VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            barrier.dstAccessMask =
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            vkCmdPipelineBarrier(
                cmdBuf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
                &barrier, 0, nullptr, 0, nullptr);

            // Write compacted size to query number idx.
            if (bDoCompaction)
            {
                // Reset the query before using it
                vkCmdResetQueryPool(cmdBuf, queryPool, idx, 1);
                // Write the query to idx query in the pool
                vkCmdWriteAccelerationStructuresPropertiesKHR(
                    cmdBuf, 1, &blas.m_ac,
                    VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                    queryPool, idx);
            }
        }
        vkEndCommandBuffer(cmdBuf);
        cmdBuffers.push_back(cmdBuf);
    }

    GetRenderDevice()->SubmitCommandBuffersAndWait(cmdBuffers);
    cmdBuffers.clear();

    if (bDoCompaction)
    {
        std::vector<VkDeviceSize> vCompactSizes(nNumBlas);
        vkGetQueryPoolResults(GetRenderDevice()->GetDevice(), queryPool, 0,
                              (uint32_t)vCompactSizes.size(),
                              vCompactSizes.size() * sizeof(VkDeviceSize),
                              vCompactSizes.data(), sizeof(VkDeviceSize),
                              VK_QUERY_RESULT_WAIT_BIT);

        uint32_t nTotalOriginalSizes = 0;
        uint32_t nTotalCompactSizes = 0;
        std::vector<VkAccelerationStructureKHR> vAcToSwap;
        std::vector<AccelerationStructureBuffer*> vBufToSwap;
        for (uint32_t idx = 0; idx < nNumBlas; idx++)
        {

            nTotalOriginalSizes += vOriginalSizes[idx];
            nTotalCompactSizes += vCompactSizes[idx];

            AccelerationStructureBuffer* pBuf = new AccelerationStructureBuffer(vCompactSizes[idx]);
            VkAccelerationStructureKHR acc = VK_NULL_HANDLE;

            // Create compact acceleration structure
            VkAccelerationStructureCreateInfoKHR createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            createInfo.size = vCompactSizes[idx];
            createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            createInfo.buffer = pBuf->buffer();
            vkCreateAccelerationStructureKHR(GetRenderDevice()->GetDevice(),
                                             &createInfo, nullptr,
                                             &acc);

            VkCommandBuffer cmdBuf = GetRenderDevice()->AllocateImmediateCommandBuffer();
            cmdBuffers.push_back(cmdBuf);
            VkCommandBufferBeginInfo cmdBeginInfo = {};
            cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            cmdBeginInfo.pInheritanceInfo = nullptr;
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(cmdBuf), VK_OBJECT_TYPE_COMMAND_BUFFER, (std::string("[CB] BLAS Compaction " + std::to_string(idx)).c_str()));
            vkBeginCommandBuffer(cmdBuf, &cmdBeginInfo);
            {
                // Copy old acc structure to new acc structure with compaction
                SCOPED_MARKER(cmdBuf, "Acceleration structure compaction" + std::to_string(idx));
                VkCopyAccelerationStructureInfoKHR copyInfo = {};
                copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
                copyInfo.src = m_blas[idx].m_ac;
                copyInfo.dst = acc;
                copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                vkCmdCopyAccelerationStructureKHR(cmdBuf, &copyInfo);
            }
            vkEndCommandBuffer(cmdBuf);

            // Prepare resources to swap
            vAcToSwap.push_back(acc);
            vBufToSwap.push_back(pBuf);
        }
        GetRenderDevice()->SubmitCommandBuffersAndWait(cmdBuffers);
        cmdBuffers.clear();

        // Swap old blas structure with compacted blas structure

        for (uint32_t idx = 0; idx < nNumBlas; idx++)
        {
            
            vkDestroyAccelerationStructureKHR(GetRenderDevice()->GetDevice(), m_blas[idx].m_ac, nullptr);
            // Make sure they are the same names used above because we want to swap exactly the same buffer
            const std::string sAccBufName = "acBuf_" + std::to_string(idx);

            const std::string sAccStructName = "acStruct_" + std::to_string(idx)+"_compacted";
            m_blas[idx].m_ac = vAcToSwap[idx];
            GetRenderResourceManager()->AssignResource(sAccBufName, vBufToSwap[idx]);
            setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_blas[idx].m_ac), VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, sAccStructName.c_str());
        }
    }
    vkDestroyQueryPool(GetRenderDevice()->GetDevice(), queryPool, nullptr);

}


void RTBuilder::BuildTLAS(const std::vector<Instance> &instances, VkBuildAccelerationStructureFlagsKHR flags, bool bUpdate)
{
    static const std::string TLAS_BUFFER_NAME = "TLAS instance buffer";

    assert(m_tlas.m_ac == VK_NULL_HANDLE || bUpdate);
    m_tlas.m_flags = flags;
    std::vector<VkAccelerationStructureInstanceKHR> geometryInstances;
    geometryInstances.reserve(instances.size());
    for(const auto& instance : instances)
    {
        geometryInstances.push_back(TLASInputToVkGeometryInstance(instance));
    }
    if (bUpdate)
    {
        GetRenderResourceManager()->removeResource(TLAS_BUFFER_NAME);
    }
    uint32_t nBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * geometryInstances.size();
    AccelerationStructureBuffer* instanceBuffer = GetRenderResourceManager()->GetAccelerationStructureBuffer(TLAS_BUFFER_NAME, geometryInstances.data(), nBufferSize);
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
    buildInfo.mode = bUpdate
                         ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                         : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;

    uint32_t nCount = (uint32_t) instances.size();
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(GetRenderDevice()->GetDevice(),
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &nCount, &sizeInfo);

    if (!bUpdate)
    {
        VkAccelerationStructureCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        createInfo.buffer = GetRenderResourceManager()->GetAccelerationStructureBuffer("TLAS buffer", sizeInfo.accelerationStructureSize)->buffer();
        vkCreateAccelerationStructureKHR(GetRenderDevice()->GetDevice(),
                                         &createInfo, nullptr,
                                         &m_tlas.m_ac);
    }

    // Allocate scratch size to build the structure
    AccelerationStructureBuffer scratchBuffer(sizeInfo.buildScratchSize);
    VkDeviceAddress scratchAddress = GetRenderDevice()->GetBufferDeviceAddress(scratchBuffer.buffer());
    buildInfo.srcAccelerationStructure =  bUpdate ? m_tlas.m_ac : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = m_tlas.m_ac;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build Offsets info: n instances
    VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{static_cast<uint32_t>(instances.size()), 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

    // Build the TLAS
    GetRenderDevice()->ExecuteImmediateCommand([&](VkCommandBuffer cmdBuf) {
        SCOPED_MARKER(cmdBuf, "Buld TLAS");
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, &pBuildOffsetInfo);
    });
}

void RTBuilder::Cleanup()
{
    for (auto& blasInput : m_blas)
    {
        vkDestroyAccelerationStructureKHR(GetRenderDevice()->GetDevice(), blasInput.m_ac, nullptr);
    }
    vkDestroyAccelerationStructureKHR(GetRenderDevice()->GetDevice(), m_tlas.m_ac, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
}

VkAccelerationStructureInstanceKHR RTBuilder::TLASInputToVkGeometryInstance(const Instance& instance)
{
    assert(size_t(instance.blasId) < m_blas.size());

    BLASInput& blas{m_blas[instance.blasId]};

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        nullptr,
        blas.m_ac};
    VkDeviceAddress blasAddress = vkGetAccelerationStructureDeviceAddressKHR(GetRenderDevice()->GetDevice(), &addressInfo);

    VkAccelerationStructureInstanceKHR gInst{};
    // The matrices for the instance transforms are row-major, instead of
    // column-major in the rest of the application
    // The gInst.transform value only contains 12 values, corresponding to a 4x3
    // matrix, hence saving the last row that is anyway always (0,0,0,1). Since
    // the matrix is row-major, we simply copy the first 12 values of the
    // original 4x4 matrix
    memcpy(&gInst.transform, glm::value_ptr(glm::transpose(instance.transform)), sizeof(gInst.transform));
    gInst.instanceCustomIndex = instance.instanceCustomId;
    gInst.mask = instance.mask;
    gInst.instanceShaderBindingTableRecordOffset = instance.hitGroupId;
    gInst.flags = instance.flags;
    gInst.accelerationStructureReference = blasAddress;
    return gInst;
}

void RTBuilder::BuildRTPipeline()
{
    // shader stages
    VkShaderModule rayGenShdr = CreateShaderModule(ReadSpv("shaders/raytrace.rgen.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(rayGenShdr), VK_OBJECT_TYPE_SHADER_MODULE, "raytrace.rgen.spv");

    VkShaderModule missShdr = CreateShaderModule(ReadSpv("shaders/raytrace.rmiss.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(missShdr), VK_OBJECT_TYPE_SHADER_MODULE, "raytrace.rmiss.spv");

    VkShaderModule chitShdr = CreateShaderModule(ReadSpv("shaders/raytrace.rchit.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(chitShdr), VK_OBJECT_TYPE_SHADER_MODULE, "raytrace.rchit.spv");


    

    RayTracingPipelineBuilder builder;
    builder.AddShaderModule(rayGenShdr, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
    .AddShaderModule(missShdr, VK_SHADER_STAGE_MISS_BIT_KHR)
    .AddShaderModule(chitShdr, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    // pipeline layout
    std::vector<VkPushConstantRange> pushConstants = {};
        //GetPushConstantRange<RTLightingPushConstantBlock>((VkShaderStageFlagBits)(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR))};

    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_RAY_TRACING)
    };

    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Ray Tracing");
    builder.SetPipelineLayout(m_pipelineLayout).SetMaxRecursionDepth(1);
    VkRayTracingPipelineCreateInfoKHR createInfo = builder.Build();

    assert(vkCreateRayTracingPipelinesKHR(GetRenderDevice()->GetDevice(),
                                          VK_NULL_HANDLE, 
                                          VK_NULL_HANDLE,
                                          1,
                                          &createInfo,
                                          nullptr,
                                          &m_pipeline) == VK_SUCCESS);

    m_vRTShaderGroupInfos = builder.GetShaderGroupInfos();

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), rayGenShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), missShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), chitShdr, nullptr);
}
void RTBuilder::BuildShaderBindingTable()
{
    GetRenderDevice()->GetPhysicalDeviceProperties(m_rtProperties);

    // Number of handles
    uint32_t nMissCount = 1;
    uint32_t nHitCount = 1;
    uint32_t nHandleCount = 1 + nMissCount + nHitCount;
    uint32_t nHandleSize = m_rtProperties.shaderGroupHandleSize;
    uint32_t nHandleSizeAligned = AlignUp(nHandleSize, m_rtProperties.shaderGroupHandleAlignment);


    // Different shader regions
    m_rgenRegion.stride = AlignUp(nHandleSizeAligned, m_rtProperties.shaderGroupBaseAlignment);
    m_rgenRegion.size = m_rgenRegion.stride;

    m_missRegion.stride = nHandleSizeAligned;
    m_missRegion.size = AlignUp(nMissCount * nHandleSizeAligned, m_rtProperties.shaderGroupBaseAlignment);

    m_hitRegion.stride = nHandleSizeAligned;
    m_hitRegion.size = AlignUp(nHitCount * nHandleSizeAligned, m_rtProperties.shaderGroupBaseAlignment);

    // Fectch handles to the shader groups
    uint32_t nDataSize = nHandleCount * nHandleSize;
    std::vector<uint8_t> handles(nDataSize);
    assert(vkGetRayTracingShaderGroupHandlesKHR(GetRenderDevice()->GetDevice(), m_pipeline, 0, nHandleCount, nDataSize, handles.data()) == VK_SUCCESS);

    // Allocate buffer to store SBT
    m_pSBTBuffer = GetRenderResourceManager()->GetShaderBindingTableBuffer("SBT", handles.data(), m_rgenRegion.size + m_missRegion.size + m_hitRegion.size);

    // Copy handles to GPU
    VkDeviceAddress SBTAddress = GetRenderDevice()->GetBufferDeviceAddress(m_pSBTBuffer->buffer());
    m_rgenRegion.deviceAddress = SBTAddress;
    m_missRegion.deviceAddress = SBTAddress + m_rgenRegion.size;
    m_hitRegion.deviceAddress = m_missRegion.deviceAddress + m_missRegion.size;

    // Get handle data on CPU
    auto GetHandle = [&](uint32_t i) { return handles.data() + i * nHandleSize; };

    void* pHandlesGPU = m_pSBTBuffer->Map();
    uint8_t* pData = nullptr;
    int nHandleIndex = 0;

    // Copy Ray Gen
    pData = (uint8_t*)pHandlesGPU;
    memcpy(pData, GetHandle(nHandleIndex++), nHandleSize);

    // Copy Miss
    pData = (uint8_t*)pHandlesGPU + m_rgenRegion.size;
    for (uint32_t i = 0 ; i < nMissCount; i++)
    {
        memcpy(pData, GetHandle(nHandleIndex++), nHandleSize);
        pData += m_missRegion.stride;
    }

    // Copy hit
    pData = (uint8_t*)pHandlesGPU + m_rgenRegion.size + m_missRegion.size;
    for (uint32_t i = 0 ; i < nHitCount; i++)
    {
        memcpy(pData, GetHandle(nHandleIndex++), nHandleSize);
        pData += m_hitRegion.stride;
    }
    m_pSBTBuffer->Unmap();
}

void RTBuilder::RayTrace(VkExtent2D imgSize)
{

    VkExtent2D ext {0, 0};

    const auto* pStorageImageRes = GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Output", ext, VK_FORMAT_R16G16B16A16_SFLOAT);
    const UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");
    StorageBuffer<PrimitiveDescription>* primDescBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<PrimitiveDescription>>("primitive descs");
    std::vector<VkDescriptorSet> vDescSets =
        {
            GetDescriptorManager()->AllocatePerviewDataDescriptorSet(*perView),
            GetDescriptorManager()->AllocateRayTracingDescriptorSet(m_tlas.m_ac, pStorageImageRes->getView(), *primDescBuffer, GetTextureManager()->GetTextures())};

    m_cmdBuf = GetRenderDevice()->AllocateReusablePrimaryCommandbuffer();

    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;
        vkBeginCommandBuffer(m_cmdBuf, &beginInfo);
        {
            // Transit output image to general layout using Image memory barrier 2

            VkImage outputImage = pStorageImageRes->getImage();
#ifdef FEATURE_SYNCHRONIZATION2
            VkImageMemoryBarrier2KHR imgBarrier = {
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,  //VkStructureType             sType;
                nullptr,                                       //const void*                 pNext;
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,             //VkPipelineStageFlags2KHR    srcStageMask;
                0,                                             //VkAccessFlags2KHR           srcAccessMask;
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,  //VkPipelineStageFlags2KHR    dstStageMask;
                0,                                             //VkAccessFlags2KHR           dstAccessMask;
                VK_IMAGE_LAYOUT_UNDEFINED,                     //VkImageLayout               oldLayout;
                VK_IMAGE_LAYOUT_GENERAL,                       //VkImageLayout               newLayout;
                VK_QUEUE_FAMILY_IGNORED,                       //uint32_t                    srcQueueFamilyIndex;
                VK_QUEUE_FAMILY_IGNORED,                       //uint32_t                    dstQueueFamilyIndex;
                outputImage,                                   //VkImage                     image;
                {
                    //VkImageSubresourceRange     subresourceRange;
                    VK_IMAGE_ASPECT_COLOR_BIT,  //VkImageAspectFlags    aspectMask;
                    0,                          //uint32_t              baseMipLevel;
                    1,                          //uint32_t              levelCount;
                    0,                          //uint32_t              baseArrayLayer;
                    1,                          //uint32_t              layerCount;
                },
            };
            VkDependencyInfoKHR dependency =
                {
                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,  //sType;
                    nullptr,                                //pNext;
                    0,                                      //dependencyFlags;
                    0,                                      //memoryBarrierCount;
                    nullptr,                                //pMemoryBarriers;
                    0,                                      //bufferMemoryBarrierCount;
                    nullptr,                                //pBufferMemoryBarriers;
                    1,                                      //imageMemoryBarrierCount;
                    &imgBarrier                             //pImageMemoryBarriers;
                };
            vkCmdPipelineBarrier2KHR(m_cmdBuf, &dependency);
#else
            ImageResourceBarrier barrier(outputImage, VK_IMAGE_LAYOUT_GENERAL);
            GetRenderDevice()->AddResourceBarrier(m_cmdBuf, barrier);
#endif
        };

        SCOPED_MARKER(m_cmdBuf, "Trace Ray");
        vkCmdBindPipeline(m_cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
        vkCmdBindDescriptorSets(m_cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, (uint32_t)vDescSets.size(), vDescSets.data(), 0, nullptr);

        vkCmdTraceRaysKHR(
            m_cmdBuf,
            &m_rgenRegion,
            &m_missRegion,
            &m_hitRegion,
            &m_callRegion,
            imgSize.width, imgSize.height, 1);

        vkEndCommandBuffer(m_cmdBuf);
    }
}
