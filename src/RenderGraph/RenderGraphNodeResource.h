#pragma once
#include <cstdint>

#include "RenderGraphResourceHandle.h"
#include "vulkan/vulkan_core.h"

namespace Muyo::RenderGraph
{
enum class ResourceKind : uint8_t
{
    IMAGE,
    BUFFER,
    ACCELERATION_STRUCTURE,
    SAMPLER
};

enum class ResourceIOType : uint8_t
{
    READ,
    WRITE,
    READ_WRITE
};

enum class ResourceUsage : uint8_t
{
    // Images
    SAMPLED,
    STORAGE_IMAGE,
    COLOR_ATTACHMENT,
    DEPTH_STENCIL_ATTACHMENT,
    INPUT_ATTACHMENT,
    RESOLVE_ATTACHMENT,

    // Buffers
    UNIFORM_BUFFER,
    STORAGE_BUFFER,
    VERTEX_BUFFER,
    INDEX_BUFFER,
    INDIRECT_BUFFER,
    DRAW_COMMAND_BUFFER,

    // RT / Mesh / Work Graph
    ACCEL_STRUCTURE,
    TASK_PAYLOAD,
    MESH_PAYLOAD,
    WORKGRAPH_NODE,

    // Special
    PUSH_CONSTANTS,
};
using ResourceVersion = uint64_t;

enum class ResourceBindingSemantic : uint8_t
{
    NONE,
    PER_VIEW,
    PER_OBJ,
    MATERIAL_PARAM,
    MATERIAL_TEXTURES,
};

// Interface ResourceUse that passed into RenderGraphBuilder
struct ResourceUse
{
    ResourceHandle handle;
    ResourceIOType io;
    ResourceUsage usage;
    ResourceKind kind;
    ResourceBindingSemantic bindingSemantic = ResourceBindingSemantic::NONE;
};

// Resolved ResourceUse used within RenderGraph
struct ResolvedResourceUse
{
    ResourceHandle handle;
    ResourceVersion version = 0;

    ResourceKind kind;

    ResourceIOType io;    // read / write / rw
    ResourceUsage usage;  // semantic role

    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;

    // Optional, depends on kind
    VkImageLayout imageLayout;  // if image
    VkFormat format;            // optional validation
    VkExtent3D extent;          // optional
};

}  // namespace Muyo::RenderGraph
