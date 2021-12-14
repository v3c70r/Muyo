#pragma once

#include <stdint.h>
#include <utility>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "EventSystem.h"

namespace Window {

namespace _GLFW {
static GLFWwindow *s_pWindow = nullptr;
static GLFWcursor *s_pCursors[Input::Cursor::CURSOR_COUNT];

//get time in milliseconds
static inline uint32_t Time()
{
    return (glfwGetTimerValue() * 1000) / glfwGetTimerFrequency();
}

/// event handlers
static Input::Key GetEditKey(int key)
{
    switch(key)
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
    switch(key)
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
        //rest are the modifiers we dont care about
    default:
        return Input::KEY_UNKNOWN;
    }
}

static void KeyStrokeCallback(GLFWwindow *window, int key, int scancode,
                              int action, int mods)
{
    auto pKeyEvent = EventSystem::sys()->globalEvent<EventType::KEY,
                                                     GlobalKeyEvent>();
    EventState state = (action == GLFW_PRESS) ?
        EventState::PRESSED : EventState::RELEASED;
    uint16_t modifiers = Input::MOD_NONE;
    Input::Key press = Input::Key::KEY_UNKNOWN;

    //get mod
    if (mods & GLFW_MOD_ALT)
        modifiers |= Input::MOD_ALT;
    if (mods & GLFW_MOD_SHIFT)
        modifiers |= Input::MOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL)
        modifiers |= Input::MOD_CTRL;
    if (mods & GLFW_MOD_SUPER)
        modifiers |= Input::MOD_META;

    if ((press = GetEditKey(key)) != Input::KEY_UNKNOWN)
        pKeyEvent->Emit(Time(), press, modifiers, state);
    else if ((press = GetFunctionKey(key)) != Input::KEY_UNKNOWN)
        pKeyEvent->Emit(Time(), press, modifiers, state);
}

static void CharCallback(GLFWwindow* window, unsigned int c)
{
    auto pCharEvent = EventSystem::sys()->globalEvent<EventType::CHAR,
                                                      GlobalCharEvent>();
    pCharEvent->Emit(Time(), c);
}

static void ScrollCallback(GLFWwindow* window, double xoffset,
                                   double yoffset)
{
    auto pWheel = EventSystem::sys()->globalEvent<EventType::MOUSEWHEEL,
                                                  GlobalWheelEvent>();
    pWheel->Emit(Time(), xoffset, yoffset);
}

static void MouseCursorCallback(GLFWwindow *window, double xpos, double ypos)
{
    auto pMove = EventSystem::sys()->globalEvent<EventType::MOUSEMOTION,
                                                 GlobalMotionEvent>();
    pMove->Emit(Time(), xpos, ypos);
}

static void MouseCallback(GLFWwindow *window, int button, int action, int mods)
{
    auto pPress = EventSystem::sys()->globalEvent<EventType::MOUSEBUTTON,
                                                  GlobalButtonEvent>();
    EventState state = (action == GLFW_PRESS) ?
        EventState::PRESSED : EventState::RELEASED;
    Input::Button btn = Input::Button::BUTTON_COUNT;

    switch (button) {
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
    if (btn != Input::Button::BUTTON_COUNT)
        pPress->Emit(Time(), btn, state);
}

static void CursorSetCallback(uint32_t timestamp, Input::Cursor cursor)
{
    if (cursor == Input::Cursor::CURSOR_NONE ||
        glfwGetInputMode(s_pWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    {
        glfwSetInputMode(s_pWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
    else
    {
        glfwSetCursor(s_pWindow, s_pCursors[cursor] ?
                      s_pCursors[cursor] :
                      s_pCursors[Input::Cursor::CURSOR_ARROW]);
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
    s_pCursors[Input::Cursor::CURSOR_ARROW] =
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_TEXT_INPUT] =
        glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_VERTICAL] =
        glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_HORIZENTAL] =
        glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
#if GLFW_HAS_NEW_CURSORS
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_LEFT] =
        glfwCreateStandardCursor(GLFW_RESIZE_NEWS_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_RIGHT] =
        glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
#else
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_LEFT] =
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    s_pCursors[Input::Cursor::CURSOR_RESIZE_BOTTOM_RIGHT] =
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
    s_pCursors[Input::Cursor::CURSOR_HAND] =
        glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    auto pCursor = EventSystem::sys()->globalEvent<EventType::CURSORSET,
                                                   GlobalCursorSetEvent>();
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

}

static bool Initialize(const char *name, unsigned w, unsigned h)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    _GLFW::s_pWindow = glfwCreateWindow(w, h, "Vulkan", nullptr, nullptr);
    if (!_GLFW::s_pWindow)
    {
        glfwTerminate();
        return false;
    }

    glfwSetKeyCallback(_GLFW::s_pWindow, _GLFW::KeyStrokeCallback);
    glfwSetMouseButtonCallback(_GLFW::s_pWindow, _GLFW::MouseCallback);
    glfwSetCursorPosCallback(_GLFW::s_pWindow, _GLFW::MouseCursorCallback);
    glfwSetScrollCallback(_GLFW::s_pWindow, _GLFW::ScrollCallback);
    glfwSetCharCallback(_GLFW::s_pWindow, _GLFW::CharCallback);

    _GLFW::InitCursors();
    return true;
}

static void Uninitialize()
{
    glfwDestroyWindow(_GLFW::s_pWindow);
    _GLFW::s_pWindow = nullptr;
    _GLFW::FiniCursors();
    glfwTerminate();
}

static bool ShouldQuit()
{
    return glfwWindowShouldClose(_GLFW::s_pWindow);
}

static void ProcessEvents()
{
    glfwPollEvents(); //don't block
}

static std::pair<int, int> GetWindowSize()
{
    int w, h;
    glfwGetWindowSize(_GLFW::s_pWindow, &w, &h);
    return std::make_pair(w, h);
}

template<class container>
static size_t GetVulkanInstanceExtensions(container& c)
{
    uint32_t count = 0;
    const char **extensions;

    extensions = glfwGetRequiredInstanceExtensions(&count);
    for (unsigned i = 0; i < count; i++) {
        c.push_back(extensions[i]);
    }
    return count;
}

static VkSurfaceKHR GetVulkanSurface(VkInstance& instance)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    glfwCreateWindowSurface(instance, _GLFW::s_pWindow, NULL, &surface);
    return surface;
}

}
