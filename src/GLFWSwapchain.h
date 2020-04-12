#pragma once
#include "Swapchain.h"

class GLFWwindow;
class GLFWSwapchain : public Swapchain
{
public:
    GLFWSwapchain(GLFWwindow* pWindow) : m_pWindow(pWindow) {}
    void createSurface() override;
    void destroySurface() override;
private:
    GLFWwindow* m_pWindow = nullptr;
};
