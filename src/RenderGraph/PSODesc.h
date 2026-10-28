#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "MeshVertex.h"
#include "PipelineStateBuilder.h"

namespace Muyo::RenderGraph
{

/// Which vertex layout a graphics node's pipeline expects.
enum class VertexType : uint8_t
{
    GENERIC,  ///< `Vertex` (position/normal/texcoord) used by scene geometry.
    UI,       ///< ImGui's `ImDrawVert` used by the UI pass.
    COUNT     ///< Number of values; not a valid value, only the bound for the layout table.
};

/// Primitive topology for a graphics pipeline.
enum class PrimitiveTopology : uint8_t
{
    TRIANGLES,       ///< Triangle list.
    TRIANGLE_STRIP,  ///< Triangle strip.
    LINES,           ///< Line list.
    LINE_STRIP,      ///< Line strip.
    POINTS,          ///< Point list.
    COUNT            ///< Number of values; not a valid value, only the bound for the table below.
};

/// @brief Map an enum value through a table indexed by its underlying value.
///
/// A table rather than a `switch` because a `switch`'s exhaustiveness was a *belief*: the seven conversions in this
/// header fell off the end of a non-void function for any value outside the enum, which is undefined behaviour, and
/// `-Wreturn-type` reported all seven unnoticed (issue #174). Indexing makes the mapping total by construction, and
/// the table's size is **deduced from its initialisers** and checked against the enum's `COUNT`: adding an enumerator
/// without adding its mapping, or removing a mapping, is a compile error. The first version of this fix sized each
/// table *by* `COUNT`, which made that assertion vacuous - an added enumerator simply extended the array with a
/// zero - and a mutation test caught it.
///
/// The bounds check is a runtime check and not an `assert`, per AGENTS section 3: the value can be falsified by
/// *input* - a `psoDesc` a graph user filled in - so it has to survive `NDEBUG`.
///
/// @tparam Enum    Enum whose enumerators are contiguous from zero, with a trailing `COUNT`.
/// @tparam VkEnum  Vulkan enumerator type the table holds.
/// @tparam N       Table size, which must equal `Enum::COUNT`.
/// @param value    Value to map.
/// @param table    One Vulkan enumerator per `Enum` value, in declaration order.
/// @param typeName Enum name, used in the exception so the message says which type was wrong.
/// @return The mapped Vulkan enumerator.
/// @throws std::invalid_argument when `value` is not one of the enumerators.
template <typename Enum, typename VkEnum, std::size_t N>
constexpr VkEnum ToVkIndexed(Enum value, const std::array<VkEnum, N>& table, const char* typeName)
{
    static_assert(N == static_cast<std::size_t>(Enum::COUNT), "the table must cover every enumerator");
    const std::size_t index = static_cast<std::size_t>(value);
    if (index >= N)
    {
        throw std::invalid_argument(std::string(typeName) + " value " + std::to_string(index) +
                                    " is not one of its enumerators, so it has no Vulkan mapping.");
    }
    return table[index];
}

/// Maps PrimitiveTopology to the Vulkan enum.
constexpr VkPrimitiveTopology ToVkInputAssembly(PrimitiveTopology topology)
{
    constexpr std::array kMap{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                              VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
                              VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
    return ToVkIndexed(topology, kMap, "PrimitiveTopology");
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
    WIREFRAME,  ///< Wireframe (`VK_POLYGON_MODE_LINE`).
    COUNT       ///< Number of values; not a valid value, only the bound for the table below.
};

/// Maps FillMode to the Vulkan enum.
constexpr VkPolygonMode ToVkPolygonMode(FillMode fillMode)
{
    constexpr std::array kMap{VK_POLYGON_MODE_FILL, VK_POLYGON_MODE_LINE};
    return ToVkIndexed(fillMode, kMap, "FillMode");
}

/// Face culling mode.
enum class CullMode : uint8_t
{
    NONE,   ///< No culling.
    FRONT,  ///< Cull front faces.
    BACK,   ///< Cull back faces.
    COUNT   ///< Number of values; not a valid value, only the bound for the table below.
};

/// Maps CullMode to the Vulkan enum.
constexpr VkCullModeFlagBits ToVkCullMode(CullMode cullMode)
{
    constexpr std::array kMap{VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT};
    return ToVkIndexed(cullMode, kMap, "CullMode");
}

/// Winding considered to be the front face.
enum class FrontFace : uint8_t
{
    CW,    ///< Clockwise.
    CCW,   ///< Counter-clockwise.
    COUNT  ///< Number of values; not a valid value, only the bound for the table below.
};

/// Maps FrontFace to the Vulkan enum.
constexpr VkFrontFace ToVkFrontFace(FrontFace frontFace)
{
    constexpr std::array kMap{VK_FRONT_FACE_CLOCKWISE, VK_FRONT_FACE_COUNTER_CLOCKWISE};
    return ToVkIndexed(frontFace, kMap, "FrontFace");
}

/// Rasterization state.
struct RasterState
{
    FillMode fillMode = FillMode::SOLID;   ///< Polygon fill mode.
    CullMode cullMode = CullMode::BACK;    ///< Face culling.
    FrontFace frontFace = FrontFace::CCW;  ///< Front-face winding.

    bool depthClamp = false;  ///< Enable depth clamping.
    bool depthBias = false;   ///< Enable depth bias.

    int32_t depthBiasConstant = 0;  ///< Constant depth bias factor.
    float depthBiasSlope = 0.0F;    ///< Slope depth bias factor.
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
    ALWAYS,         ///< Always passes.
    COUNT           ///< Number of values; not a valid value, only the bound for the table below.
};

/// Maps CompareOp to the Vulkan enum.
constexpr VkCompareOp ToVkCompareOp(CompareOp compareOp)
{
    constexpr std::array kMap{VK_COMPARE_OP_NEVER,
                              VK_COMPARE_OP_LESS,
                              VK_COMPARE_OP_EQUAL,
                              VK_COMPARE_OP_LESS_OR_EQUAL,
                              VK_COMPARE_OP_GREATER,
                              VK_COMPARE_OP_NOT_EQUAL,
                              VK_COMPARE_OP_GREATER_OR_EQUAL,
                              VK_COMPARE_OP_ALWAYS};
    return ToVkIndexed(compareOp, kMap, "CompareOp");
}

/// Stencil operations for one face.
struct StencilOpState
{
    uint8_t failOp;       ///< Operation when the stencil test fails.
    uint8_t passOp;       ///< Operation when the stencil test passes.
    uint8_t depthFailOp;  ///< Operation when the stencil passes but depth fails.
    CompareOp compareOp;  ///< Stencil comparison operation.
};

/// Depth and stencil state.
struct DepthStencilState
{
    bool depthTestEnable = true;                     ///< Enable depth testing.
    bool depthWriteEnable = true;                    ///< Enable depth writes.
    CompareOp depthCompare = CompareOp::LESS_EQUAL;  ///< Depth comparison operation.

    bool stencilEnable = false;  ///< Enable stencil testing.
    StencilOpState front;        ///< Front-face stencil state.
    StencilOpState back;         ///< Back-face stencil state.
};

/// Maximum number of color attachments a node can declare.
constexpr uint32_t MAX_COLOR_ATTACHMENTS = 8;

/// Blend factor for color/alpha blending.
enum class BlendFactor : uint8_t
{
    ONE,                  ///< 1.0.
    ZERO,                 ///< 0.0.
    SRC_ALPHA,            ///< Source alpha.
    ONE_MINUS_SRC_ALPHA,  ///< 1 - source alpha.
    COUNT                 ///< Number of values; not a valid value, only the bound for the table below.
};

/// Maps BlendFactor to the Vulkan enum.
constexpr VkBlendFactor ToVkBlendFactor(BlendFactor blendFactor)
{
    constexpr std::array kMap{VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_ALPHA,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    return ToVkIndexed(blendFactor, kMap, "BlendFactor");
}

/// Blend operation.
enum class BlendOp : uint8_t
{
    ADD,       ///< source + destination.
    SUBTRACT,  ///< source - destination.
    COUNT      ///< Number of values; not a valid value, only the bound for the table below.
};

/// Maps BlendOp to the Vulkan enum.
constexpr VkBlendOp ToVkBlendOp(BlendOp blendOp)
{
    constexpr std::array kMap{VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT};
    return ToVkIndexed(blendOp, kMap, "BlendOp");
}

/// Per-attachment blend state.
struct ColorBlendAttachment
{
    bool blendEnable = true;  ///< Enable blending for this attachment.

    BlendFactor srcColor = BlendFactor::ONE;   ///< Source color factor.
    BlendFactor dstColor = BlendFactor::ZERO;  ///< Destination color factor.
    BlendOp colorOp = BlendOp::ADD;            ///< Color blend operation.

    BlendFactor srcAlpha = BlendFactor::ONE;   ///< Source alpha factor.
    BlendFactor dstAlpha = BlendFactor::ZERO;  ///< Destination alpha factor.
    BlendOp alphaOp = BlendOp::ADD;            ///< Alpha blend operation.

    uint8_t colorWriteMask =  ///< Which color channels are written.
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
};

/// Blend state for all color attachments of a node.
struct BlendState
{
    uint32_t attachmentCount = 0;                                         ///< Number of used attachments.
    std::array<ColorBlendAttachment, MAX_COLOR_ATTACHMENTS> attachments;  ///< Per-attachment state.
};

/// Multisample state.
struct MultisampleState
{
    uint8_t sampleCount = 1;           ///< Number of samples per pixel.
    bool sampleShadingEnable = false;  ///< Enable sample shading.
    float minSampleShading = 0.0F;     ///< Minimum fraction of sample shading.
};

/// Full pipeline state for a graphics node.
///
/// The graph turns this into a `VkPipeline` (via `CreatePipelineFromPSODesc`), deriving the
/// rendering attachment formats and vertex input from the node's declared resource uses.
struct PSODesc
{
    VertexType vertexType = VertexType::GENERIC;  ///< Vertex layout to use.
    InputAssemblyState inputAssembly{.topology = PrimitiveTopology::TRIANGLES,
                                     .primitiveRestart = false};  ///< Input assembly.
    RasterState rasterState{.fillMode = FillMode::SOLID,
                            .cullMode = CullMode::BACK,
                            .frontFace = FrontFace::CW,
                            .depthClamp = false,
                            .depthBias = false,
                            .depthBiasConstant = 0,
                            .depthBiasSlope = 0.0F};  ///< Rasterization state.
    DepthStencilState depthStencilState{.depthTestEnable = true,
                                        .depthWriteEnable = true,
                                        .depthCompare = CompareOp::LESS_EQUAL,
                                        .stencilEnable = false};     ///< Depth/stencil state.
    BlendState blendState{.attachmentCount = 0, .attachments = {}};  ///< Blend state.
    MultisampleState multisampleState{
        .sampleCount = 1, .sampleShadingEnable = false, .minSampleShading = 0.0F};  ///< Multisample state.
};

/// Vertex input layout selected by a `VertexType`.
struct VertexInputLayout
{
    VkVertexInputBindingDescription binding;                    ///< The layout's single binding (binding 0).
    std::vector<VkVertexInputAttributeDescription> attributes;  ///< Attribute descriptions, in location order.
};

/// Map a `VertexType` to the vertex input layout its pipeline must declare.
///
/// A table indexed by the enum, for the reason `ToVkIndexed` records: a `switch` made "this type has a layout" a
/// belief, and a value outside the enum left both vectors empty. The pipeline was then created with no vertex input
/// at all, and the failure surfaced at draw time as a validation error far from the value that caused it (issue
/// #178). The table's size is checked against `VertexType::COUNT`, so adding an enumerator without its layout is a
/// compile error rather than a runtime throw nobody notices.
///
/// @param vertexType Vertex layout to map.
/// @return The binding and attribute descriptions for that layout.
/// @throws std::invalid_argument when `vertexType` is not one of the enumerators.
inline VertexInputLayout ToVertexLayout(VertexType vertexType)
{
    static const std::array kMap{
        VertexInputLayout{Vertex::getBindingDescription(), Vertex::getAttributeDescriptions()},
        VertexInputLayout{UIVertex::getBindingDescription(), UIVertex::getAttributeDescriptions()}};
    return ToVkIndexed(vertexType, kMap, "VertexType");
}

/// Build a graphics pipeline from a PSODesc using dynamic rendering.
///
/// @param psoDesc        Pipeline state.
/// @param vkDevice       Logical device.
/// @param shaderModuels  Vertex shader module followed by the fragment shader module.
/// @param renderingInfo  Dynamic-rendering attachment formats.
/// @param pSpecializationInfo Values for the shaders' specialization constants, applied to both stages,
///        or nullptr. Entries naming a constant a stage does not declare are ignored by Vulkan, so one
///        map can serve both.
/// @param pipelineCache Optional driver pipeline cache to record this pipeline in, or `VK_NULL_HANDLE`.
///        It is the caller's cache; this function neither keeps nor destroys it.
/// @return The created pipeline (caller owns it). Always created with
///         VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT and `layout = VK_NULL_HANDLE` (B1b).
inline VkPipeline CreatePipelineFromPSODesc(const PSODesc& psoDesc, VkDevice vkDevice,
                                            const std::vector<VkShaderModule>& shaderModuels,
                                            VkPipelineRenderingCreateInfo renderingInfo,
                                            const VkSpecializationInfo* pSpecializationInfo = nullptr,
                                            VkPipelineCache pipelineCache = VK_NULL_HANDLE)
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

    // Total by construction: an out-of-range value throws here, naming the type and the value, instead of leaving
    // both vectors empty and building a pipeline with no vertex input (#178).
    const VertexInputLayout layout = ToVertexLayout(psoDesc.vertexType);
    const std::vector<VkVertexInputBindingDescription> indexDescs{layout.binding};
    const std::vector<VkVertexInputAttributeDescription> attributeDescs = layout.attributes;
    psoBuilder.setVertextInfo(indexDescs, attributeDescs);

    psoBuilder.setDescriptorHeap();
    VkPipeline pipeline = psoBuilder.setShaderModules(shaderModuels, pSpecializationInfo)
                              .setAssembly(iaBuilder.Build())
                              .setDynamicStates(dynamicStates)
                              .setRasterizer(rsBuilder.Build())
                              .setMSAA(msBuilder.Build())
                              .setColorBlending(blendBuilder.Build())
                              .setDepthStencil(depthStencilBuilder.Build())
                              .setRenderingCreateInfo(renderingInfo)
                              .Build(vkDevice, pipelineCache);
    return pipeline;
}

}  // namespace Muyo::RenderGraph
