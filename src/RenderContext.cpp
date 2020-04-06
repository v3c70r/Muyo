#include "RenderContext.h"
#include <array>
#include "assert.h"
void RenderContext::initialize(size_t numBuffers, VkDevice *pDevice, VkCommandPool* pPool)
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
