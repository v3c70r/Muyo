#pragma once
#include <imgui.h>

#include <memory>

#include "DebugUI.h"
#include "RenderPass.h"
#include "Texture.h"
#include "VertexBuffer.h"

namespace Muyo
{
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
    VertexBuffer<ImDrawVert>* pVertexBuffers;
    IndexBuffer* pIndexBuffers;

    VkSampler sampler;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;

    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    // VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    // VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    int nTotalIndexCount = 0;

    void CreateResources();
};

class RenderPassUI : public RenderPass
{
public:
  explicit RenderPassUI(const VkExtent2D& renderArea);
  ~RenderPassUI() override;

  void PrepareRenderPass() override;
  void CreatePipeline() override;
  void RecordCommandBuffer();
  VkCommandBuffer GetCommandBuffer() const override { return m_commandBuffer; }

  // ImGui Related functions
  void NewFrame(VkExtent2D screenExtent);
  void UpdateBuffers();
  void CreateImGuiResources();
  template<class DebugPageType>
  DebugPageType* RegisterDebugPage(const std::string& sName)
  {
      m_vpDebugPages.emplace_back(new DebugPageType(sName));
      return static_cast<DebugPageType*>(m_vpDebugPages.back().get());
  }

private:
    struct PushConstBlock
    {
        glm::vec2 scale;
        glm::vec2 translate;
    };
    ImGuiResource m_uiResources;
    std::vector<std::unique_ptr<IDebugUIPage>> m_vpDebugPages;

    VkExtent2D m_renderArea;
    VkPipeline m_pipeline           = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};
}  // namespace Muyo
