#pragma once

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "EventSystem.h"

namespace Window
{

/// Why `GetVulkanInstanceExtensions` did not return the window's WSI extensions.
enum class VulkanInstanceExtensionStatus
{
    Success,
    /// GLFW has not been initialised (or its initialisation failed); there is no platform to ask.
    NotInitialized,
    /// GLFW could not load or query the Vulkan loader; its error is included in the message.
    LoaderUnavailable,
    /// GLFW selected a platform, but the loader did not advertise that platform's WSI extension.
    MissingWsiExtension,
};

namespace GLFWDetail
{
static GLFWwindow *s_pWindow = nullptr;
static GLFWcursor *s_pCursors[Input::Cursor::CURSOR_COUNT];

// get time in milliseconds
static inline uint32_t Time() { return (glfwGetTimerValue() * 1000) / glfwGetTimerFrequency(); }

/// The GLFW platform selected by `glfwInit`; 0 when it has not run, -1 on GLFW versions that cannot say.
static int CurrentPlatform()
{
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    return glfwGetPlatform();
#else
    return -1;
#endif
}

static const char *PlatformName(int platform)
{
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    switch (platform)
    {
        case GLFW_PLATFORM_WIN32:
            return "Win32";
        case GLFW_PLATFORM_COCOA:
            return "Cocoa";
        case GLFW_PLATFORM_WAYLAND:
            return "Wayland";
        case GLFW_PLATFORM_X11:
            return "X11";
        case GLFW_PLATFORM_NULL:
            return "Null";
        default:
            break;
    }
#endif
    return "unknown";
}

/// The loader-provided WSI extension GLFW needs for the platform it selected.
static const char *PlatformWsiExtension(int platform)
{
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    switch (platform)
    {
        case GLFW_PLATFORM_WAYLAND:
            return "VK_KHR_wayland_surface";
        case GLFW_PLATFORM_X11:
            // GLFW prefers XCB when libX11-xcb is present and falls back to XLIB otherwise.
            return "VK_KHR_xcb_surface or VK_KHR_xlib_surface";
        case GLFW_PLATFORM_WIN32:
            return "VK_KHR_win32_surface";
        case GLFW_PLATFORM_COCOA:
            return "VK_EXT_metal_surface or VK_MVK_macos_surface";
        case GLFW_PLATFORM_NULL:
            return "VK_EXT_headless_surface";
        default:
            break;
    }
#endif
    return "the platform's Vulkan surface extension";
}

static const char *GlfwErrorName(int code)
{
    switch (code)
    {
        case GLFW_NOT_INITIALIZED:
            return "GLFW_NOT_INITIALIZED";
        case GLFW_NO_CURRENT_CONTEXT:
            return "GLFW_NO_CURRENT_CONTEXT";
        case GLFW_INVALID_ENUM:
            return "GLFW_INVALID_ENUM";
        case GLFW_INVALID_VALUE:
            return "GLFW_INVALID_VALUE";
        case GLFW_OUT_OF_MEMORY:
            return "GLFW_OUT_OF_MEMORY";
        case GLFW_API_UNAVAILABLE:
            return "GLFW_API_UNAVAILABLE";
        case GLFW_VERSION_UNAVAILABLE:
            return "GLFW_VERSION_UNAVAILABLE";
        case GLFW_PLATFORM_ERROR:
            return "GLFW_PLATFORM_ERROR";
        case GLFW_FORMAT_UNAVAILABLE:
            return "GLFW_FORMAT_UNAVAILABLE";
        case GLFW_NO_WINDOW_CONTEXT:
            return "GLFW_NO_WINDOW_CONTEXT";
#ifdef GLFW_CURSOR_UNAVAILABLE
        case GLFW_CURSOR_UNAVAILABLE:
            return "GLFW_CURSOR_UNAVAILABLE";
#endif
#ifdef GLFW_FEATURE_UNAVAILABLE
        case GLFW_FEATURE_UNAVAILABLE:
            return "GLFW_FEATURE_UNAVAILABLE";
#endif
#ifdef GLFW_FEATURE_UNIMPLEMENTED
        case GLFW_FEATURE_UNIMPLEMENTED:
            return "GLFW_FEATURE_UNIMPLEMENTED";
#endif
#ifdef GLFW_PLATFORM_UNAVAILABLE
        case GLFW_PLATFORM_UNAVAILABLE:
            return "GLFW_PLATFORM_UNAVAILABLE";
#endif
        default:
            return "unknown GLFW error";
    }
}

/// Report the error GLFW recorded for a failed call. GLFW's own message is the only cause information
/// it exposes, so this is the difference between a named failure and a process that exits silently.
static void ReportGlfwError(const char *what)
{
    const char *description = nullptr;
    const int code = glfwGetError(&description);
    if (code == GLFW_NO_ERROR)
    {
        std::fprintf(stderr, "WindowGLFW: %s failed; GLFW recorded no error\n", what);
        return;
    }
    std::fprintf(stderr, "WindowGLFW: %s failed: %s (0x%08X): %s\n", what, GlfwErrorName(code),
                 static_cast<unsigned>(code), description ? description : "no description");
}

/// event handlers
static Input::Key GetEditKey(int key)
{
    switch (key)
    {
        case GLFW_KEY_ENTER:
            return Input::KEY_RETURN;
        case GLFW_KEY_ESCAPE:
            return Input::KEY_ESCAPE;
        case GLFW_KEY_BACKSPACE:
            return Input::KEY_BACKSPACE;
        case GLFW_KEY_TAB:
            return Input::KEY_TAB;
        case GLFW_KEY_DELETE:
            return Input::KEY_DELETE;
        default:
            return Input::KEY_UNKNOWN;
    }
}

static Input::Key GetFunctionKey(int key)
{
    switch (key)
    {
        case GLFW_KEY_CAPS_LOCK:
            return Input::KEY_CAPSLOCK;
        case GLFW_KEY_PRINT_SCREEN:
            return Input::KEY_PRINTSCREEN;
        case GLFW_KEY_SCROLL_LOCK:
            return Input::KEY_SCROLLLOCK;
        case GLFW_KEY_PAUSE:
            return Input::KEY_PAUSE;
        case GLFW_KEY_INSERT:
            return Input::KEY_INSERT;
        case GLFW_KEY_HOME:
            return Input::KEY_HOME;
        case GLFW_KEY_PAGE_UP:
            return Input::KEY_PAGEUP;
        case GLFW_KEY_PAGE_DOWN:
            return Input::KEY_PAGEDOWN;
        case GLFW_KEY_RIGHT:
            return Input::KEY_RIGHT;
        case GLFW_KEY_LEFT:
            return Input::KEY_LEFT;
        case GLFW_KEY_DOWN:
            return Input::KEY_DOWN;
        case GLFW_KEY_UP:
            return Input::KEY_UP;
            // rest are the modifiers we dont care about
        default:
            return Input::KEY_UNKNOWN;
    }
}

static void KeyStrokeCallback(GLFWwindow * /*window*/, int key, int /*scancode*/, int action, int mods)
{
    auto pKeyEvent = EventSystem::sys()->globalEvent<EventType::KEY, GlobalKeyEvent>();
    EventState state = (action == GLFW_PRESS) ? EventState::PRESSED : EventState::RELEASED;
    uint16_t modifiers = Input::MOD_NONE;
    Input::Key press = Input::Key::KEY_UNKNOWN;

    // get mod
    if (mods & GLFW_MOD_ALT) modifiers |= Input::MOD_ALT;
    if (mods & GLFW_MOD_SHIFT) modifiers |= Input::MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) modifiers |= Input::MOD_CTRL;
    if (mods & GLFW_MOD_SUPER) modifiers |= Input::MOD_META;

