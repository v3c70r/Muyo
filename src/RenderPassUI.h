#pragma once
#include <memory>
#include <imgui.h>

#include "RenderPass.h"
#include "Texture.h"
#include "VertexBuffer.h"
#include "DebugUI.h"

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
struct UIVertex;
struct ImGuiResource
{
    // Vertex buffer and index buffer can be updated each frame
    std::vector<VertexBuffer<ImDrawVert>*> vpVertexBuffers;
    std::vector<IndexBuffer*> vpIndexBuffers;

    VkSampler sampler;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;


    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    //VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    //VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    int nTotalIndexCount = 0;

    void createResources(uint32_t numSwapchainBuffers);
};

class RenderPassUI : public RenderPassFinal
{
public:
    RenderPassUI(VkFormat swapChainFormat);
    void RecordCommandBuffer(VkExtent2D screenExtent, uint32_t nBufferIdx);
    ~RenderPassUI() override;

    // ImGui Related functions
    void newFrame(VkExtent2D screenExtent);
    void updateBuffers(uint32_t nSwapchainBufferIndex);
    void CreateImGuiResources();
    template <class DebugPageType>
    DebugPageType* RegisterDebugPage(const std::string& sName)
    {
        m_vpDebugPages.emplace_back(new DebugPageType(sName));
        return static_cast<DebugPageType*>(m_vpDebugPages.back().get());
    }
    virtual void CreatePipeline() override;


private:
    struct PushConstBlock
    {
        glm::vec2 scale;
        glm::vec2 translate;
    };
    ImGuiResource m_uiResources;
    std::vector<std::unique_ptr<IDebugUIPage>> m_vpDebugPages;
};
