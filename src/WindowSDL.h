#pragma once

#include <SDL.h>
#include <SDL_events.h>
#include <SDL_mouse.h>
#include <SDL_vulkan.h>
#include <bits/stdint-uintn.h>
#include <cstdio>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include "EventSystem.h"

/// This is by no means the best way doing things, but since writing window
/// system code directly in main.cpp is less pleasant
namespace Window {

namespace _SDL {
static SDL_Window *s_pWindow = nullptr;
static SDL_Cursor *s_pCursors[SDL_NUM_SYSTEM_CURSORS] = {};

static bool s_bShouldQuit = true;

static void onKeyStroke(EventSystem *sys, uint32_t time,
                        SDL_Keycode sym, SDL_Scancode scan,
                        int action, int mods)
{
    auto pKeyEvent = sys->globalEvent<EventType::KEY, GlobalKeyEvent>();
    auto pCharEvent = sys->globalEvent<EventType::CHAR, GlobalCharEvent>();
    enum Input::Key key = Input::KEY_UNKNOWN;
    uint16_t modifiers = Input::MOD_NONE;
    EventState state = (action == SDL_PRESSED) ?
        EventState::PRESSED : EventState::RELEASED;

    if ((mods & KMOD_ALT))
        modifiers |= Input::MOD_ALT;
    if ((mods & KMOD_SHIFT))
        modifiers |= Input::MOD_SHIFT;
    if ((mods & KMOD_CTRL))
        modifiers |= Input::MOD_CTRL;
    if ((mods & KMOD_GUI))
        modifiers |= Input::MOD_META;

    //printable chars, we generate char event
    if ((sym < INT8_MAX) && (::isprint(sym)))
    {
        if (action == SDL_PRESSED)
            pCharEvent->Emit(time, sym);
        return;
    }
    else if ((key = Input::getKeyREBTD(sym)))
    {
        pKeyEvent->Emit(time, key, modifiers, state);
        return;
    }
    else if ((scan & SDLK_SCANCODE_MASK))
    {
        key = Input::keyFromScanCode(scan);
        if (key != Input::KEY_UNKNOWN)
            pKeyEvent->Emit(time, key, modifiers, state);
        return;
    }
    //get scancode
}

static void onMouseScroll(EventSystem *sys, uint32_t time,
                          double xoffset, double yoffset)
{
    auto pWheel = sys->globalEvent<EventType::MOUSEWHEEL, GlobalWheelEvent>();
    pWheel->Emit(time, xoffset, yoffset);
}

static void onMouseMotion(EventSystem *sys, uint32_t time,
                          double xpose, double ypos)
{
    auto pMove = sys->globalEvent<EventType::MOUSEMOTION, GlobalMotionEvent>();
    pMove->Emit(time, xpose, ypos);
}

static void onMousePress(EventSystem *sys, uint32_t time, int button,
                         int action)
{
    auto pPress = sys->globalEvent<EventType::MOUSEBUTTON, GlobalButtonEvent>();
    EventState state = (action == SDL_PRESSED) ?
        EventState::PRESSED : EventState::RELEASED;
    Input::Button btn = Input::Button::BUTTON_COUNT;

    switch (button)
    {
    case SDL_BUTTON_LEFT:
        btn = Input::Button::BUTTON_LEFT;
        break;
    case SDL_BUTTON_MIDDLE:
        btn = Input::Button::BUTTON_MIDDLE;
        break;
    case SDL_BUTTON_RIGHT:
        btn = Input::Button::BUTTON_RIGHT;
        break;
    }
    if (btn != Input::Button::BUTTON_COUNT)
    {
        pPress->Emit(time, btn, state);
    }

}

static void onWindowEvent(EventSystem *sys, SDL_WindowEvent &event)
{
   auto pResize = sys->globalEvent<EventType::WINDOWRESIZE, GlobalResizeEvent>();
   if (event.event == SDL_WINDOWEVENT_SIZE_CHANGED)
   {
        pResize->Emit(event.timestamp, event.data1, event.data2);
   }
   else if (event.event == SDL_WINDOWEVENT_CLOSE)
   {
       s_bShouldQuit = true;
   }
}

static void onSetCursor(uint32_t timestamp, Input::Cursor type)
{
        int show = (type != Input::Cursor::CURSOR_NONE) ?
            SDL_ENABLE : SDL_DISABLE;
        SDL_Cursor *cursor = nullptr;

        switch (type)
        {
        case Input::Cursor::CURSOR_ARROW:
            cursor = s_pCursors[SDL_SYSTEM_CURSOR_ARROW];
            break;
        case Input::Cursor::CURSOR_TEXT_INPUT:
            cursor = s_pCursors[SDL_SYSTEM_CURSOR_IBEAM];
            break;
        case Input::Cursor::CURSOR_RESIZE_HORIZENTAL:
            cursor = s_pCursors[SDL_SYSTEM_CURSOR_SIZENS];
            break;
        case Input::Cursor::CURSOR_RESIZE_VERTICAL:
            cursor = s_pCursors[SDL_SYSTEM_CURSOR_SIZENS];
            break;
        case Input::Cursor::CURSOR_RESIZE_BOTTOM_LEFT:
            cursor = s_pCursors[SDL_SYSTEM_CURSOR_SIZENESW];
            break;
        case Input::Cursor::CURSOR_RESIZE_BOTTOM_RIGHT:
            cursor = s_pCursors[SDL_SYSTEM_CURSOR_SIZENWSE];
            break;
        case Input::Cursor::CURSOR_HAND:
            cursor = s_pCursors[SDL_SYSTEM_CURSOR_HAND];
            break;
        default:
            break;
        }
        if (cursor)
            SDL_SetCursor(cursor);
        SDL_ShowCursor(show);
}

static void HandleEvents(SDL_Event& event, EventSystem *system)
{
    switch (event.type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        onKeyStroke(system,
                    event.key.timestamp,
                    event.key.keysym.sym,
                    event.key.keysym.scancode,
                    event.key.state,
                    event.key.keysym.mod);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        onMousePress(system,
                     event.button.timestamp,
                     event.button.button,
                     event.button.state);
        break;
    case SDL_MOUSEMOTION:
        onMouseMotion(system, event.motion.timestamp,
                      event.motion.x,
                      event.motion.y);
        break;
    case SDL_MOUSEWHEEL:
        onMouseScroll(system,
                      event.wheel.timestamp,
                      event.wheel.x, event.wheel.y);
        break;
    case SDL_WINDOWEVENT:
        onWindowEvent(system, event.window);
        break;
    case SDL_QUIT:
    case SDL_APP_TERMINATING:
        printf("Should close here!!!\n");
        s_bShouldQuit = true;
        break;
    default:
        break;
    }
}

static void InitCursors()
{
    for (int i = 0; i < SDL_NUM_SYSTEM_CURSORS; i++)
        s_pCursors[i] = SDL_CreateSystemCursor((SDL_SystemCursor)i);
    auto pCursor = EventSystem::sys()->globalEvent<EventType::CURSORSET,
                                                   GlobalCursorSetEvent>();
    pCursor->Watch(onSetCursor);
}

static void FiniCursors()
{
    for (int i = 0; i < SDL_NUM_SYSTEM_CURSORS; i++) {
        if (s_pCursors[i])
            SDL_FreeCursor(s_pCursors[i]);
        s_pCursors[i] = nullptr;
    }
}

}

static bool Initialize(const char *name, unsigned w, unsigned h)
{
        if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
            return false;
        _SDL::s_pWindow = SDL_CreateWindow(name,
                                           SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED,
                                           w, h,
                                           SDL_WINDOW_VULKAN |
                                           SDL_WINDOW_SHOWN);
        if (!_SDL::s_pWindow)
        {
            SDL_Quit();
            return false;
        }
        _SDL::s_bShouldQuit = false;
        _SDL::InitCursors();
        return true;
}

static void Uninitialize()
{
    SDL_DestroyWindow(_SDL::s_pWindow);
    _SDL::s_pWindow = nullptr;
    _SDL::s_bShouldQuit = true;
    _SDL::FiniCursors();
    SDL_Quit();
}

static bool ShouldQuit()
{
    return _SDL::s_bShouldQuit;
}

static void ProcessEvents()
{
    SDL_Event event;

    while (SDL_PollEvent(&event) != 0)
        _SDL::HandleEvents(event, EventSystem::sys());
}

static std::pair<int, int> GetWindowSize()
{
    int w, h;

    SDL_GetWindowSize(_SDL::s_pWindow, &w, &h);
    return std::make_pair(w, h);
}

template<class container>
static size_t GetVulkanInstanceExtensions(container& c)
{
    uint32_t count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(_SDL::s_pWindow, &count, nullptr))
        return 0;
    c.resize(count);
    if (!SDL_Vulkan_GetInstanceExtensions(_SDL::s_pWindow, &count, c.data()))
        return 0;
    return count;
}

static VkSurfaceKHR GetVulkanSurface(VkInstance& instance)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    SDL_Vulkan_CreateSurface(_SDL::s_pWindow, instance, &surface);
    return surface;
}

}