    // One lookup fallback rather than an assignment inside each condition: the same keys, without the
    // duplicated emit body or the assignment-in-condition idiom (clang-tidy bugprone-branch-clone and
    // -assignment-in-if-condition).
    press = GetEditKey(key);
    if (press == Input::KEY_UNKNOWN) press = GetFunctionKey(key);
    if (press != Input::KEY_UNKNOWN) pKeyEvent->Emit(Time(), press, modifiers, state);
}

static void CharCallback(GLFWwindow * /*window*/, unsigned int c)
{
    auto pCharEvent = EventSystem::sys()->globalEvent<EventType::CHAR, GlobalCharEvent>();
    pCharEvent->Emit(Time(), c);
}

static void ScrollCallback(GLFWwindow * /*window*/, double xoffset, double yoffset)
{
    auto pWheel = EventSystem::sys()->globalEvent<EventType::MOUSEWHEEL, GlobalWheelEvent>();
    pWheel->Emit(Time(), xoffset, yoffset);
}

static void MouseCursorCallback(GLFWwindow * /*window*/, double xpos, double ypos)
{
    auto pMove = EventSystem::sys()->globalEvent<EventType::MOUSEMOTION, GlobalMotionEvent>();
    pMove->Emit(Time(), xpos, ypos);
}

static void MouseCallback(GLFWwindow * /*window*/, int button, int action, int /*mods*/)
{
    auto pPress = EventSystem::sys()->globalEvent<EventType::MOUSEBUTTON, GlobalButtonEvent>();
    EventState state = (action == GLFW_PRESS) ? EventState::PRESSED : EventState::RELEASED;
    Input::Button btn = Input::Button::BUTTON_COUNT;

    switch (button)
    {
        case GLFW_MOUSE_BUTTON_LEFT:
            btn = Input::Button::BUTTON_LEFT;
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            btn = Input::Button::BUTTON_RIGHT;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            btn = Input::Button::BUTTON_MIDDLE;
            break;
        default:
            break;
    }
    if (btn != Input::Button::BUTTON_COUNT) pPress->Emit(Time(), btn, state);
}

static void CursorSetCallback(uint32_t /*timestamp*/, Input::Cursor cursor)
{
    if (cursor == Input::Cursor::CURSOR_NONE || glfwGetInputMode(s_pWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    {
        glfwSetInputMode(s_pWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
    else
    {
        glfwSetCursor(s_pWindow, s_pCursors[cursor] ? s_pCursors[cursor] : s_pCursors[Input::Cursor::CURSOR_ARROW]);
        glfwSetInputMode(s_pWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

static void InitCursors()
{
    // Create mouse cursors (By design, on X11 cursors are user configurable
    // and some cursors may be missing. When a cursor doesn't exist, GLFW will
    // emit an error which will often be printed by the app, so we temporarily
    // disable error reporting.  Missing cursors will return NULL and our
    // _UpdateMouseCursor() function will use the Arrow cursor instead.)

    GLFWerrorfun prev_error_callback = glfwSetErrorCallback(NULL);
    s_pCursors[Input::Cursor::CURSOR_ARROW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_TEXT_INPUT] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_VERTICAL] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_HORIZENTAL] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
#if GLFW_HAS_NEW_CURSORS
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_LEFT] = glfwCreateStandardCursor(GLFW_RESIZE_NEWS_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_RIGHT] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
#else
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_LEFT] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_RIGHT] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);

#endif
    s_pCursors[Input::Cursor::CURSOR_HAND] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    auto pCursor = EventSystem::sys()->globalEvent<EventType::CURSORSET, GlobalCursorSetEvent>();
    pCursor->Watch(CursorSetCallback);
    glfwSetErrorCallback(prev_error_callback);
}

static void FiniCursors()
{
    for (int i = 0; i < Input::Cursor::CURSOR_COUNT; i++)
    {
        glfwDestroyCursor(s_pCursors[i]);
        s_pCursors[i] = nullptr;
    }
}

}  // namespace GLFWDetail

static bool Initialize(const char *name, unsigned w, unsigned h, bool bResizable = false)
{
    // A stale error would otherwise be reported as the cause of the call that actually failed.
    glfwGetError(nullptr);
    if (!glfwInit())
    {
        GLFWDetail::ReportGlfwError("glfwInit");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, bResizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWDetail::s_pWindow = glfwCreateWindow(w, h, name, nullptr, nullptr);
    if (!GLFWDetail::s_pWindow)
    {
        GLFWDetail::ReportGlfwError("glfwCreateWindow");
        glfwTerminate();
        return false;
    }

    glfwSetKeyCallback(GLFWDetail::s_pWindow, GLFWDetail::KeyStrokeCallback);
    glfwSetMouseButtonCallback(GLFWDetail::s_pWindow, GLFWDetail::MouseCallback);
    glfwSetCursorPosCallback(GLFWDetail::s_pWindow, GLFWDetail::MouseCursorCallback);
    glfwSetScrollCallback(GLFWDetail::s_pWindow, GLFWDetail::ScrollCallback);
    glfwSetCharCallback(GLFWDetail::s_pWindow, GLFWDetail::CharCallback);

    GLFWDetail::InitCursors();
    return true;
}

static void Uninitialize()
{
    glfwDestroyWindow(GLFWDetail::s_pWindow);
    GLFWDetail::s_pWindow = nullptr;
    GLFWDetail::FiniCursors();
    glfwTerminate();
}

static bool ShouldQuit() { return glfwWindowShouldClose(GLFWDetail::s_pWindow); }

static void ProcessEvents()
{
    glfwPollEvents();  // don't block
}

// Provided for callers that need the framebuffer size; the in-tree applications do not (yet), and a
// translation unit that includes this header must not warn about it (#25).
[[maybe_unused]] static std::pair<int, int> GetWindowSize()
{
    int w;
    int h;
    glfwGetFramebufferSize(GLFWDetail::s_pWindow, &w, &h);
    return std::make_pair(w, h);
}

static void GetFramebufferSize(int &w, int &h) { glfwGetFramebufferSize(GLFWDetail::s_pWindow, &w, &h); }

/// Append the window's required instance extensions.
///
/// GLFW returns NULL with no error when the loader does not advertise the WSI extension its selected
/// platform needs, so the status alone is not enough to diagnose a start-up failure: on failure `error`
/// names the platform and the extension that was missing.
static VulkanInstanceExtensionStatus GetVulkanInstanceExtensions(std::vector<const char *> &vExtensions,
                                                                 std::string &error)
{
    // Clear anything recorded before this query, so the error read below belongs to it.
    glfwGetError(nullptr);

    uint32_t count = 0;
    const char **extensions = glfwGetRequiredInstanceExtensions(&count);
    if (extensions != nullptr && count > 0)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            vExtensions.push_back(extensions[i]);
        }
        return VulkanInstanceExtensionStatus::Success;
    }

