#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

#include "MeshVertex.h"
#include "PipelineStateBuilder.h"

namespace Muyo::RenderGraph
{

/// Which vertex layout a graphics node's pipeline expects.
enum class VertexType : uint8_t
{
    GENERIC,  ///< `Vertex` (position/normal/texcoord) used by scene geometry.
    UI        ///< ImGui's `ImDrawVert` used by the UI pass.
};

/// Primitive topology for a graphics pipeline.
enum class PrimitiveTopology : uint8_t
{
    TRIANGLES,       ///< Triangle list.
    TRIANGLE_STRIP,  ///< Triangle strip.
    LINES,           ///< Line list.
    LINE_STRIP,      ///< Line strip.
    POINTS,          ///< Point list.
};

/// Maps PrimitiveTopology to the Vulkan enum.
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

/// Input assembly state (topology and primitive restart).
struct InputAssemblyState
{
    PrimitiveTopology topology = PrimitiveTopology::TRIANGLES;  ///< Primitive topology.
    bool primitiveRestart = false;                              ///< Enable primitive restart.
};

/// Polygon fill mode.
enum class FillMode : uint8_t
{
    SOLID,      ///< Filled polygons.
    WIREFRAME   ///< Wireframe (`VK_POLYGON_MODE_LINE`).
};

/// Maps FillMode to the Vulkan enum.
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

/// Face culling mode.
enum class CullMode : uint8_t
{
    NONE,   ///< No culling.
    FRONT,  ///< Cull front faces.
    BACK    ///< Cull back faces.
};

/// Maps CullMode to the Vulkan enum.
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

/// Winding considered to be the front face.
enum class FrontFace : uint8_t
{
    CW,   ///< Clockwise.
    CCW   ///< Counter-clockwise.
};

/// Maps FrontFace to the Vulkan enum.
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

/// Rasterization state.
struct RasterState
{
    FillMode fillMode = FillMode::SOLID;   ///< Polygon fill mode.
    CullMode cullMode = CullMode::BACK;    ///< Face culling.
    FrontFace frontFace = FrontFace::CCW;  ///< Front-face winding.

    bool depthClamp = false;               ///< Enable depth clamping.
    bool depthBias = false;                ///< Enable depth bias.

    int32_t depthBiasConstant = 0;         ///< Constant depth bias factor.
    float depthBiasSlope = 0.0F;           ///< Slope depth bias factor.
};

/// Comparison operation used by depth/stencil state.
enum class CompareOp : uint8_t
{
    NEVER,          ///< Never passes.
    LESS,           ///< Passes if the new value is less.
    EQUAL,          ///< Passes if equal.
    LESS_EQUAL,     ///< Passes if less or equal.
    GREATER,        ///< Passes if greater.
    NOT_EQUAL,      ///< Passes if not equal.
    GREATER_EQUAL,  ///< Passes if greater or equal.
    ALWAYS          ///< Always passes.
};

/// Maps CompareOp to the Vulkan enum.
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

/// Stencil operations for one face.
struct StencilOpState
{
    uint8_t failOp;        ///< Operation when the stencil test fails.
    uint8_t passOp;        ///< Operation when the stencil test passes.
    uint8_t depthFailOp;   ///< Operation when the stencil passes but depth fails.
    CompareOp compareOp;   ///< Stencil comparison operation.
};

/// Depth and stencil state.
struct DepthStencilState
{
    bool depthTestEnable = true;                        ///< Enable depth testing.
    bool depthWriteEnable = true;                       ///< Enable depth writes.
    CompareOp depthCompare = CompareOp::LESS_EQUAL;     ///< Depth comparison operation.

    bool stencilEnable = false;                         ///< Enable stencil testing.
    StencilOpState front;                               ///< Front-face stencil state.
    StencilOpState back;                                ///< Back-face stencil state.
};

/// Maximum number of color attachments a node can declare.
constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;

/// Blend factor for color/alpha blending.
enum class BlendFactor : uint8_t
{
    ONE,                   ///< 1.0.
    ZERO,                  ///< 0.0.
    SRC_ALPHA,             ///< Source alpha.
    ONE_MINUS_SRC_ALPHA    ///< 1 - source alpha.
};

/// Maps BlendFactor to the Vulkan enum.
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

/// Blend operation.
enum class BlendOp : uint8_t
{
    ADD,       ///< source + destination.
    SUBTRACT   ///< source - destination.
};

/// Maps BlendOp to the Vulkan enum.
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

/// Per-attachment blend state.
struct ColorBlendAttachment
{
    bool blendEnable = true;                          ///< Enable blending for this attachment.

    BlendFactor srcColor = BlendFactor::ONE;          ///< Source color factor.
    BlendFactor dstColor = BlendFactor::ZERO;         ///< Destination color factor.
    BlendOp colorOp = BlendOp::ADD;                   ///< Color blend operation.

    BlendFactor srcAlpha = BlendFactor::ONE;          ///< Source alpha factor.
    BlendFactor dstAlpha = BlendFactor::ZERO;         ///< Destination alpha factor.
    BlendOp alphaOp = BlendOp::ADD;                   ///< Alpha blend operation.

    uint8_t colorWriteMask =                          ///< Which color channels are written.
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
};

/// Blend state for all color attachments of a node.
struct BlendState
{
    uint32_t attachmentCount = 0;                                          ///< Number of used attachments.
    std::array<ColorBlendAttachment, MAX_COLOR_ATTACHMENTS> attachments;   ///< Per-attachment state.
};

/// Multisample state.
struct MultisampleState
{
    uint8_t sampleCount = 1;               ///< Number of samples per pixel.
    bool sampleShadingEnable = false;      ///< Enable sample shading.
    float minSampleShading = 0.0F;         ///< Minimum fraction of sample shading.
};

/// Full pipeline state for a graphics node.
///
/// The graph turns this into a `VkPipeline` (via `CreatePipelineFromPSODesc`), deriving the
/// rendering attachment formats and vertex input from the node's declared resource uses.
struct PSODesc
{
    VertexType vertexType = VertexType::GENERIC;  ///< Vertex layout to use.
    InputAssemblyState inputAssembly{.topology = PrimitiveTopology::TRIANGLES, .primitiveRestart = false};  ///< Input assembly.
    RasterState rasterState{.fillMode = FillMode::SOLID,
                            .cullMode = CullMode::BACK,
                            .frontFace = FrontFace::CW,
                            .depthClamp = false,
                            .depthBias = false,
                            .depthBiasConstant = 0,
                            .depthBiasSlope = 0.0F};                                        ///< Rasterization state.
    DepthStencilState depthStencilState{.depthTestEnable = true,
                                        .depthWriteEnable = true,
                                        .depthCompare = CompareOp::LESS_EQUAL,
                                        .stencilEnable = false};                        ///< Depth/stencil state.
    BlendState blendState{.attachmentCount = 0, .attachments = {}};                     ///< Blend state.
    MultisampleState multisampleState{.sampleCount = 1, .sampleShadingEnable = false, .minSampleShading = 0.0F};  ///< Multisample state.
};

/// Build a graphics pipeline from a PSODesc using dynamic rendering.
///
/// @param psoDesc        Pipeline state.
/// @param vkDevice       Logical device.
/// @param shaderModuels  Vertex shader module followed by the fragment shader module.
/// @param pipelineLayout Pipeline layout (descriptor sets and push constants).
/// @param renderingInfo  Dynamic-rendering attachment formats.
/// @return The created pipeline (caller owns it).
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
