#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

#include "MeshVertex.h"
#include "PipelineStateBuilder.h"

namespace Muyo::RenderGraph
{

enum class VertexType : uint8_t
{
    GENERIC,
    UI
};

enum class PrimitiveTopology : uint8_t
{
    TRIANGLES,
    TRIANGLE_STRIP,
    LINES,
    LINE_STRIP,
    POINTS,
};

constexpr VkPrimitiveTopology ToVkInputAssembly(PrimitiveTopology topology)
{
    switch (topology)
    {
        case PrimitiveTopology::TRIANGLES:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TRIANGLE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::LINES:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::LINE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopology::POINTS:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
}

struct InputAssemblyState
{
    PrimitiveTopology topology = PrimitiveTopology::TRIANGLES;
    bool primitiveRestart = false;
};

enum class FillMode : uint8_t
{
    SOLID,
    WIREFRAME
};

constexpr VkPolygonMode ToVkPolygonMode(FillMode fillMode)
{
    switch (fillMode)
    {
        case FillMode::SOLID:
            return VK_POLYGON_MODE_FILL;
        case FillMode::WIREFRAME:
            return VK_POLYGON_MODE_LINE;
    }
}

enum class CullMode : uint8_t
{
    NONE,
    FRONT,
    BACK
};

constexpr VkCullModeFlagBits ToVkCullMode(CullMode cullMode)
{
    switch (cullMode)
    {
        case CullMode::NONE:
            return VK_CULL_MODE_NONE;
        case CullMode::FRONT:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::BACK:
            return VK_CULL_MODE_BACK_BIT;
    }
}

enum class FrontFace : uint8_t
{
    CW,
    CCW
};

constexpr VkFrontFace ToVkFrontFace(FrontFace frontFace)
{
    switch (frontFace)
    {
        case FrontFace::CW:
            return VK_FRONT_FACE_CLOCKWISE;
        case FrontFace::CCW:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

struct RasterState
{
    FillMode fillMode = FillMode::SOLID;
    CullMode cullMode = CullMode::BACK;
    FrontFace frontFace = FrontFace::CCW;

    bool depthClamp = false;
    bool depthBias = false;

    int32_t depthBiasConstant = 0;
    float depthBiasSlope = 0.0F;
};

enum class CompareOp : uint8_t
{
    NEVER,
    LESS,
    EQUAL,
    LESS_EQUAL,
    GREATER,
    NOT_EQUAL,
    GREATER_EQUAL,
    ALWAYS
};

constexpr VkCompareOp ToVkCompareOp(CompareOp compareOp)
{
    switch (compareOp)
    {
        case CompareOp::NEVER:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::LESS:
            return VK_COMPARE_OP_LESS;
        case CompareOp::EQUAL:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LESS_EQUAL:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::GREATER:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NOT_EQUAL:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GREATER_EQUAL:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::ALWAYS:
            return VK_COMPARE_OP_ALWAYS;
    }
}

struct StencilOpState
{
    uint8_t failOp;
    uint8_t passOp;
    uint8_t depthFailOp;
    CompareOp compareOp;
};

struct DepthStencilState
{
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    CompareOp depthCompare = CompareOp::LESS_EQUAL;

    bool stencilEnable = false;
    StencilOpState front;
    StencilOpState back;
};

constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;

enum class BlendFactor : uint8_t
{
    ONE,
    ZERO,
    SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA
};

constexpr VkBlendFactor ToVkBlendFactor(BlendFactor blendFactor)
{
    switch (blendFactor)
    {
        case BlendFactor::ONE:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::ZERO:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::SRC_ALPHA:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::ONE_MINUS_SRC_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
}

enum class BlendOp : uint8_t
{
    ADD,
    SUBTRACT
};

constexpr VkBlendOp ToVkBlendOp(BlendOp blendOp)
{
    switch (blendOp)
    {
        case BlendOp::ADD:
            return VK_BLEND_OP_ADD;
        case BlendOp::SUBTRACT:
            return VK_BLEND_OP_SUBTRACT;
    }
}

struct ColorBlendAttachment
{
    bool blendEnable = true;

    BlendFactor srcColor = BlendFactor::ONE;
    BlendFactor dstColor = BlendFactor::ZERO;
    BlendOp colorOp = BlendOp::ADD;

    BlendFactor srcAlpha = BlendFactor::ONE;
    BlendFactor dstAlpha = BlendFactor::ZERO;
    BlendOp alphaOp = BlendOp::ADD;

    uint8_t colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
};

struct BlendState
{
    uint32_t attachmentCount = 0;
    std::array<ColorBlendAttachment, MAX_COLOR_ATTACHMENTS> attachments;
};

struct MultisampleState
{
    uint8_t sampleCount = 1;
    bool sampleShadingEnable = false;
    float minSampleShading = 0.0F;
};

struct PSODesc
{
    VertexType vertexType = VertexType::GENERIC;
    InputAssemblyState inputAssembly{.topology = PrimitiveTopology::TRIANGLES, .primitiveRestart = false};
    RasterState rasterState{.fillMode = FillMode::SOLID,
                            .cullMode = CullMode::BACK,
                            .frontFace = FrontFace::CW,
                            .depthClamp = false,
                            .depthBias = false,
                            .depthBiasConstant = 0,
                            .depthBiasSlope = 0.0F};
    DepthStencilState depthStencilState{.depthTestEnable = true,
                                        .depthWriteEnable = true,
                                        .depthCompare = CompareOp::LESS_EQUAL,
                                        .stencilEnable = false};
    BlendState blendState{.attachmentCount = 0, .attachments = {}};
    MultisampleState multisampleState{.sampleCount = 1, .sampleShadingEnable = false, .minSampleShading = 0.0F};
};

inline VkPipeline CreatePipelineFromPSODesc(const PSODesc& psoDesc, VkDevice vkDevice,
                                            const std::vector<VkShaderModule>& shaderModuels,
                                            VkPipelineLayout pipelineLayout,
                                            VkPipelineRenderingCreateInfo renderingInfo)
{
    // Create pipeline
    PipelineStateBuilder psoBuilder;

    InputAssemblyStateCIBuilder iaBuilder;
    iaBuilder.setPrimitiveRestartEnabled(psoDesc.inputAssembly.primitiveRestart)
        .setTopology(ToVkInputAssembly(psoDesc.inputAssembly.topology));

    RasterizationStateCIBuilder rsBuilder;
    rsBuilder.SetCullMode(ToVkCullMode(psoDesc.rasterState.cullMode))
        .SetPolygonMode(ToVkPolygonMode(psoDesc.rasterState.fillMode))
        .SetFrontFace(ToVkFrontFace(psoDesc.rasterState.frontFace))
        .SetDepthClampEnable(psoDesc.rasterState.depthClamp);
    //.SetDepthBiasEnable(psoDesc.rasterState.depthBias)
    //.SetDepthBiasConstantFactor(static_cast<float>(psoDesc.rasterState.depthBiasConstant))
    //.SetDepthBiasSlopeFactor(psoDesc.rasterState.depthBiasSlope);

    MultisampleStateCIBuilder msBuilder;

    BlendStateCIBuilder blendBuilder;
    for (uint32_t i = 0; i < psoDesc.blendState.attachmentCount; ++i)
    {
        const ColorBlendAttachment& srcAttachment = psoDesc.blendState.attachments[i];
        VkPipelineColorBlendAttachmentState dstAttachment{};
        dstAttachment.blendEnable = srcAttachment.blendEnable ? VK_TRUE : VK_FALSE;
        dstAttachment.srcColorBlendFactor = ToVkBlendFactor(srcAttachment.srcColor);
        dstAttachment.dstColorBlendFactor = ToVkBlendFactor(srcAttachment.dstColor);
        dstAttachment.colorBlendOp = ToVkBlendOp(srcAttachment.colorOp);
        dstAttachment.srcAlphaBlendFactor = ToVkBlendFactor(srcAttachment.srcAlpha);
        dstAttachment.dstAlphaBlendFactor = ToVkBlendFactor(srcAttachment.dstAlpha);
        dstAttachment.alphaBlendOp = ToVkBlendOp(srcAttachment.alphaOp);
        dstAttachment.colorWriteMask = srcAttachment.colorWriteMask;
        blendBuilder.AddAttachment(dstAttachment);
    }
    DepthStencilCIBuilder depthStencilBuilder;
    depthStencilBuilder.setDepthTestEnabled(psoDesc.depthStencilState.depthTestEnable)
        .setDepthWriteEnabled(psoDesc.depthStencilState.depthWriteEnable)
        .setDepthCompareOp(ToVkCompareOp(psoDesc.depthStencilState.depthCompare));

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    std::vector<VkVertexInputBindingDescription> indexDescs;
    std::vector<VkVertexInputAttributeDescription> attributeDescs;
    switch (psoDesc.vertexType)
    {
        case VertexType::GENERIC:
            indexDescs = {Vertex::getBindingDescription()};
            attributeDescs = {Vertex::getAttributeDescriptions()};
            break;
        case VertexType::UI:
            indexDescs = {UIVertex::getBindingDescription()};
            attributeDescs = {UIVertex::getAttributeDescriptions()};
            break;
    }
    psoBuilder.setVertextInfo(indexDescs, attributeDescs);

    VkPipeline pipeline = psoBuilder.setShaderModules(shaderModuels)
                              .setAssembly(iaBuilder.Build())
                              .setDynamicStates(dynamicStates)
                              .setRasterizer(rsBuilder.Build())
                              .setMSAA(msBuilder.Build())
                              .setColorBlending(blendBuilder.Build())
                              .setPipelineLayout(pipelineLayout)
                              .setDepthStencil(depthStencilBuilder.Build())
                              .setRenderingCreateInfo(renderingInfo)
                              .Build(vkDevice);
    return pipeline;
}

}  // namespace Muyo::RenderGraph