    const char *description = nullptr;
    const int code = glfwGetError(&description);
    if (code != GLFW_NO_ERROR)
    {
        error = "GLFW could not initialise Vulkan: ";
        error += GLFWDetail::GlfwErrorName(code);
        error += ": ";
        error += description ? description : "no description";
        return VulkanInstanceExtensionStatus::LoaderUnavailable;
    }

    const int platform = GLFWDetail::CurrentPlatform();
    if (platform == 0)
    {
        error = "GLFW is not initialized; call Window::Initialize() before querying instance extensions.";
        return VulkanInstanceExtensionStatus::NotInitialized;
    }
    if (platform < 0)
    {
        error =
            "the window reported no Vulkan instance extensions, and this GLFW version cannot report the "
            "platform that needed a WSI extension.";
        return VulkanInstanceExtensionStatus::MissingWsiExtension;
    }

    error = "GLFW selected the ";
    error += GLFWDetail::PlatformName(platform);
    error += " platform, but the Vulkan loader did not advertise ";
    error += GLFWDetail::PlatformWsiExtension(platform);
    error += ", so the window reported no Vulkan instance extensions.";
    return VulkanInstanceExtensionStatus::MissingWsiExtension;
}

static VkSurfaceKHR GetVulkanSurface(VkInstance &instance)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    glfwCreateWindowSurface(instance, GLFWDetail::s_pWindow, NULL, &surface);
    return surface;
}

}  // namespace Window
