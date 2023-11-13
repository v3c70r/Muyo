#include "RenderPassGEMM.h"
#include "RenderResourceManager.h"
#include "PipelineStateBuilder.h"
#include "VkRenderDevice.h"
#include <vulkan/vulkan_core.h>

/*
cooperativeMatrixProps = 16x16x16   A = float16_t B = float16_t C = float32_t D = float32_t scope = subgroup
TILE_M=128 TILE_N=128, TILE_K=16 BColMajor=0   2.484938 TFlops
TILE_M=128 TILE_N=128, TILE_K=16 BColMajor=1   3.344990 TFlops
TILE_M=128 TILE_N=256, TILE_K=16 BColMajor=0   4.812828 TFlops
TILE_M=128 TILE_N=256, TILE_K=16 BColMajor=1   3.564880 TFlops
TILE_M=256 TILE_N=128, TILE_K=16 BColMajor=0   4.592566 TFlops
TILE_M=256 TILE_N=128, TILE_K=16 BColMajor=1   2.875581 TFlops
*/


#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

namespace Muyo
{
    void RenderPassGEMM::PrepareRenderPass()
    {


        // Allocate matrix buffers
        std::vector<uint32_t> data;
        uint32_t val = glm::packHalf2x16(glm::vec2(1.0, 1.0));
        uint32_t zeros = glm::packHalf2x16(glm::vec2(0.0, 0.0));

        data.resize(M * K / 2, val);

        const auto* matA = GetRenderResourceManager()->GetStorageBuffer<uint32_t>("MatA", data);

        data.resize(K * N / 2, val);
        const auto* matB = GetRenderResourceManager()->GetStorageBuffer<uint32_t>("MatB", data);

        data.resize(M * N / 2, zeros);

        const auto* matC = GetRenderResourceManager()->GetStorageBuffer<uint32_t>("MatC", data);
        const auto* matO = GetRenderResourceManager()->GetStorageBuffer<uint32_t>("MatO", data);

        MatrixParams matrixParams
        {
            GetRenderDevice()->GetBufferDeviceAddress(matA->buffer()),
            GetRenderDevice()->GetBufferDeviceAddress(matB->buffer()),
            GetRenderDevice()->GetBufferDeviceAddress(matC->buffer()),
            GetRenderDevice()->GetBufferDeviceAddress(matO->buffer()),
        };

        // Allocate uniform buffer
        auto* matAddrs = GetRenderResourceManager()->getUniformBuffer<MatrixParams>("matAddrs");
        matAddrs->SetData(matrixParams);
        m_renderPassParameters.AddParameter(matAddrs, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

        m_renderPassParameters.Finalize("GEMM");

        CreatePipeline();
    }

    void RenderPassGEMM::CreatePipeline()
    {
        VkShaderModule gemmShader = CreateShaderModule(ReadSpv("shaders/shmem.comp.spv"));
        //VkShaderModule gemmShader = CreateShaderModule(ReadSpv("shaders/shmemfp32.spv"));

        GetCoopMatProperties();
        VkCooperativeMatrixPropertiesNV* cooperativeMatrixProps = m_vCoopMatProperties.data();
        // Specialization info
        //
        //
        /*
         * -		[3]	{sType=VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_NV (1000249001) pNext=0x0000000000000000 MSize=...}	VkCooperativeMatrixPropertiesNV
		sType	VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_NV (1000249001)	VkStructureType
		pNext	0x0000000000000000	void *
		MSize	16	unsigned int
		NSize	16	unsigned int
		KSize	16	unsigned int
		AType	VK_COMPONENT_TYPE_FLOAT16_NV (0)	VkComponentTypeNV
		BType	VK_COMPONENT_TYPE_FLOAT16_NV (0)	VkComponentTypeNV
		CType	VK_COMPONENT_TYPE_FLOAT32_NV (1)	VkComponentTypeNV
		DType	VK_COMPONENT_TYPE_FLOAT32_NV (1)	VkComponentTypeNV
		scope	VK_SCOPE_SUBGROUP_NV (3)	VkScopeNV
        */
        TestCase testCase = {
            TT_SHARED, 
            VK_COMPONENT_TYPE_FLOAT16_NV, // VkComponentTypeNV inputType;
            VK_COMPONENT_TYPE_FLOAT32_NV, // VkComponentTypeNV outputType;

            // MxNxK is the size of the full matrix multiply
            256, // uint32_t M;
            1024, // uint32_t N;
            2048, // uint32_t K;

            // Each cooperative matrix multiply is lMxlNxlK
            16, // uint32_t lM;
            16, // uint32_t lN;
            16, // uint32_t lK;

            // size of workgroup tile in destination matrix
            16, // uint32_t TILE_M;
            16, // uint32_t TILE_N;
            16, // uint32_t TILE_K;

            false, // bool BColMajor;
        };
        float alpha = 1.0f;
        float beta = 0.0f;
        // Specialize the shader with the matrix sizes, strides, and constants.
        const uint32_t specData[] = {
            testCase.lM,
            testCase.lN,
            testCase.lK,
            testCase.TILE_M,
            testCase.TILE_N,
            testCase.TILE_K,
            testCase.K,
            testCase.K, // stride0
            testCase.BColMajor ? testCase.K : testCase.N, // stride1
            testCase.N, // stride2
            testCase.N, // stride3
            1,
            1,
            testCase.BColMajor,
            testCase.ARowLen,
            testCase.ANumRows,
            testCase.BRowLen,
            testCase.BNumRows,
        };

#if 0
        for (int i = 0; i < ARRAY_LENGTH(specData); ++i) {
            printf("specdata[%d] = %d\n", i, specData[i]);
        }
#endif

        VkSpecializationMapEntry entries[] = {
            {0, sizeof(uint32_t) * 0, sizeof(uint32_t)},
            {1, sizeof(uint32_t) * 1, sizeof(uint32_t)},
            {2, sizeof(uint32_t) * 2, sizeof(uint32_t)},
            {3, sizeof(uint32_t) * 3, sizeof(uint32_t)},
            {4, sizeof(uint32_t) * 4, sizeof(uint32_t)},
            {5, sizeof(uint32_t) * 5, sizeof(uint32_t)},
            {6, sizeof(uint32_t) * 6, sizeof(uint32_t)},
            {7, sizeof(uint32_t) * 7, sizeof(uint32_t)},
            {8, sizeof(uint32_t) * 8, sizeof(uint32_t)},
            {9, sizeof(uint32_t) * 9, sizeof(uint32_t)},
            {10, sizeof(uint32_t) * 10, sizeof(uint32_t)},
            {11, sizeof(uint32_t) * 11, sizeof(uint32_t)},
            {12, sizeof(uint32_t) * 12, sizeof(uint32_t)},
            {13, sizeof(uint32_t) * 13, sizeof(uint32_t)},
            {14, sizeof(uint32_t) * 14, sizeof(uint32_t)},
            {15, sizeof(uint32_t) * 15, sizeof(uint32_t)},
            {16, sizeof(uint32_t) * 16, sizeof(uint32_t)},
            {17, sizeof(uint32_t) * 17, sizeof(uint32_t)},
        };

        VkSpecializationInfo specInfo =
        {
            ARRAY_LENGTH(specData),
            entries,
            sizeof(specData),
            specData,
        };

        ComputePipelineBuilder builder;
        builder.AddShaderModule(gemmShader).SetPipelineLayout(m_renderPassParameters.GetPipelineLayout());
        VkComputePipelineCreateInfo pipelineCreateInfo = builder.Build();
        VK_ASSERT(vkCreateComputePipelines(GetRenderDevice()->GetDevice(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_pipeline));

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), gemmShader, nullptr);
    }

void RenderPassGEMM::GetCoopMatProperties()
{

    // Query the list of supported cooperative matrix multiply sizes/types.
    uint32_t numCooperativeMatrixProperties = 0;

    auto instance = GetRenderDevice()->GetInstance();
    auto physicalDevice = GetRenderDevice()->GetPhysicalDevice();

    PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV pfn_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV =
        (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesNV");

    VK_ASSERT( pfn_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV(physicalDevice, &numCooperativeMatrixProperties, NULL));

    m_vCoopMatProperties.resize(numCooperativeMatrixProperties);
    for (uint32_t i = 0; i < numCooperativeMatrixProperties; ++i) {
        m_vCoopMatProperties[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_NV;
        m_vCoopMatProperties[i].pNext = NULL;
    }

    VK_ASSERT(pfn_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV(physicalDevice, &numCooperativeMatrixProperties, m_vCoopMatProperties.data()));

}

RenderPassGEMM::~RenderPassGEMM()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}


void RenderPassGEMM::RecordCommandBuffer()
{
    if (m_cmdBuf == VK_NULL_HANDLE)
    {
        m_cmdBuf = GetRenderDevice()->AllocateComputeCommandBuffer();
    }
    else
    {
        vkResetCommandBuffer(m_cmdBuf, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    }
    VkCommandBufferBeginInfo beginInfo =
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr};

    // vkQueueWaitIdle(GetRenderDevice()->GetComputeQueue());
    VK_ASSERT(vkBeginCommandBuffer(m_cmdBuf, &beginInfo));
    {
        SCOPED_MARKER(m_cmdBuf, "GEMM");

        VkPipelineLayout pipelineLayout =  m_renderPassParameters.GetPipelineLayout();

        VkDescriptorSetLayout descLayout = m_renderPassParameters.GetDescriptorSetLayout();
        std::vector<VkDescriptorSetLayout> descLayouts = {descLayout};

        std::vector<VkDescriptorSet> vDescSets = {m_renderPassParameters.AllocateDescriptorSet("", 0)};

        vkCmdBindPipeline(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, (uint32_t)vDescSets.size(), vDescSets.data(), 0, nullptr);
        vkCmdDispatch(m_cmdBuf, N / TILE_N, M / TILE_M, 1);
    }
    vkEndCommandBuffer(m_cmdBuf);

}
}
