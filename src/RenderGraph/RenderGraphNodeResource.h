#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

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
    WRITE,           ///< Written by a shader (a storage buffer store, an attachment write).
    READ_WRITE,      ///< Read and write (e.g. an atomic counter).
    TRANSFER_WRITE,  ///< Written by a transfer operation (e.g. `vkCmdCopyBuffer`).
    /// Written by the **host** through a mapping, e.g. `BufferResource::SetData` or `Map()`/`memcpy`.
    ///
    /// Distinct from `WRITE` because the barrier has to be: a host write's source scope is
    /// `HOST`/`HOST_WRITE`, and a shader write's is the shader stages. Declaring one as the other leaves the
    /// barrier ordering a write that never happened - and the graph cannot infer it from the node's queue
    /// type, because a GPU node that maps a buffer is a documented pattern (`EnsureMappingsReleased` guards
    /// it). See #152.
    HOST_WRITE,
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
    PRESENT,                   ///< Final image an external consumer (the presentation engine) reads.

    // Buffers
    UNIFORM_BUFFER,       ///< Uniform buffer.
    STORAGE_BUFFER,       ///< Storage buffer.
    VERTEX_BUFFER,        ///< Vertex buffer.
    INDEX_BUFFER,         ///< Index buffer.
    INDIRECT_BUFFER,      ///< Indirect draw/dispatch buffer.
    DRAW_COMMAND_BUFFER,  ///< Draw/draw-count command buffer (written by compute, read by draws).

    // Transfer / clear
    /// Copy source (`vkCmdCopyImageToBuffer`, `vkCmdCopyBufferToImage`, `vkCmdCopyBuffer`...).
    /// Images are moved to `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`.
    TRANSFER_SOURCE,
    /// Copy, upload, clear or fill destination (`vkCmdCopy*`, `vkCmdClear*Image`, `vkCmdFillBuffer`).
    ///
    /// One role covers all four because the barrier they need is the same one: the transfer stage,
    /// `VK_ACCESS_2_TRANSFER_WRITE_BIT`, and `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` for an image - a
    /// clear is a transfer write in the specification's own terms, not a colour-attachment one. Which
    /// command the node records is its own business; the graph only needs the access.
    TRANSFER_DEST,

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

/// @param io An IO type.
/// @return Whether the access writes the resource.
///
/// One place instead of five: every check that asks "is this a write" has to answer the same way, and adding
/// `HOST_WRITE` (#152) is the demonstration - the transient-lifetime validation counted writes by listing the
/// enumerators, so a host write stopped being a write the moment a new one appeared.
constexpr bool IsWrite(ResourceIOType io) { return io != ResourceIOType::READ; }

/// @return The name of an IO type, for diagnostics.
///
/// A switch with no default on purpose: adding a value becomes a warning here rather than a message
/// that prints a number.
constexpr const char* NameOf(ResourceIOType io)
{
    switch (io)
    {
        case ResourceIOType::READ:
            return "READ";
        case ResourceIOType::WRITE:
            return "WRITE";
        case ResourceIOType::READ_WRITE:
            return "READ_WRITE";
        case ResourceIOType::TRANSFER_WRITE:
            return "TRANSFER_WRITE";
        case ResourceIOType::HOST_WRITE:
            return "HOST_WRITE";
    }
    return "(unknown IO type)";
}

/// @param kind A resource kind.
/// @return Its lower-case name, for diagnostics.
///
/// An overload of `NameOf` rather than a second `NameOfKind`, because every diagnostic that names a kind wants
/// the same word and the existing overloads already cover a usage and an IO type.
inline const char* NameOf(ResourceKind kind)
{
    switch (kind)
    {
        case ResourceKind::IMAGE:
            return "image";
        case ResourceKind::BUFFER:
            return "buffer";
        case ResourceKind::ACCELERATION_STRUCTURE:
            return "acceleration structure";
        case ResourceKind::SAMPLER:
            return "sampler";
    }
    return "(unknown kind)";
}

