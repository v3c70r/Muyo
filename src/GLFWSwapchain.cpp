#include "GLFWSwapchain.h"
#include "VkRenderDevice.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <cassert>


void GLFWSwapchain::CreateSurface()
{
    assert(glfwCreateWindowSurface(GetRenderDevice()->GetInstance(), m_pWindow,
                                   nullptr, &m_surface) == VK_SUCCESS);
}

void GLFWSwapchain::DestroySurface()
{
    vkDestroySurfaceKHR(GetRenderDevice()->GetInstance(), m_surface, nullptr);
}
