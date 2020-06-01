#pragma once
#include "Geometry.h"
#include "RenderPass.h"

class RenderPassIBL : public RenderPass
{
    // Generate convoluted irradiance map
public:
    RenderPassIBL();
    ~RenderPassIBL();
    void initializeIBLConvolutedMapView();
    void initializeIBLPipeline();
    void destroyIBLPipeline();
    void destroyFramebuffer();

    void recordCommandBuffer(const PrimitiveList& primitives,
                             VkPipeline pipeline,
                             VkPipelineLayout pipelineLayout,
                             VkDescriptorSet descriptorSet);

private:
    const uint32_t CUBE_DIM = 64;
    //const uint32_t NUM_FACES = 1;
    //const uint32_t NUM_MIP = 1;//(uint32_t)std::floor(std::log2(CUBE_DIM))+1;
    const VkFormat TEX_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {0, 0};
};