/// @return The name of a usage, for diagnostics.
constexpr const char* NameOf(ResourceUsage usage)
{
    switch (usage)
    {
        case ResourceUsage::SAMPLED:
            return "SAMPLED";
        case ResourceUsage::STORAGE_IMAGE:
            return "STORAGE_IMAGE";
        case ResourceUsage::COLOR_ATTACHMENT:
            return "COLOR_ATTACHMENT";
        case ResourceUsage::DEPTH_STENCIL_ATTACHMENT:
            return "DEPTH_STENCIL_ATTACHMENT";
        case ResourceUsage::INPUT_ATTACHMENT:
            return "INPUT_ATTACHMENT";
        case ResourceUsage::RESOLVE_ATTACHMENT:
            return "RESOLVE_ATTACHMENT";
        case ResourceUsage::PRESENT:
            return "PRESENT";
        case ResourceUsage::UNIFORM_BUFFER:
            return "UNIFORM_BUFFER";
        case ResourceUsage::STORAGE_BUFFER:
            return "STORAGE_BUFFER";
        case ResourceUsage::VERTEX_BUFFER:
            return "VERTEX_BUFFER";
        case ResourceUsage::INDEX_BUFFER:
            return "INDEX_BUFFER";
        case ResourceUsage::INDIRECT_BUFFER:
            return "INDIRECT_BUFFER";
        case ResourceUsage::DRAW_COMMAND_BUFFER:
            return "DRAW_COMMAND_BUFFER";
        case ResourceUsage::TRANSFER_SOURCE:
            return "TRANSFER_SOURCE";
        case ResourceUsage::TRANSFER_DEST:
            return "TRANSFER_DEST";
        case ResourceUsage::ACCEL_STRUCTURE:
            return "ACCEL_STRUCTURE";
        case ResourceUsage::TASK_PAYLOAD:
            return "TASK_PAYLOAD";
        case ResourceUsage::MESH_PAYLOAD:
            return "MESH_PAYLOAD";
        case ResourceUsage::WORKGRAPH_NODE:
            return "WORKGRAPH_NODE";
        case ResourceUsage::PUSH_CONSTANTS:
            return "PUSH_CONSTANTS";
    }
    return "(unknown usage)";
}

/// Explicit shader descriptor location (set + binding) for a resource.
///
/// The graph derives a node's set layouts from the shader reflection and takes each resource's
/// location from its `ResourceUse`, so a descriptor-bound resource must name its (set, binding). The
/// engine's built-in set convention lives outside the library, in RenderGraphSemanticBindings.h.
struct DescriptorBinding
{
    uint32_t set = 0;      ///< Descriptor set index.
    uint32_t binding = 0;  ///< Binding index within the set.
};

/// A byte range inside a buffer resource - a view of part of it.
///
/// Declared on a `ResourceUse` to bind only `[offset, offset + size)`, which is what one tensor of a
/// packed weights buffer or a row of a KV cache needs. Without it the whole buffer is bound, which is
/// what every existing caller gets.
///
/// The range is resolved at `Build()`: it is written into `VkDescriptorBufferInfo` once, so changing it
/// means rebuilding the graph. A base that is only known per execution is a different mechanism, not a
/// different value of this one - see issue #81.
struct BufferRange
{
    uint64_t offset = 0;  ///< Byte offset from the start of the buffer.
    uint64_t size = 0;    ///< Byte length of the range. Must not be zero.
};

