#pragma once
#include <memory>

#include "RenderPass.h"
#include "Texture.h"
#include "VertexBuffer.h"

// Options and values to display/toggle from the UI
struct UISettings
{
    bool displayModels = true;
    bool displayLogos = true;
    bool displayBackground = true;
    bool animateLight = false;
    float lightSpeed = 0.25f;
    std::array<float, 50> frameTimes{};
    float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
    float lightTimer = 0.0f;
};
class Texture;
struct ImGuiResource
{
    // Vertex buffer and index buffer can be updated each frame
    std::vector<VertexBuffer> vertexBuffers;
    std::vector<IndexBuffer> indexBuffers;

    VkSampler sampler;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;

    std::unique_ptr<Texture> pTexture = nullptr;

    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    //VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    //VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    int nTotalIndexCount = 0;

    void createResources(VkRenderPass UIRenderPass, uint32_t numSwapchainBuffers);
    void destroyResources();
};

class RenderPassUI : public RenderPassFinal
{
public:
    RenderPassUI(VkFormat swapChainFormat);
    void recordCommandBuffer(VkExtent2D screenExtent, uint32_t nBufferIdx);
    ~RenderPassUI() override;

    // ImGui Related functions
    void newFrame(VkExtent2D screenExtent);
    void updateBuffers(uint32_t nSwapchainBufferIndex);
    virtual void setSwapchainImageViews(std::vector<VkImageView>& vImageViews,
                                        VkImageView depthImageView,
                                        uint32_t nWidth, uint32_t nHeight) override;
protected:
    void setupPipeline() override;

private:
    struct PushConstBlock
    {
        glm::vec2 scale;
        glm::vec2 translate;
    };
    ImGuiResource m_uiResources;
};
