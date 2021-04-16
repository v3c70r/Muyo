#pragma once

#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <utility>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "EventSystem.h"
#include "interface/key.h"
#include "interface/mouse.h"

namespace Window {

namespace _GLFW {
static GLFWwindow *s_pWindow = nullptr;
static GLFWcursor *s_pCursors[Input::Cursor::Cursor_Count];

//get time in milliseconds
static inline uint32_t Time()
{
    return (glfwGetTimerValue() * 1000) / glfwGetTimerFrequency();
}

/// event handlers
static Input::Key GetEditKey(int key)
{
    switch(key) {
    case GLFW_KEY_ENTER:
        return Input::Key_Return;
    case GLFW_KEY_ESCAPE:
        return Input::Key_Escape;
    case GLFW_KEY_BACKSPACE:
        return Input::Key_Backspace;
    case GLFW_KEY_TAB:
        return Input::Key_Tab;
    case GLFW_KEY_DELETE:
        return Input::Key_Delete;
    default:
        return Input::Key_Unknown;
    }
}

static Input::Key GetFunctionKey(int key)
{
    switch(key) {
    case GLFW_KEY_CAPS_LOCK:
        return Input::Key_Capslock;
    case GLFW_KEY_PRINT_SCREEN:
        return Input::Key_Printscreen;
    case GLFW_KEY_SCROLL_LOCK:
        return Input::Key_Scrolllock;
    case GLFW_KEY_PAUSE:
        return Input::Key_Pause;
    case GLFW_KEY_INSERT:
        return Input::Key_Insert;
    case GLFW_KEY_HOME:
        return Input::Key_Home;
    case GLFW_KEY_PAGE_UP:
        return Input::Key_PageUp;
    case GLFW_KEY_PAGE_DOWN:
        return Input::Key_PageDown;
    case GLFW_KEY_RIGHT:
        return Input::Key_Right;
    case GLFW_KEY_LEFT:
        return Input::Key_Left;
    case GLFW_KEY_DOWN:
        return Input::Key_Down;
    case GLFW_KEY_UP:
        return Input::Key_Up;
        //rest are the modifiers we dont care about
    default:
        return Input::Key_Unknown;
    }
}

static void KeyStrokeCallback(GLFWwindow *window, int key, int scancode,
                              int action, int mods)
{
    auto pKeyEvent = EventSystem::sys()->globalEvent<EventType::Key,
                                                     GlobalKeyEvent>();
    EventState state = (action == GLFW_PRESS) ?
        EventState::Pressed : EventState::Released;
    uint16_t modifiers = Input::Mod_None;
    Input::Key press = Input::Key::Key_Unknown;

    //get mod
    if (mods & GLFW_MOD_ALT)
        modifiers |= Input::Mod_Alt;
    if (mods & GLFW_MOD_SHIFT)
        modifiers |= Input::Mod_Shift;
    if (mods & GLFW_MOD_CONTROL)
        modifiers |= Input::Mod_Ctrl;
    if (mods & GLFW_MOD_SUPER)
        modifiers |= Input::Mod_Meta;

    if ((press = GetEditKey(key)) != Input::Key_Unknown)
        pKeyEvent->emit(Time(), press, modifiers, state);
    else if ((press = GetFunctionKey(key)) != Input::Key_Unknown)
        pKeyEvent->emit(Time(), press, modifiers, state);
}

static void CharCallback(GLFWwindow* window, unsigned int c)
{
    auto pCharEvent = EventSystem::sys()->globalEvent<EventType::Char,
                                                      GlobalCharEvent>();
    pCharEvent->emit(Time(), c);
}

static void ScrollCallback(GLFWwindow* window, double xoffset,
                                   double yoffset)
{
    auto pWheel = EventSystem::sys()->globalEvent<EventType::MouseWheel,
                                                  GlobalWheelEvent>();
    pWheel->emit(Time(), xoffset, yoffset);
}

static void MouseCursorCallback(GLFWwindow *window, double xpos, double ypos)
{
    auto pMove = EventSystem::sys()->globalEvent<EventType::MouseMotion,
                                                 GlobalMotionEvent>();
    pMove->emit(Time(), xpos, ypos);
}

static void MouseCallback(GLFWwindow *window, int button, int action, int mods)
{
    auto pPress = EventSystem::sys()->globalEvent<EventType::MouseButton,
                                                  GlobalButtonEvent>();
    EventState state = (action == GLFW_PRESS) ?
        EventState::Pressed : EventState::Released;
    Input::Button btn = Input::Button::Count;

    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        btn = Input::Button::Left;
        break;
    case GLFW_MOUSE_BUTTON_RIGHT:
        btn = Input::Button::Right;
        break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        btn = Input::Button::Middle;
        break;
    default:
        break;
    }
    if (btn != Input::Button::Count)
        pPress->emit(Time(), btn, state);
}

static void CursorSetCallback(uint32_t timestamp, Input::Cursor cursor)
{
    if (cursor == Input::Cursor::Cursor_None ||
        glfwGetInputMode(s_pWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
        glfwSetInputMode(s_pWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    } else {
        glfwSetCursor(s_pWindow, s_pCursors[cursor] ?
                      s_pCursors[cursor] :
                      s_pCursors[Input::Cursor::Cursor_Arrow]);
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
    s_pCursors[Input::Cursor::Cursor_Arrow] =
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    s_pCursors[Input::Cursor::Cursor_Text_Input] =
        glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    s_pCursors[Input::Cursor::Cursor_Resize_Vertical] =
        glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    s_pCursors[Input::Cursor::Cursor_Resize_Horizental] =
        glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
#if GLFW_HAS_NEW_CURSORS
    s_pCursors[Input::Cursor::Cursor_Resize_Bottom_Left] =
        glfwCreateStandardCursor(GLFW_RESIZE_NEWS_CURSOR);
    s_pCursors[Input::Cursor::Cursor_Resize_Bottom_Right] =
        glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
#else
    s_pCursors[Input::Cursor::Cursor_Resize_Bottom_Left] =
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    s_pCursors[Input::Cursor::Cursor_Resize_Bottom_Right] =
        glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
    s_pCursors[Input::Cursor::Cursor_Hand] =
        glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    auto pCursor = EventSystem::sys()->globalEvent<EventType::CursorSet,
                                                   GlobalCursorSetEvent>();
    pCursor->watch(CursorSetCallback);
    glfwSetErrorCallback(prev_error_callback);
}

static void FiniCursors()
{
    for (int i = 0; i < Input::Cursor::Cursor_Count; i++) {
        glfwDestroyCursor(s_pCursors[i]);
        s_pCursors[i] = nullptr;
    }
}

}

static bool Init(const char *name, unsigned w, unsigned h)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    _GLFW::s_pWindow = glfwCreateWindow(w, h, "Vulkan", nullptr, nullptr);
    if (!_GLFW::s_pWindow) {
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

static void Fini()
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

static void PostPseudoEvents()
{
    glfwPostEmptyEvent();
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
