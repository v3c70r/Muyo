#pragma once
#include "DescriptorManager.h"
#include "RenderPass.h"

namespace Muyo
{
class RenderPassRayTracing : public IRenderPass
{
public:
    RenderPassRayTracing(VkExtent2D imageSize) : m_imageSize(imageSize) {}
    virtual ~RenderPassRayTracing() { DestroyPipeline(); }
    virtual void CreatePipeline() override;
    virtual void PrepareRenderPass() override;
    virtual VkCommandBuffer GetCommandBuffer() const override
    {
        return m_commandBuffer;
    }
    void RecordCommandBuffer();

    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }
    VkPipeline GetPipeline() const { return m_pipeline; }
    void DestroyPipeline();

private:
    void AllocateShaderBindingTable();
    const VkStridedDeviceAddressRegionKHR* GetRayGenRegion() const { return &m_aSBTRegions[SBT_REGION_RAY_GEN]; }
    const VkStridedDeviceAddressRegionKHR* GetRayMissRegion() const { return &m_aSBTRegions[SBT_REGION_RAY_MISS]; }
    const VkStridedDeviceAddressRegionKHR* GetRayHitRegion() const { return &m_aSBTRegions[SBT_REGION_RAY_HIT]; }

    VkExtent2D m_imageSize;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    enum SBTRegions
    {
        SBT_REGION_RAY_GEN = 0,
        SBT_REGION_RAY_MISS,
        SBT_REGION_RAY_HIT,
        SBT_REGION_CALLABLE,
        SBT_REGION_COUNT

    };
    std::array<VkStridedDeviceAddressRegionKHR, SBT_REGION_COUNT> m_aSBTRegions;
    RenderPassParameters m_renderPassParameters;
};
}  // namespace Muyo
