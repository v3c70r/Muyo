#pragma once
#include <stdexcept>
#include <vulkan/vulkan.h>

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
struct UsageKey
{
    ResourceUsage usage;
    ResourceIOType io;

    constexpr bool operator==(const UsageKey&) const = default;
};

struct UsagePolicy
{
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    VkImageLayout layout;  // ignored for buffers / AS / push constants
};

constexpr std::array<std::pair<UsageKey, UsagePolicy>, 25> K_USAGE_POLICIES = {{
    // ───────────── Images ─────────────
    {
        {ResourceUsage::SAMPLED, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
         VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    },
    {
        {ResourceUsage::STORAGE_IMAGE, ResourceIOType::READ_WRITE},
        {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL}
    },
    {
        {ResourceUsage::COLOR_ATTACHMENT, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    },
    {
        {ResourceUsage::COLOR_ATTACHMENT, ResourceIOType::READ_WRITE},
        {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    },
    {
        {ResourceUsage::DEPTH_STENCIL_ATTACHMENT, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    },
    {
        {ResourceUsage::INPUT_ATTACHMENT, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    },
    {
        {ResourceUsage::RESOLVE_ATTACHMENT, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    },

    // ───────────── Buffers ─────────────
    {
        {ResourceUsage::UNIFORM_BUFFER, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::UNIFORM_BUFFER, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::STORAGE_BUFFER, ResourceIOType::READ_WRITE},
        {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::STORAGE_BUFFER, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::STORAGE_BUFFER, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::VERTEX_BUFFER, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::VERTEX_BUFFER, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::INDEX_BUFFER, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::INDEX_BUFFER, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::INDIRECT_BUFFER, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::INDIRECT_BUFFER, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    // ───────────── Draw Command / Indirect Buffers ─────────────
    // The Read case
    {
        {ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    // GPU Write case (e.g., a Compute shader generating draw calls)
    {
        {ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::WRITE},
        {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    // Transfer/CPU Write case (e.g., vkCmdCopyBuffer or Host mapping)
    {
        {ResourceUsage::DRAW_COMMAND_BUFFER, ResourceIOType::TRANSFER_WRITE},
        {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    // ───────────── Ray Tracing / Mesh / Work Graph ─────────────
    {
        {ResourceUsage::ACCEL_STRUCTURE, ResourceIOType::READ},
        {VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::TASK_PAYLOAD, ResourceIOType::READ_WRITE},
        {VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::MESH_PAYLOAD, ResourceIOType::READ_WRITE},
        {VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    },
    {
        {ResourceUsage::WORKGRAPH_NODE, ResourceIOType::READ_WRITE},
        {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED}
    }
}};

constexpr UsagePolicy ResolvePolicy(UsageKey key)
{
    for (auto&& [k, v] : K_USAGE_POLICIES)
    {
        if (k == key) return v;
    }
    throw "Missing ResourceUsage policy";
}

ResolvedResourceUse ResolveResourceUse(const ResourceUse& use)
{
    // Special handling
    if (use.usage == ResourceUsage::PUSH_CONSTANTS)
    {
        throw std::logic_error("Push constants are not resolved as resources");
    }

    const UsagePolicy policy = ResolvePolicy({use.usage, use.io});

    ResolvedResourceUse resolved{};
    resolved.handle = use.handle;
    resolved.version = 0;
    resolved.kind = use.kind;
    resolved.io = use.io;
    resolved.usage = use.usage;
    resolved.stages = policy.stages;
    resolved.access = policy.access;
    resolved.bindingSemantic = use.bindingSemantic;

    if (use.kind == ResourceKind::IMAGE)
    {
        resolved.imageLayout = policy.layout;
    }

    return resolved;
}
}  // namespace Muyo::RenderGraph
