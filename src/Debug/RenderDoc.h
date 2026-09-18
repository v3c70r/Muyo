#pragma once
#define RENDERDOC_API_ENABLED
#include <renderdoc_app.h>

#include <atomic>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Returns the RenderDoc API if the library is injected into the current process, otherwise
// nullptr. Safe to call repeatedly; the dynamic library handle is only looked up once.
inline RENDERDOC_API_1_6_0* GetRenderDocAPI()
{
    static RENDERDOC_API_1_6_0* s_api = []() -> RENDERDOC_API_1_6_0*
    {
        RENDERDOC_API_1_6_0* api = nullptr;
#if defined(_WIN32)
        if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
        {
            auto getApi = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
            if (getApi && getApi(eRENDERDOC_API_Version_1_6_0, (void**)&api) == 1)
            {
                return api;
            }
        }
#else
        if (void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
        {
            auto getApi = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
            if (getApi && getApi(eRENDERDOC_API_Version_1_6_0, (void**)&api) == 1)
            {
                return api;
            }
        }
#endif
        return nullptr;
    }();
    return s_api;
}

// RAII helper around a RenderDoc frame capture.
//
// Every capture is scoped to a single "frame" (the work submitted between construction and
// destruction). RenderDoc explicitly forbids overlapping captures, so this helper keeps a
// process-wide count and only the outermost instance actually drives Start/EndFrameCapture.
// Inner instances are no-ops beyond keeping the frame alive.
class RenderDocScopedCapture
{
public:
    // `name` is used as the capture file path template (captures are written to
    // "<name>_frame<N>.rdc") and as the capture title shown in the RenderDoc UI.
    // It is only applied by the outermost capture of a frame.
    explicit RenderDocScopedCapture(const std::string& name = "")
        : m_api(GetRenderDocAPI())
    {
        if (!m_api) return;

        // Only the outermost capture owns the frame; RenderDoc crashes on overlapping captures.
        const uint32_t previous = s_activeCaptures.fetch_add(1);
        m_isOutermost = (previous == 0);

        if (m_isOutermost)
        {
            if (!name.empty())
            {
                m_api->SetCaptureFilePathTemplate(name.c_str());
                // SetCaptureTitle labels the capture shown in the RenderDoc UI. It applies to the
                // capture that ends next, i.e. this outermost one.
                m_api->SetCaptureTitle(name.c_str());
            }
            m_api->StartFrameCapture(nullptr, nullptr);
        }
    }

    ~RenderDocScopedCapture()
    {
        if (m_api && m_isOutermost)
        {
            const uint32_t nSuccess = m_api->EndFrameCapture(nullptr, nullptr);
            if (nSuccess == 0)
            {
                std::fprintf(stderr, "[RenderDoc] EndFrameCapture failed; no capture was written.\n");
            }
        }
        if (m_api)
        {
            s_activeCaptures.fetch_sub(1);
        }
    }

    RenderDocScopedCapture(const RenderDocScopedCapture&) = delete;
    RenderDocScopedCapture& operator=(const RenderDocScopedCapture&) = delete;
    RenderDocScopedCapture& operator=(RenderDocScopedCapture&&) = delete;
    RenderDocScopedCapture(RenderDocScopedCapture&&) = delete;

    // True while a frame capture is in progress (including captures started by an outer scope).
    bool IsCapturing() const { return m_api != nullptr && m_api->IsFrameCapturing() != 0; }

    // True when RenderDoc is injected and can produce captures at all.
    bool IsAvailable() const { return m_api != nullptr; }

private:
    RENDERDOC_API_1_6_0* m_api = nullptr;
    bool m_isOutermost = false;

    // Number of live scopes across the process. Only the first one starts a capture.
    inline static std::atomic<uint32_t> s_activeCaptures{0};
};
