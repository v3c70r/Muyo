#include "RayTracingSceneManager.h"
#include "Scene.h"
#include "Geometry.h"
#include "DescriptorManager.h"
#include "VkExtFuncsLoader.h"
#include "PipelineStateBuilder.h"

#include <glm/gtc/type_ptr.hpp>
void RayTracingSceneManager::BuildRayTracingPipeline()
{
    // shader stages
    VkShaderModule rayGenShdr = CreateShaderModule(ReadSpv("shaders/pathTracing.rgen.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(rayGenShdr), VK_OBJECT_TYPE_SHADER_MODULE, "pathTracing.rgen.spv");

    VkShaderModule missShdr = CreateShaderModule(ReadSpv("shaders/pathTracing.rmiss.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(missShdr), VK_OBJECT_TYPE_SHADER_MODULE, "pathTracing.rmiss.spv");

    //VkShaderModule missShadowShdr = CreateShaderModule(ReadSpv("shaders/raytraceShadow.rmiss.spv"));
    //setDebugUtilsObjectName(reinterpret_cast<uint64_t>(missShadowShdr), VK_OBJECT_TYPE_SHADER_MODULE, "raytraceShadow.rmiss.spv");

    VkShaderModule chitShdr = CreateShaderModule(ReadSpv("shaders/pathTracing.rchit.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(chitShdr), VK_OBJECT_TYPE_SHADER_MODULE, "pathTracing.rchit.spv");

    RayTracingPipelineBuilder builder;
    builder.AddShaderModule(rayGenShdr, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .AddShaderModule(missShdr, VK_SHADER_STAGE_MISS_BIT_KHR)
        //.AddShaderModule(missShadowShdr, VK_SHADER_STAGE_MISS_BIT_KHR)
        .AddShaderModule(chitShdr, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    // pipeline layout
    std::vector<VkPushConstantRange> pushConstants = {};
    // GetPushConstantRange<RTLightingPushConstantBlock>((VkShaderStageFlagBits)(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR))};

    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_RAY_TRACING),
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_IBL),
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_LIGHT_DATA)};

    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Ray Tracing");
    builder.SetPipelineLayout(m_pipelineLayout).SetMaxRecursionDepth(2);
    VkRayTracingPipelineCreateInfoKHR createInfo = builder.Build();

    VK_ASSERT(VkExt::vkCreateRayTracingPipelinesKHR(GetRenderDevice()->GetDevice(),
                                                    VK_NULL_HANDLE,
                                                    VK_NULL_HANDLE,
                                                    1,
                                                    &createInfo,
                                                    nullptr,
                                                    &m_pipeline));

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), rayGenShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), missShdr, nullptr);
    //vkDestroyShaderModule(GetRenderDevice()->GetDevice(), missShadowShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), chitShdr, nullptr);
}

AccelerationStructure* RayTracingSceneManager::BuildBLASfromNode(const SceneNode& sceneNode, VkAccelerationStructureCreateFlagsKHR flags)
{
    const GeometrySceneNode& geometryNode = dynamic_cast<const GeometrySceneNode&>(sceneNode);

    m_mPrimitiveDescStartingIndices[&sceneNode] = static_cast<uint32_t>(m_vPrimitiveDescs.size());

    std::vector<VkAccelerationStructureGeometryKHR> vGeometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> vRangeInfo;

    for (const auto& primitive : geometryNode.GetGeometry()->getPrimitives())
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
        vGeometries.emplace_back(geometry);
        vRangeInfo.emplace_back(range);

        const Material* pMaterial = primitive->GetMaterial();
        VkDeviceAddress pbrFactorAdd = pMaterial->GetPBRFactorsDeviceAdd();
        PrimitiveDescription primDesc = {triangles.vertexData.deviceAddress, triangles.indexData.deviceAddress, pbrFactorAdd, {}};
        pMaterial->FillPbrTextureIndices(primDesc.m_aPbrTextureIndices);
        m_vPrimitiveDescs.emplace_back(primDesc);
    }

    // allocate memory for BLAS

    const std::string sAccStructName = "acStruct_" + geometryNode.GetName();

    std::vector<uint32_t> vPrimCounts;
    vPrimCounts.reserve(vRangeInfo.size());
    for (const auto &range : vRangeInfo)
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

    AccelerationStructure *pAccelerationStructure = GetRenderResourceManager()->CreateBLAS(sAccStructName, sizeInfo.accelerationStructureSize);

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
                                                       &barrier, 0, nullptr, 0, nullptr);
                                               });
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
    buildInfo.srcAccelerationStructure =  VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = pTLAS->GetAccelerationStructure();
    buildInfo.scratchData.deviceAddress = scratchAddress;

    // Build Offsets info: n instances
    VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{static_cast<uint32_t>(vInstances.size()), 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

    // Build the TLAS
    GetRenderDevice()->ExecuteImmediateCommand([&](VkCommandBuffer cmdBuf) {
        SCOPED_MARKER(cmdBuf, "Buld TLAS");
        VkExt::vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, &pBuildOffsetInfo);
    });

    return pTLAS;
}


