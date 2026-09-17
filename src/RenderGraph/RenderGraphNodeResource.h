#pragma once
#include <cstdint>
#include <optional>

#include "RenderGraphResourceHandle.h"
#include "vulkan/vulkan_core.h"

namespace Muyo::RenderGraph
{
/// Which executor / queue a node runs on. CPU is treated as a queue inside the graph.
///
/// A node with `QueueType::COMPUTE` **and** `RenderGraphNodeCreateInfo::async` set may be run
/// concurrently with the graphics queue; every other value is recorded on the graphics queue.
enum class QueueType : uint8_t
{
    GRAPHICS,     ///< Graphics queue.
    COMPUTE,      ///< Compute queue (async only when the node opts in).
    COPY,         ///< Transfer work (currently the graphics queue).
    RAY_TRACING,  ///< Ray tracing pipeline, dispatched on the graphics queue.
    CPU,          ///< Host-side node, executed inline while the graph runs.
    COUNT         ///< Number of queue types; not a valid value.
};

/// The concrete Vulkan object kind behind a resource handle.
enum class ResourceKind : uint8_t
{
    IMAGE,                   ///< A `VkImage` (render target / storage image).
    BUFFER,                  ///< A `VkBuffer`.
    ACCELERATION_STRUCTURE,  ///< A `VkAccelerationStructureKHR` (e.g. a TLAS).
    SAMPLER                  ///< A `VkSampler`.
};

/// Whether a node reads or writes a resource. Drives barrier generation.
enum class ResourceIOType : uint8_t
{
    READ,            ///< Read-only access.
    WRITE,           ///< Write-only access.
    READ_WRITE,      ///< Read and write (e.g. an atomic counter).
    TRANSFER_WRITE,  ///< Written by a transfer operation (e.g. `vkCmdCopyBuffer`).
};

/// The semantic role of a resource within a node.
///
/// Together with `ResourceIOType` this selects a `UsagePolicy` (barrier stage/access/layout), and
/// for graphics nodes it also determines how the resource is bound (attachment, vertex buffer...).
enum class ResourceUsage : uint8_t
{
    // Images
    SAMPLED,                   ///< Sampled texture.
    STORAGE_IMAGE,             ///< Read/write storage image.
    COLOR_ATTACHMENT,          ///< Color render target.
    DEPTH_STENCIL_ATTACHMENT,  ///< Depth/stencil render target.
    INPUT_ATTACHMENT,          ///< Input attachment read.
    RESOLVE_ATTACHMENT,        ///< Multisample resolve target.

    // Buffers
    UNIFORM_BUFFER,        ///< Uniform buffer.
    STORAGE_BUFFER,        ///< Storage buffer.
    VERTEX_BUFFER,         ///< Vertex buffer.
    INDEX_BUFFER,          ///< Index buffer.
    INDIRECT_BUFFER,       ///< Indirect draw/dispatch buffer.
    DRAW_COMMAND_BUFFER,   ///< Draw/draw-count command buffer (written by compute, read by draws).

    // RT / Mesh / Work Graph
    ACCEL_STRUCTURE,  ///< Top-level acceleration structure (TLAS).
    TASK_PAYLOAD,     ///< Mesh shader task payload.
    MESH_PAYLOAD,     ///< Mesh shader output payload.
    WORKGRAPH_NODE,   ///< GPU work graph node record.

    // Special
    PUSH_CONSTANTS,  ///< Push constants (not resolved as a resource).
};

/// Monotonic version counter used to track resource writes across nodes.
using ResourceVersion = uint64_t;

/// Built-in descriptor set the graph writes a resource into for graphics nodes.
///
/// Compute and ray tracing nodes use an explicit `DescriptorBinding` instead.
enum class ResourceBindingSemantic : uint8_t
{
    PER_VIEW,  ///< Set 0: per-frame view/camera data.
    PER_OBJ,   ///< Set 1: per-object data.
    MATERIAL,  ///< Set 2: materials and the bindless texture array.
    COUNT,     ///< Number of semantic sets; not a valid value.
    NONE,      ///< Not bound to a built-in semantic set.
};

/// Explicit shader descriptor location (set + binding) for a resource.
///
/// When set on a ResourceUse the graph builds the node's descriptors from this instead of the
/// built-in semantic sets. Required for arbitrary compute passes (e.g. GPU-driven draw command
/// generation) whose shaders bind raw set/binding.
struct DescriptorBinding
{
    uint32_t set = 0;      ///< Descriptor set index.
    uint32_t binding = 0;  ///< Binding index within the set.
};

/// A node's declaration that it uses a resource (the interface passed to RenderGraphBuilder).
struct ResourceUse
{
    ResourceHandle handle;                                  ///< Name of the resource.
    ResourceIOType io;                                      ///< Read / write / read-write.
    ResourceUsage usage;                                    ///< Semantic role.
    ResourceKind kind;                                      ///< Concrete resource kind.
    ResourceBindingSemantic bindingSemantic = ResourceBindingSemantic::NONE;  ///< Built-in set for graphics nodes.
    /// Optional explicit descriptor location. Only used when bindingSemantic == NONE.
    std::optional<DescriptorBinding> descriptorBinding = std::nullopt;
};

/// A `ResourceUse` after the graph has resolved it to concrete barrier state.
///
/// Produced by `ResolveResourceUse()`; mostly consumed internally by `RenderGraphBuilder`.
struct ResolvedResourceUse
{
    ResourceHandle handle;         ///< Name of the resource.
    ResourceVersion version = 0;   ///< Write version this use refers to.

    ResourceKind kind;             ///< Concrete resource kind.

    ResourceIOType io;             ///< Read / write / read-write.
    ResourceUsage usage;           ///< Semantic role.

    VkPipelineStageFlags2 stages;  ///< Pipeline stages that access the resource.
    VkAccessFlags2 access;         ///< Access mask for the resource.

    // Optional, depends on kind
    VkImageLayout imageLayout;     ///< Image layout (images only).
    VkFormat format;               ///< Optional format (validation / pipeline creation).
    VkExtent3D extent;             ///< Optional extent (validation / trace dispatch).

    ResourceBindingSemantic bindingSemantic = ResourceBindingSemantic::NONE;  ///< Built-in set.
    std::optional<DescriptorBinding> descriptorBinding = std::nullopt;         ///< Explicit set/binding.
};

}  // namespace Muyo::RenderGraph
