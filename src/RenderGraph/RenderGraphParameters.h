#pragma once
#include <optional>
#include "RenderGraphNodePipelineLayoutDesc.h"
#include "RenderGraphResourceHandle.h"
#include "RenderGraphNodeResource.h"

namespace Muyo::RenderGraph
{
enum class QueueType : uint8_t
{
    GRAPHICS,
    COMPUTE,
    COPY,
    CPU,
    COUNT
};
class RenderGraphNodeParameters
{
    friend class RenderGraphBuilder;

public:
    virtual void OnGraphBuild() {}
    virtual void OnGraphExecute() {}
    virtual ~RenderGraphNodeParameters() = default;

private:
    std::vector<ResourceUse> m_ResourceUses;
    std::optional<std::vector<VkPushConstantRange>> m_PushConstants;

    VkPipelineBindPoint m_BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    QueueType m_QueueType = QueueType::GRAPHICS;
};
}  // namespace Muyo::RenderGraph
