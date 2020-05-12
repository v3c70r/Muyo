#pragma once
#include "RenderPass.h"
#include "VertexBuffer.h"
#include "Texture.h"
#include <memory>

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
    VkSampler sampler;
    VertexBuffer vertexBuffer;
    IndexBuffer indexBuffer;
	VkDeviceMemory fontMemory = VK_NULL_HANDLE;

    std::unique_ptr<Texture> pTexture = nullptr;

	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    int nTotalIndexCount = 0;

    void createResources(VkRenderPass UIRenderPass);
    void destroyResources();
};

class RenderPassUI : public RenderPassFinal
{
public:
    RenderPassUI(VkFormat swapChainFormat);
    void recordCommandBuffer(VkExtent2D screenExtent);
   ~RenderPassUI();

   // ImGui Related functions
   void newFrame(VkExtent2D screenExtent);
   void updateBuffers();

   private:
       struct PushConstBlock
       {
           glm::vec2 scale;
           glm::vec2 translate;
       };
   ImGuiResource m_uiResources;
};
