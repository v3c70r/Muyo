#pragma once
#include "RenderPass.h"

// Prototyping AO pass with compute queue
class RenderPassAO : public RenderPass
{
public:
    RenderPassAO();
    ~RenderPassAO() override;
    void RecordCommandBuffer();
    VkCommandBuffer GetCommandBuffer() const override
    {
        return m_cmdBuf;
    }
private:
    void CreateRenderPass();

    void CreatePipeline() override;


    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkCommandBuffer m_cmdBuf = VK_NULL_HANDLE;

    
};
