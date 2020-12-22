#pragma once
#include "Swapchain.h"

struct GLFWwindow;
class GLFWSwapchain : public Swapchain
{
public:
    GLFWSwapchain(GLFWwindow* pWindow) : m_pWindow(pWindow) {}
    void CreateSurface() override;
    void DestroySurface() override;
private:
    GLFWwindow* m_pWindow = nullptr;
};
