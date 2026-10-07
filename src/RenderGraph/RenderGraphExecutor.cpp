#include "RenderGraphExecutor.h"

#include <cstddef>
#include <map>
#include <utility>

#include "VkRenderDevice.h"

namespace Muyo::RenderGraph
{
RenderGraphExecutor::RenderGraphExecutor(VkRenderDevice* renderDevice)
    : m_renderDevice(renderDevice), m_device(renderDevice != nullptr ? renderDevice->GetDevice() : VK_NULL_HANDLE)
{
}

RenderGraphExecutor::~RenderGraphExecutor() { DestroyHandoverSemaphores(); }

void RenderGraphExecutor::DestroyHandoverSemaphores()
{
    for (VkSemaphore semaphore : m_handoverSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_handoverSemaphores.clear();
}

VkQueue RenderGraphExecutor::GetQueueForType(QueueType type) const
{
    if (type == QueueType::COMPUTE) return m_renderDevice->GetComputeQueue();
    return m_renderDevice->GetGraphicsQueue();
}

void RenderGraphExecutor::Submit(const RenderGraphExecutionPlan& plan,
                                 const std::vector<VkCommandBuffer>& segmentCommandBuffers)
{
    DestroyHandoverSemaphores();

    const std::vector<RenderGraphQueueSegment>& segments = plan.segments;
    const std::vector<RenderGraphQueueTransfer>& transfers = plan.transfers;

    // ── One semaphore per (producer, consumer) pair: a queue does not start the segment that
    //    consumes a resource until the segment that produced it has finished. ───────────────────
    std::map<std::pair<size_t, size_t>, VkSemaphore> transitionSemaphores;
    for (const auto& transfer : transfers)
    {
        const auto key = std::make_pair(transfer.producer, transfer.consumer);
        if (transitionSemaphores.find(key) == transitionSemaphores.end())
        {
            VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore semaphore = VK_NULL_HANDLE;
            VK_ASSERT(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore));
            transitionSemaphores[key] = semaphore;
            m_handoverSemaphores.push_back(semaphore);
        }
    }

    for (size_t s = 0; s < segments.size(); ++s)
    {
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags2> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        for (const auto& [key, semaphore] : transitionSemaphores)
        {
            if (key.second == s)
            {
                waitSemaphores.push_back(semaphore);
                waitStages.push_back(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
            }
            if (key.first == s)
            {
                signalSemaphores.push_back(semaphore);
            }
        }

        std::vector<VkSemaphoreSubmitInfo> waitSemaphoreInfos(waitSemaphores.size());
        for (size_t i = 0; i < waitSemaphores.size(); ++i)
        {
            waitSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemaphoreInfos[i].semaphore = waitSemaphores[i];
            waitSemaphoreInfos[i].value = 0;  // ignored for binary semaphores
            waitSemaphoreInfos[i].stageMask = waitStages[i];
            waitSemaphoreInfos[i].deviceIndex = 0;
        }
        std::vector<VkSemaphoreSubmitInfo> signalSemaphoreInfos(signalSemaphores.size());
        for (size_t i = 0; i < signalSemaphores.size(); ++i)
        {
            signalSemaphoreInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemaphoreInfos[i].semaphore = signalSemaphores[i];
            signalSemaphoreInfos[i].value = 0;  // ignored for binary semaphores
            signalSemaphoreInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalSemaphoreInfos[i].deviceIndex = 0;
        }
        VkCommandBufferSubmitInfo commandBufferInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandBufferInfo.commandBuffer = segmentCommandBuffers[s];
        commandBufferInfo.deviceMask = 0;

        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitSemaphoreInfos.size());
        submitInfo.pWaitSemaphoreInfos = waitSemaphoreInfos.empty() ? nullptr : waitSemaphoreInfos.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreInfos.size());
        submitInfo.pSignalSemaphoreInfos = signalSemaphoreInfos.empty() ? nullptr : signalSemaphoreInfos.data();

        VK_ASSERT(vkQueueSubmit2(GetQueueForType(segments[s].queueType), 1, &submitInfo, VK_NULL_HANDLE));
    }
}

void RenderGraphExecutor::WaitIdle()
{
    vkQueueWaitIdle(m_renderDevice->GetGraphicsQueue());
    if (m_renderDevice->IsComputeQueueDedicated())
    {
        vkQueueWaitIdle(m_renderDevice->GetComputeQueue());
    }
}
}  // namespace Muyo::RenderGraph
