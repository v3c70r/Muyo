#pragma once
#include <memory>

#include "Geometry.h"
#include "RenderPass.h"
#include "Texture.h"

class RenderPassIBL : public RenderPass
{
    // Generate convoluted irradiance map
public:
    RenderPassIBL();
    ~RenderPassIBL();
    void initializeIBLResources();
    void initializeIBLPipeline();
    void destroyIBLPipeline();
    void destroyFramebuffer();
    void setEnvMap(const std::string path)
    {
        m_texEnvMap.LoadImage(path);
    }

    void generateIrradianceCube();

    VkCommandBuffer GetCommandBuffer() const
    {
        return m_commandBuffer;
    }

private:
    const uint32_t CUBE_DIM = 64;
    //const uint32_t NUM_FACES = 1;
    //const uint32_t NUM_MIP = 1;//(uint32_t)std::floor(std::log2(CUBE_DIM))+1;
    const VkFormat TEX_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {0, 0};
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    Texture m_texEnvMap;
    std::unique_ptr<Geometry> m_pSkybox = nullptr;
};

