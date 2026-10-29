#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "RenderPassParameters.h"

namespace Muyo
{
class Geometry;

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;
    virtual VkCommandBuffer GetCommandBuffer() const = 0;
    virtual void CreatePipeline() = 0;
    virtual void PrepareRenderPass() = 0;
    virtual const std::vector<const IRenderResource*>& GetInputResources() const = 0;
    virtual const std::vector<const IRenderResource*>& GetOutputResources() const = 0;
    virtual std::string GetName() const = 0;
};

class RenderPass : public IRenderPass
{
public:
    void PrepareRenderPass() override {};
    const std::vector<const IRenderResource*>& GetInputResources() const override
    {
        return m_renderPassParameters.GetInputResources();
    }
    const std::vector<const IRenderResource*>& GetOutputResources() const override
    {
        return m_renderPassParameters.GetOutputResources();
    }
    std::string GetName() const override { return m_renderPassParameters.GetName(); }

protected:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    RenderPassParameters m_renderPassParameters;
};

}  // namespace Muyo
