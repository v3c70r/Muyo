#pragma once
#include <memory>

#include "Geometry.h"
#include "RenderPass.h"
#include "Texture.h"

class RenderPassIBL : public RenderPass
{
    struct IBLPassResource
    {
    };
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
        Texture equirectangularMap;
        equirectangularMap.LoadImage(path);
        // TODO: Convert this texture to cubemap

    }

    void generateIrradianceCube();

    VkCommandBuffer GetCommandBuffer() const
    {
        return m_commandBuffer;
    }

private:
    const uint32_t CUBE_DIM = 64;
    const uint32_t NUM_FACES = 6;
    const uint32_t NUM_MIP = (uint32_t)std::floor(std::log2(CUBE_DIM))+1;
    const VkFormat TEX_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkExtent2D m_renderArea = {0, 0};
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    std::unique_ptr<Geometry> m_pSkybox = nullptr;

    void envMapToCubemap(VkImageView envMap);
    Texture m_texEnvCupMap;
    VkPipeline envToCubeMapPipeline = VK_NULL_HANDLE;
};