/// Validate a declared byte range against the buffer it addresses and a device alignment limit.
///
/// Free, and taking the alignment as an argument, for two reasons. It has to be callable from both
/// places a buffer is bound into a descriptor, which are different classes. And the failure modes are
/// worth testing on any machine: how much alignment a driver demands is a property of *that* device
/// (`minStorageBufferOffsetAlignment` may be far larger than the 4 bytes this machine requires), while
/// a free function with the limit passed in tests the rule without needing the device that exhibits
/// it.
///
/// Both checks exist because neither is caught where the mistake is made: an out-of-range range reads
/// or writes the wrong bytes, and a misaligned one is a validation error at bind time, far from the
/// line that declared it.
///
/// @param nodeName          Node that declared the range; named in the message.
/// @param handle            Buffer handle; named in the message.
/// @param range             The declared range.
/// @param bufferSize        Size of the addressed buffer, in bytes.
/// @param requiredAlignment Device limit for the binding's descriptor type. 1 disables the check.
/// @throws std::runtime_error if the range is zero-sized, out of bounds, or misaligned.
inline void ValidateBufferRange(const ResourceHandle& nodeName, const ResourceHandle& handle, const BufferRange& range,
                                uint64_t bufferSize, uint64_t requiredAlignment)
{
    if (range.size == 0)
    {
        throw std::runtime_error("Node '" + nodeName + "' binds buffer '" + handle +
                                 "' with a zero-sized range. Leave bufferRange unset to bind the whole buffer.");
    }
    if (range.offset > bufferSize || range.size > bufferSize - range.offset)
    {
        throw std::runtime_error("Node '" + nodeName + "' binds buffer '" + handle + "' at [" +
                                 std::to_string(range.offset) + ", " + std::to_string(range.offset + range.size) +
                                 ") which is outside the buffer's " + std::to_string(bufferSize) + " bytes.");
    }
    if (requiredAlignment > 1 && (range.offset % requiredAlignment) != 0)
    {
        throw std::runtime_error("Node '" + nodeName + "' binds buffer '" + handle + "' at offset " +
                                 std::to_string(range.offset) + ", which is not a multiple of this device's " +
                                 std::to_string(requiredAlignment) +
                                 "-byte descriptor offset alignment for that descriptor type.");
    }
}

/// A node's declaration that it uses a resource (the interface passed to RenderGraphBuilder).
struct ResourceUse
{
    ResourceHandle handle;                                                    ///< Name of the resource.
    ResourceIOType io;                                                        ///< Read / write / read-write.
    ResourceUsage usage;                                                      ///< Semantic role.
    ResourceKind kind;                                                        ///< Concrete resource kind.
    /// Descriptor location (set + binding), when the resource is bound into a descriptor set. The
    /// shader reflection supplies the layout and the descriptor type; this names the slot.
    std::optional<DescriptorBinding> descriptorBinding = std::nullopt;
    /// Optional byte range of a buffer resource to bind. Absent binds the whole buffer.
    std::optional<BufferRange> bufferRange = std::nullopt;
};

/// A `ResourceUse` after the graph has resolved it to concrete barrier state.
///
/// Produced by `ResolveResourceUse()`; mostly consumed internally by `RenderGraphBuilder`.
struct ResolvedResourceUse
{
    ResourceHandle handle;        ///< Name of the resource.
    ResourceVersion version = 0;  ///< Write version this use refers to.

    ResourceKind kind;  ///< Concrete resource kind.

    ResourceIOType io;    ///< Read / write / read-write.
    ResourceUsage usage;  ///< Semantic role.

    VkPipelineStageFlags2 stages;  ///< Pipeline stages that access the resource.
    VkAccessFlags2 access;         ///< Access mask for the resource.

    // Optional, depends on kind
    VkImageLayout imageLayout;  ///< Image layout (images only).
    VkFormat format;            ///< Optional format (validation / pipeline creation).
    VkExtent3D extent;          ///< Optional extent (validation / trace dispatch).

    std::optional<DescriptorBinding> descriptorBinding = std::nullopt;  ///< Descriptor location.
    /// Byte range of a buffer resource to bind, carried through from the declaration.
    ///
    /// Kept here because both consumers read this struct: the descriptor write needs it, and the
    /// barrier path deliberately ignores it - a range is a *binding* claim, not a claim that two uses
    /// touch disjoint bytes, so hazards stay per-resource and conservative.
    std::optional<BufferRange> bufferRange = std::nullopt;
};

}  // namespace Muyo::RenderGraph
