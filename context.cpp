#include "context.hpp"
#include <array>
#include "assert.h"
void RenderContext::init(size_t numBuffers, VkDevice *pDevice, VkCommandPool* pPool)
{
    m_pCommandPool = pPool;
    m_pDevice = pDevice;

    m_commandBuffers.resize(numBuffers);
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = *m_pCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();
    assert(vkAllocateCommandBuffers(*m_pDevice, &cmdAllocInfo,
                                    m_commandBuffers.data()) == VK_SUCCESS);

    m_cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    m_cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    m_cmdBufferBeginInfo.pInheritanceInfo = nullptr;
    m_recording.resize(numBuffers, false);
}

void RenderContext::finalize()
{
    vkFreeCommandBuffers(*m_pDevice, *m_pCommandPool,
                         static_cast<uint32_t>(m_commandBuffers.size()),
                         m_commandBuffers.data());
    m_commandBuffers.clear();
    m_pCommandPool = nullptr;
    m_pDevice = nullptr;
    m_recording.clear();
}
void RenderContext::startRecording()
{
    VkCommandBuffer cmdBuffer = getCommandBuffer();
    vkBeginCommandBuffer(cmdBuffer, &m_cmdBufferBeginInfo);
    m_recording[s_currentContext] = true;
}

void RenderContext::endRecording()
{
    assert(isRecording());
    vkEndCommandBuffer(getCommandBuffer());
    m_recording[s_currentContext] = false;
}
void RenderContext::beginPass(VkRenderPass& renderPass, VkFramebuffer& frameBuffer,
                        VkExtent2D& extent)
{
    assert(isRecording());

    // TODO: Move pass begin info class member
    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.framebuffer = frameBuffer;

    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = extent;

    std::array<VkClearValue, 2> clearValues = {};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassBeginInfo.clearValueCount =
        static_cast<uint32_t>(clearValues.size());
    renderPassBeginInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(getCommandBuffer(), &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
}
void RenderContext::endPass()
{
    assert(isRecording());
    vkCmdEndRenderPass(getCommandBuffer());
}
