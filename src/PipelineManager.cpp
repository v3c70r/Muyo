#include "PipelineManager.h"
#include <cassert>
#include <fstream>
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderPass.h"
#include "Debug.h"

// Singletone 
static PipelineManager sPipelineManager;
PipelineManager* GetPipelineManager() { return &sPipelineManager; }

PipelineManager::PipelineManager()
{
    InitializeDefaultBlendStats();
}

void PipelineManager::InitializeDefaultBlendStats()
{
    VkPipelineColorBlendAttachmentState defaultBlendState = {};
    defaultBlendState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    defaultBlendState.blendEnable = VK_FALSE;
    defaultBlendState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    defaultBlendState.dstColorBlendFactor =
        VK_BLEND_FACTOR_ZERO;                                        // Optional
    defaultBlendState.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
    defaultBlendState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    defaultBlendState.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ZERO;                             // Optional
    defaultBlendState.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    for(auto& state : m_aBlendModes)
    {
        state = defaultBlendState;
    }
}

PipelineManager::~PipelineManager()
{
    // TODO: Clean up
}
void PipelineManager::CreateStaticObjectPipeline(
    uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout,
    VkRenderPass pass)
{
    mViewport.x = 0.0f;
    mViewport.y = 0.0f;
    mViewport.width = (float)width;
    mViewport.height = (float)height;
    mViewport.minDepth = 0.0f;
    mViewport.maxDepth = 1.0f;

    mScissorRect.offset = {0, 0};
    mScissorRect.extent = {width, height};

    maPipelineLayouts[PIPELINE_TYPE_STATIC_OBJECT] = CreatePipelineLayout(descriptorSetLayout);

    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/triangle.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/triangle.frag.spv"));

    // build the stuff with builder
    PipelineStateBuilder builder;

    maPipelines[PIPELINE_TYPE_STATIC_OBJECT] =
        builder.setShaderModules({vertShdr, fragShdr})
            .setVertextInfo({Vertex::getBindingDescription()},
                            Vertex::getAttributeDescriptions())
            .setAssembly(GetIAInfo())
            .setViewport(mViewport, mScissorRect)
            .setRasterizer(GetRasterInfo())
            .setMSAA(GetMultisampleState())
            .setColorBlending(GetBlendState(1))
            .setPipelineLayout(maPipelineLayouts[PIPELINE_TYPE_STATIC_OBJECT])
            .setDepthStencil(GetDepthStencilCreateinfo())
            .setRenderPass(pass)
            .build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(
        reinterpret_cast<uint64_t>(maPipelines[PIPELINE_TYPE_STATIC_OBJECT]),
        VK_OBJECT_TYPE_PIPELINE, "Static Object");
}

void PipelineManager::DestroyStaticObjectPipeline()
{
    vkDestroyPipeline(GetRenderDevice()->GetDevice(),
                      maPipelines[PIPELINE_TYPE_STATIC_OBJECT], nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(),
                            maPipelineLayouts[PIPELINE_TYPE_STATIC_OBJECT],
                            nullptr);
    maPipelines[PIPELINE_TYPE_STATIC_OBJECT] = VK_NULL_HANDLE;
    maPipelineLayouts[PIPELINE_TYPE_STATIC_OBJECT] = VK_NULL_HANDLE;
}

void PipelineManager::CreateGBufferPipeline(
    uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout,
    VkRenderPass pass)
{
    mViewport.x = 0.0f;
    mViewport.y = 0.0f;
    mViewport.width = (float)width;
    mViewport.height = (float)height;
    mViewport.minDepth = 0.0f;
    mViewport.maxDepth = 1.0f;
    mScissorRect.offset = {0, 0};
    mScissorRect.extent = {width, height};

    maPipelineLayouts[PIPELINE_TYPE_GBUFFER] = CreatePipelineLayout(descriptorSetLayout);

    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/gbuffer.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/gbuffer.frag.spv"));
    PipelineStateBuilder builder;

    maPipelines[PIPELINE_TYPE_GBUFFER] =
        builder.setShaderModules({vertShdr, fragShdr})
            .setVertextInfo({Vertex::getBindingDescription()},
                            Vertex::getAttributeDescriptions())
            .setAssembly(GetIAInfo())
            .setViewport(mViewport, mScissorRect)
            .setRasterizer(GetRasterInfo())
            .setMSAA(GetMultisampleState())
            .setColorBlending(GetBlendState(4))
            .setPipelineLayout(maPipelineLayouts[PIPELINE_TYPE_GBUFFER])
            .setDepthStencil(GetDepthStencilCreateinfo())
            .setRenderPass(pass)
            .build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(
        reinterpret_cast<uint64_t>(maPipelines[PIPELINE_TYPE_GBUFFER]),
        VK_OBJECT_TYPE_PIPELINE, "GBuffer");
}

void PipelineManager::DestroyGBufferPipeline() {
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), maPipelines[PIPELINE_TYPE_GBUFFER], nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(),
                            maPipelineLayouts[PIPELINE_TYPE_GBUFFER], nullptr);
    maPipelines[PIPELINE_TYPE_GBUFFER] = VK_NULL_HANDLE;
    maPipelineLayouts[PIPELINE_TYPE_GBUFFER] = VK_NULL_HANDLE;
}