uint32_t RayTracingSceneManager::AlignUp(uint32_t nSize, uint32_t nAlignment)
{
    return (nSize + nAlignment - 1) / nAlignment * nAlignment;
}

void RayTracingSceneManager::AllocateShaderBindingTable()
{
    // https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways
    // https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/#shaderbindingtable

    uint32_t nMissGroupCount = 1;
    uint32_t nHitGroupCount = 1;
    uint32_t nRayGenGroupCount = 1;      // can have only one

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracingProperties = {};
    GetRenderDevice()->GetPhysicalDeviceProperties(raytracingProperties);

    uint32_t nHandleSize = raytracingProperties.shaderGroupHandleSize;
    uint32_t nHandleSizeAligned = AlignUp(nHandleSize, raytracingProperties.shaderGroupHandleAlignment);


    // ray gen shader takes a shader group base alignment
    // There should be just 1 ray gen shader
    VkStridedDeviceAddressRegionKHR& rgenRegion = m_aSBTRegions[SBT_REGION_RAY_GEN];
    rgenRegion.stride = AlignUp(nHandleSizeAligned, raytracingProperties.shaderGroupBaseAlignment);
    rgenRegion.size = rgenRegion.stride;

    VkStridedDeviceAddressRegionKHR& hitRegion = m_aSBTRegions[SBT_REGION_RAY_HIT];
    hitRegion.stride = nHandleSizeAligned;
    hitRegion.size = AlignUp(nHitGroupCount * nHandleSizeAligned, raytracingProperties.shaderGroupBaseAlignment);

    VkStridedDeviceAddressRegionKHR& missRegion = m_aSBTRegions[SBT_REGION_RAY_MISS];
    missRegion.stride = nHandleSizeAligned;
    missRegion.size = AlignUp(nMissGroupCount * nHandleSizeAligned, raytracingProperties.shaderGroupBaseAlignment);

    uint32_t sbtSize = rgenRegion.size + hitRegion.size + missRegion.size;


    uint32_t nHandleCount = nRayGenGroupCount + nMissGroupCount + nHitGroupCount;
    uint32_t nDataSize = nHandleCount * nHandleSize;
    std::vector<uint8_t> handleBuffer(nDataSize);
    (VkExt::vkGetRayTracingShaderGroupHandlesKHR(GetRenderDevice()->GetDevice(), m_pipeline, 0, nHandleCount, nDataSize, handleBuffer.data()));
    ShaderBindingTableBuffer *pSBTBuffer = GetRenderResourceManager()->GetShaderBindingTableBuffer("SBT", sbtSize);

    // Copy handles to GPU
    VkDeviceAddress SBTAddress = GetRenderDevice()->GetBufferDeviceAddress(pSBTBuffer->buffer());
    rgenRegion.deviceAddress = SBTAddress;
    missRegion.deviceAddress = SBTAddress + rgenRegion.size;
    hitRegion.deviceAddress = missRegion.deviceAddress + missRegion.size;

    uint8_t* pRayGenHandleGPU = (uint8_t*)pSBTBuffer->Map();
    uint32_t nHandleOffset = 0;

    // copy ray gen at begining of the handle data
    memcpy(pRayGenHandleGPU, handleBuffer.data(), nHandleSize);


    // copy miss
    nHandleOffset += nHandleSize;
    uint8_t* pRayMissHandleGPU = pRayGenHandleGPU + rgenRegion.size;
    for (uint32_t i = 0; i < nMissGroupCount; i++)
    {
        memcpy(pRayMissHandleGPU, &(handleBuffer[nHandleOffset]), nHandleSize);
        nHandleOffset += nHandleSize;
        pRayMissHandleGPU += missRegion.stride;
    }

    // Copy Hit
    uint8_t* pRayHitHandleGPU = pRayGenHandleGPU + rgenRegion.size + missRegion.size;
    for (uint32_t i = 0; i < nHitGroupCount; i++)
    {
        memcpy(pRayHitHandleGPU, &(handleBuffer[nHandleOffset]), nHandleSize);
        nHandleOffset += nHandleSize;
        pRayHitHandleGPU += hitRegion.stride;
    }

    pSBTBuffer->Unmap();
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
        instance.instanceCustomIndex = m_mPrimitiveDescStartingIndices.at(pSceneNode);
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.accelerationStructureReference = pAcc->GetAccelerationStructureAddress();

        vInstances.push_back(instance);
    }

    BuildTLAS(vInstances);

    // Allocate Ray Tracing descriptor layouts
    GetDescriptorManager()->CreateRayTracingDescriptorLayout(GetTextureManager()->GetTextures().size());
    // Allocate primitive description buffer
    GetRenderResourceManager()->GetStorageBuffer("primitive descs", m_vPrimitiveDescs);
}

void RayTracingSceneManager::DestroyPipeline()
{
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
}
