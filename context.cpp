#include "vkContext.h"
#include "assert.h"
static VkDevice s_device;
void Context::init(size_t numBuffers, VkCommandPool* pPool)
{
    m_pCommandPool = pPool;

    m_commandBuffers.resize(numBuffers);
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = *m_pCommandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();
    assert(vkAllocateCommandBuffers(s_device, &cmdAllocInfo,
                                    m_commandBuffers.data()) == VK_SUCCESS);
}

void Context::finalize()
{

    vkFreeCommandBuffers(s_device, *m_pCommandPool,
                         static_cast<uint32_t>(m_commandBuffers.size()),
                         m_commandBuffers.data());
    m_commandBuffers.clear();
    m_pCommandPool = nullptr;
}