void PipelineManager::CreateImGuiPipeline(
    uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout,
    VkRenderPass pass)
{
    mViewport.x = 0.0f;
    mViewport.y = 0.0f;
    mViewport.width = (float)width;
    mViewport.height = (float)height;
    mViewport.minDepth = 0.0f;
    mViewport.maxDepth = 1.0f;
    mScissorRect.offset = {0, 0};
    mScissorRect.extent = {width, height};

    // Push constants for UI
    // UI params are set via push constants
    struct PushConstBlock
    {
        glm::vec2 scale;
        glm::vec2 translate;
    };

    VkPushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.size = sizeof(PushConstBlock);
    pushConstantRange.offset = 0;

    maPipelineLayouts[PIPELINE_TYPE_IMGUI] =
        CreatePipelineLayout(descriptorSetLayout, pushConstantRange);

    // Dynmaic state
    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/ui.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/ui.frag.spv"));
    PipelineStateBuilder builder;

    maPipelines[PIPELINE_TYPE_IMGUI] =
        builder.setShaderModules({vertShdr, fragShdr})
            .setVertextInfo({UIVertex::getBindingDescription()},
                            UIVertex::getAttributeDescriptions())
            .setAssembly(GetIAInfo())
            .setViewport(mViewport, mScissorRect)
            .setRasterizer(GetRasterInfo())
            .setMSAA(GetMultisampleState())
            .setColorBlending(GetBlendState(1))
            .setPipelineLayout(maPipelineLayouts[PIPELINE_TYPE_IMGUI])
            .setDynamicStates(dynamicStateEnables)
            .setDepthStencil(GetDepthStencilCreateinfo())
            .setRenderPass(pass)
            .build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(
        reinterpret_cast<uint64_t>(maPipelines[PIPELINE_TYPE_IMGUI]),
        VK_OBJECT_TYPE_PIPELINE, "ImGui");
}

void PipelineManager::DestroyImGuiPipeline()
{
    vkDestroyPipeline(GetRenderDevice()->GetDevice(),
                      maPipelines[PIPELINE_TYPE_IMGUI], nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(),
                            maPipelineLayouts[PIPELINE_TYPE_IMGUI], nullptr);
    maPipelines[PIPELINE_TYPE_IMGUI] = VK_NULL_HANDLE;
    maPipelineLayouts[PIPELINE_TYPE_IMGUI] = VK_NULL_HANDLE;
}

// Private helpers
VkShaderModule PipelineManager::CreateShaderModule(
    const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shdrModule;
    assert(vkCreateShaderModule(GetRenderDevice()->GetDevice(), &createInfo,
                                nullptr, &shdrModule) == VK_SUCCESS);
    return shdrModule;
}

std::vector<char> PipelineManager::ReadSpv(const std::string& fileName)
{
    std::ifstream file(fileName, std::ios::ate | std::ios::binary);
    assert(file.is_open());
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}
VkPipelineInputAssemblyStateCreateInfo PipelineManager::GetIAInfo(
    VkPrimitiveTopology topology, bool bEnablePrimtiveStart)
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
    inputAssemblyInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = topology;
    inputAssemblyInfo.primitiveRestartEnable = bEnablePrimtiveStart;
    return inputAssemblyInfo;
}

VkPipelineViewportStateCreateInfo PipelineManager::GetViewportState(
    uint32_t width, uint32_t height)
{
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &mViewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &mScissorRect;
    return viewportState;
}

VkPipelineRasterizationStateCreateInfo PipelineManager::GetRasterInfo(
    bool bWireframe)
{
    VkPipelineRasterizationStateCreateInfo rasterizerInfo = {};
    rasterizerInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerInfo.depthClampEnable = VK_FALSE;
    rasterizerInfo.rasterizerDiscardEnable =
        VK_FALSE;  // Discard any output to framebuffer
    rasterizerInfo.polygonMode =
        bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizerInfo.lineWidth = 1.0f;
    rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;

    // Depth bias, disabled for now
    rasterizerInfo.depthBiasEnable = VK_FALSE;
    rasterizerInfo.depthBiasConstantFactor = 0.0f;  // Optional
    rasterizerInfo.depthBiasClamp = 0.0f;           // Optional
    rasterizerInfo.depthBiasSlopeFactor = 0.0f;     // Optional
    return rasterizerInfo;
}

VkPipelineMultisampleStateCreateInfo PipelineManager::GetMultisampleState()
{
    VkPipelineMultisampleStateCreateInfo multisamplingInfo = {};
    multisamplingInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisamplingInfo.sampleShadingEnable = VK_FALSE;
    multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisamplingInfo.minSampleShading = 1.0f;
    multisamplingInfo.pSampleMask = nullptr;
    multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
    multisamplingInfo.alphaToOneEnable = VK_FALSE;
    return multisamplingInfo;
}

VkPipelineColorBlendStateCreateInfo PipelineManager::GetBlendState(size_t numAttachments)
{
    assert(numAttachments <= MAX_NUM_ATTACHMENTS);
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;  // Optional
    colorBlending.attachmentCount = static_cast<uint32_t>(numAttachments);
    colorBlending.pAttachments = m_aBlendModes.data();
    colorBlending.blendConstants[0] = 0.0f;  // Optional
    colorBlending.blendConstants[1] = 0.0f;  // Optional
    colorBlending.blendConstants[2] = 0.0f;  // Optional
    colorBlending.blendConstants[3] = 0.0f;  // Optional
    return colorBlending;
}

VkPipelineLayout PipelineManager::CreatePipelineLayout(VkDescriptorSetLayout descriptorSetLayout, VkPushConstantRange pushConstantRange)
{
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    if (pushConstantRange.size > 0)
    {
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    }
    assert(vkCreatePipelineLayout(GetRenderDevice()->GetDevice(),
                                  &pipelineLayoutInfo, nullptr,
                                  &pipelineLayout) == VK_SUCCESS);
    return pipelineLayout;
}

VkPipelineDepthStencilStateCreateInfo
PipelineManager::GetDepthStencilCreateinfo()
{
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    // depth clamp?
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};
    return depthStencil;
}
