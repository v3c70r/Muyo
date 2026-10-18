#pragma once
#include <vulkan/vulkan.h>

#include <stdexcept>

#include "RenderGraphNodeResource.h"
//// ResourceUse public interface
// struct ResourceUse
//{
//     ResourceHandle handle;
//     ResourceIOType io;
//     ResourceUsage usage;
// };
//// Resolved ResourceUse used within RenderGraph
// struct ResolvedResourceUse
//{
//     ResourceHandle handle;
//     ResourceVersion version = 0;
//
//     ResourceKind kind;
//
//     ResourceIOType io;    // read / write / rw
//     ResourceUsage usage;  // semantic role
//
//     VkPipelineStageFlags2 stages;
//     VkAccessFlags2 access;
//
//     // Optional, depends on kind
//     VkImageLayout imageLayout;  // if image
//     VkFormat format;            // optional validation
//     VkExtent3D extent;          // optional
// };

namespace Muyo::RenderGraph
{
/// Lookup key for the barrier policy table.
struct UsageKey
{
    ResourceUsage usage;  ///< Semantic usage.
    ResourceIOType io;    ///< Read/write direction.

    constexpr bool operator==(const UsageKey&) const = default;  ///< Equality for table lookup.
};

/// Barrier policy (stages/access/layout) for a (usage, io) pair.
struct UsagePolicy
{
    VkPipelineStageFlags2 stages;  ///< Destination pipeline stages.
    VkAccessFlags2 access;         ///< Destination access mask.
    VkImageLayout layout;          ///< Target image layout (ignored for buffers / AS / push constants).
};

/// Barrier policy table: maps every supported (`ResourceUsage`, `ResourceIOType`) pair to the
/// pipeline stages, access mask and image layout the graph uses when a node touches it.
///
/// This is the authoritative reference for how the graph synchronises resources; a use with no
/// entry here is rejected by `ResolvePolicy`.
inline constexpr std::pair<UsageKey, UsagePolicy> K_USAGE_POLICIES[] = {
    // ───────────── Images ─────────────
    // Every stage that can sample, not only the ones this engine's shaders happen to use: sampling from a
    // mesh, vertex, geometry or tessellation stage is legal Vulkan, and a read ordered only at the fragment
    // stage is ordered against nothing when it happens earlier. The stages the device does not enable are
    // removed by `VkRenderDevice::EnabledPipelineStages`, so naming them here cannot emit an illegal bit.
    {{ResourceUsage::SAMPLED, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
          VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
          VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
          VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
      VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
    {{ResourceUsage::STORAGE_IMAGE, ResourceIOType::READ_WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_GENERAL}},
    {{ResourceUsage::COLOR_ATTACHMENT, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
    {{ResourceUsage::COLOR_ATTACHMENT, ResourceIOType::READ_WRITE},
     {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
    {{ResourceUsage::DEPTH_STENCIL_ATTACHMENT, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}},
    {{ResourceUsage::INPUT_ATTACHMENT, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
    {{ResourceUsage::RESOLVE_ATTACHMENT, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
    // Rendered as a colour attachment, then transitioned to PRESENT_SRC after the node by
    // `RecordPresentTransition`: the presentation engine is the consumer and it is not a node, so the
    // barrier cannot be emitted at the start of a consumer the way every other layout is.
    {{ResourceUsage::PRESENT, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},

    // ───────────── Buffers ─────────────
    {{ResourceUsage::UNIFORM_BUFFER, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // A shader store. `HOST_WRITE` is the host-side write and lives in its own row below; the two are separate
    // because their barriers are, and a node's queue type cannot decide which one a declaration meant (#152).
    {{ResourceUsage::UNIFORM_BUFFER, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::STORAGE_BUFFER, ResourceIOType::READ_WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::STORAGE_BUFFER, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // A shader store. `HOST_WRITE` is the host-side write and lives in its own row below; the two are separate
    // because their barriers are, and a node's queue type cannot decide which one a declaration meant (#152).
    {{ResourceUsage::STORAGE_BUFFER, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::VERTEX_BUFFER, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // A shader store. `HOST_WRITE` is the host-side write and lives in its own row below; the two are separate
    // because their barriers are, and a node's queue type cannot decide which one a declaration meant (#152).
    {{ResourceUsage::VERTEX_BUFFER, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::INDEX_BUFFER, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // A shader store. `HOST_WRITE` is the host-side write and lives in its own row below; the two are separate
    // because their barriers are, and a node's queue type cannot decide which one a declaration meant (#152).
    {{ResourceUsage::INDEX_BUFFER, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::INDIRECT_BUFFER, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // A shader store. `HOST_WRITE` is the host-side write and lives in its own row below; the two are separate
    // because their barriers are, and a node's queue type cannot decide which one a declaration meant (#152).
    {{ResourceUsage::INDIRECT_BUFFER, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::UNIFORM_BUFFER, ResourceIOType::HOST_WRITE},
     {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::STORAGE_BUFFER, ResourceIOType::HOST_WRITE},
     {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::VERTEX_BUFFER, ResourceIOType::HOST_WRITE},
     {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::INDEX_BUFFER, ResourceIOType::HOST_WRITE},
     {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::INDIRECT_BUFFER, ResourceIOType::HOST_WRITE},
     {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // ───────────── Draw Command / Indirect Buffers ─────────────
    // The Read case
    {{ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // GPU Write case (e.g., a Compute shader generating draw calls)
    {{ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // GPU read-write case (e.g., an atomic draw/command counter also read by the same pass)
    {{ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::READ_WRITE},
     {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED}},
    // Transfer/CPU Write case (e.g., vkCmdCopyBuffer or Host mapping)
    {{ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::TRANSFER_WRITE},
     {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // A CPU node filling the draw list through a mapping - the Mazda scene's fixture does exactly this.
    {{ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::HOST_WRITE},
     {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}},
    // ───────────── Transfer / clear ─────────────
    // The transfer stage rather than ALL_COMMANDS: a copy or an upload is only ordered against other
    // transfer work and against whatever reads the result, so saying so lets the graph overlap it with a
    // running shader phase instead of stalling both.
    {{ResourceUsage::TRANSFER_SOURCE, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}},
    // TRANSFER_WRITE and not WRITE: for a buffer, WRITE means a *host* write in this table - that is how a
    // mapped upload declares itself - so a copy destination has to say it is a transfer. A clear needs
    // the same access, which is why one role covers it.
    {{ResourceUsage::TRANSFER_DEST, ResourceIOType::TRANSFER_WRITE},
     {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}},
    // ───────────── Ray Tracing / Mesh / Work Graph ─────────────
    {{ResourceUsage::ACCEL_STRUCTURE, ResourceIOType::READ},
     {VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
      VK_IMAGE_LAYOUT_UNDEFINED}},
    // A build writes the structure, and the write is ordered against the build stage rather than the shader
    // stage - `vkCmdBuildAccelerationStructuresKHR` is not shader work. This pair is what makes the graph emit
    // the build -> trace barrier a node would otherwise have to record by hand (#14).
    {{ResourceUsage::ACCEL_STRUCTURE, ResourceIOType::WRITE},
     {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::TASK_PAYLOAD, ResourceIOType::READ_WRITE},
     {VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::MESH_PAYLOAD, ResourceIOType::READ_WRITE},
     {VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED}},
    {{ResourceUsage::WORKGRAPH_NODE, ResourceIOType::READ_WRITE},
     {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED}}};

/// Looks up the `UsagePolicy` for a key.
///
/// @return The policy, or nothing when the pair is unsupported - the caller reports that with the
///         resource and both names in the message, which a bare string here cannot do.
constexpr std::optional<UsagePolicy> ResolvePolicy(UsageKey key)
{
    for (auto&& [k, v] : K_USAGE_POLICIES)
    {
        if (k == key) return v;
    }
    return std::nullopt;
}

/// Resolve a node's `ResourceUse` into a `ResolvedResourceUse`.
///
/// Fills in the barrier stages/access and (for images) the target layout from `K_USAGE_POLICIES`, and copies
/// the binding information through.
///
/// The declaration alone decides what a write means: `WRITE` is a shader store and `HOST_WRITE` is a host
/// write through a mapping (#152). The node's queue type is deliberately **not** consulted - it was the proxy
/// that used to make the choice, and it was wrong for a GPU node that maps a buffer.
///
/// @param use The declared resource use.
/// @return The resolved use consumed by the graph's barrier and binding logic.
inline ResolvedResourceUse ResolveResourceUse(const ResourceUse& use)
{
    // Special handling
    if (use.usage == ResourceUsage::PUSH_CONSTANTS)
    {
        throw std::logic_error("Push constants are not resolved as resources");
    }

    const std::optional<UsagePolicy> policy = ResolvePolicy({use.usage, use.io});
    if (!policy.has_value())
    {
        // Named, because this is the error a caller meets when a declaration picks the wrong pair - most
        // often TRANSFER_DEST with WRITE, which for a buffer means a shader store in this table. The resource
        // is named too, so a node with several uses says which one is wrong.
        throw std::runtime_error(std::string("Resource '") + use.handle + "' declares usage " + NameOf(use.usage) +
                                 " with IO " + NameOf(use.io) +
                                 ", which has no barrier policy. Check the pair: a shader store is WRITE, a "
                                 "copy destination is TRANSFER_WRITE with TRANSFER_DEST, and a host upload is "
                                 "HOST_WRITE.");
    }

    ResolvedResourceUse resolved{};
    resolved.handle = use.handle;
    resolved.version = 0;
    resolved.kind = use.kind;
    resolved.io = use.io;
    resolved.usage = use.usage;
    resolved.stages = policy->stages;
    resolved.access = policy->access;
    resolved.descriptorBinding = use.descriptorBinding;
    resolved.bufferRange = use.bufferRange;

    if (use.kind == ResourceKind::IMAGE)
    {
        resolved.imageLayout = policy->layout;
    }

    return resolved;
}
}  // namespace Muyo::RenderGraph
