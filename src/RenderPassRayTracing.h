#pragma once
#include "RenderPass.h"

class RenderPassRayTracing : public RenderPass
{
public:
    virtual void CreatePipeline() override {}
    virtual VkCommandBuffer GetCommandBuffer(size_t idx = 0) const override
    {
        return m_commandBuffer;
    }
    void RecordCommandBuffer(VkExtent2D imgSize,
                             const VkStridedDeviceAddressRegionKHR* pRayGenRegion,
                             const VkStridedDeviceAddressRegionKHR* pRayMissRegion,
                             const VkStridedDeviceAddressRegionKHR* pRayHitRegion,
                             VkPipelineLayout rayTracingPipelineLayout,
                             VkPipeline rayTracingPipeline);

private:
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};
