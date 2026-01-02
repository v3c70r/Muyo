#pragma once
#define RENDERDOC_API_ENABLED
#include <renderdoc_app.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static RENDERDOC_API_1_6_0* LoadRenderDocAPI()
{
    RENDERDOC_API_1_6_0* api = nullptr;

#if defined(_WIN32)
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        auto getApi = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        if (getApi) getApi(eRENDERDOC_API_Version_1_6_0, (void**)&api);
    }
#else
    if (void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
    {
        auto getApi = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
        if (getApi) getApi(eRENDERDOC_API_Version_1_6_0, (void**)&api);
    }
#endif

    return api;
}

class RenderDocScopedCapture
{
public:
    explicit RenderDocScopedCapture(const char* name = nullptr) : m_api(LoadRenderDocAPI())
    {
        if (!m_api)
            return;

        if (name)
            m_api->SetCaptureFilePathTemplate(name);

        if (!m_api->IsFrameCapturing())
        {
            m_api->StartFrameCapture(nullptr, nullptr);
            m_active = true;
        }
    }

    ~RenderDocScopedCapture()
    {
        if (m_api && m_active)
        {
            m_api->EndFrameCapture(NULL, NULL);
        }
    }

    // Non-copyable, movable
    RenderDocScopedCapture(const RenderDocScopedCapture&) = delete;
    RenderDocScopedCapture& operator=(const RenderDocScopedCapture&) = delete;
    RenderDocScopedCapture(RenderDocScopedCapture&&) = default;
    RenderDocScopedCapture& operator=(RenderDocScopedCapture&&) = default;

    // Check if active
    bool IsCapturing() const { return m_api ? m_api->IsFrameCapturing() : false; }
private:
    RENDERDOC_API_1_6_0* m_api = nullptr;
    bool m_active = false;
};
