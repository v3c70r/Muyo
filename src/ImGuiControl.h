#pragma once
#include <imgui.h>

#include "EventSystem.h"

namespace ImGui
{
static uint32_t g_Time = 0.0;

static ImGuiKey KeyToImGuiKey(Input::Key key)
{
    switch (key)
    {
        case Input::KEY_TAB: return ImGuiKey_Tab;
        case Input::KEY_LEFT: return ImGuiKey_LeftArrow;
        case Input::KEY_RIGHT: return ImGuiKey_RightArrow;
        case Input::KEY_UP: return ImGuiKey_UpArrow;
        case Input::KEY_DOWN: return ImGuiKey_DownArrow;
        case Input::KEY_PAGEUP: return ImGuiKey_PageUp;
        case Input::KEY_PAGEDOWN: return ImGuiKey_PageDown;
        case Input::KEY_HOME: return ImGuiKey_Home;
        case Input::KEY_END: return ImGuiKey_End;
        case Input::KEY_INSERT: return ImGuiKey_Insert;
        case Input::KEY_DELETE: return ImGuiKey_Delete;
        case Input::KEY_BACKSPACE: return ImGuiKey_Backspace;
        case Input::KEY_SPACE: return ImGuiKey_Space;
        case Input::KEY_RETURN: return ImGuiKey_Enter;
        case Input::KEY_ENTER: return ImGuiKey_Enter;
        case Input::KEY_ESCAPE: return ImGuiKey_Escape;
        case Input::KEY_CAPSLOCK: return ImGuiKey_CapsLock;
        case Input::KEY_SCROLLLOCK: return ImGuiKey_ScrollLock;
        case Input::KEY_PRINTSCREEN: return ImGuiKey_PrintScreen;
        case Input::KEY_PAUSE: return ImGuiKey_Pause;
        case Input::KEY_LCTRL: return ImGuiKey_LeftCtrl;
        case Input::KEY_RCTRL: return ImGuiKey_RightCtrl;
        case Input::KEY_LSHIFT: return ImGuiKey_LeftShift;
        case Input::KEY_RSHIFT: return ImGuiKey_RightShift;
        case Input::KEY_LALT: return ImGuiKey_LeftAlt;
        case Input::KEY_RALT: return ImGuiKey_RightAlt;
        case Input::KEY_LGUI: return ImGuiKey_LeftSuper;
        case Input::KEY_RGUI: return ImGuiKey_RightSuper;
        default: break;
    }
    if (key >= Input::KEY_0 && key <= Input::KEY_9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (key - Input::KEY_0));
    }
    if (key >= Input::KEY_A && key <= Input::KEY_Z)
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + (key - Input::KEY_A));
    }
    return ImGuiKey_None;
}

static void installEventHandlers()
{
    // install callbacks
    auto pChar = EventSystem::sys()->globalEvent<EventType::CHAR, GlobalCharEvent>();
    pChar->Watch([](uint32_t timestamp, unsigned int c)
                 {
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter(c);
        g_Time = timestamp; });

    auto pKey = EventSystem::sys()->globalEvent<EventType::KEY, GlobalKeyEvent>();
    pKey->Watch([](uint32_t timestamp, Input::Key key, uint16_t mods,
                   EventState state)
                {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            io.AddKeyEvent(KeyToImGuiKey(key), state == EventState::PRESSED);
            io.KeyCtrl = (mods & Input::MOD_CTRL);
            io.KeyShift = (mods & Input::MOD_SHIFT);
            io.KeyAlt = (mods & Input::MOD_ALT);
#ifdef _WIN32
            io.KeySuper = false;
#else
            io.KeySuper = (mods & Input::MOD_META);
#endif
        }
        g_Time = timestamp; });

    auto pScroll = EventSystem::sys()->globalEvent<EventType::MOUSEWHEEL, GlobalWheelEvent>();
    pScroll->Watch([](uint32_t timestamp, float xoffset, float yoffset)
                   {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            io.MouseWheelH += (float)xoffset;
            io.MouseWheel += (float)yoffset;
        }
        g_Time = timestamp; });

    auto pBtn = EventSystem::sys()->globalEvent<EventType::MOUSEBUTTON, GlobalButtonEvent>();
    pBtn->Watch([](uint32_t timestamp, Input::Button btn, EventState state)
                {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            io.MouseDown[btn] = (state == EventState::PRESSED) ? true : false;
        }
        g_Time = timestamp; });

    auto pMotion = EventSystem::sys()->globalEvent<EventType::MOUSEMOTION, GlobalMotionEvent>();
    pMotion->Watch([](uint32_t timestamp, float sx, float sy)
                   {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(sx, sy);
        g_Time = timestamp; });
}

static bool Init()
{
    g_Time = 0.0;

    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;  // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;   // We can honor io.WantSetMousePos requests (optional, rarely used)
    // io.BackendPlatformName = "imgui_impl_glfw";

    installEventHandlers();

#if defined(_WIN32)
    //io.ImeWindowHandle = (void*)glfwGetWin32Window(g_Window);
#endif
    return true;
}

static void Shutdown()
{
}

static void UpdateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange))
        return;

    ImGuiMouseCursor type = ImGui::GetMouseCursor();
    auto pCursor = EventSystem::sys()->globalEvent<EventType::CURSORSET, GlobalCursorSetEvent>();
    if (type == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        pCursor->Emit(g_Time, Input::Cursor::CURSOR_NONE);  // use the last time
    }
    else if (type < ImGuiMouseCursor_COUNT)
    {
        pCursor->Emit(g_Time, (Input::Cursor)type);
    }
}

// Dont have a joystick, couldn't test
static void UpdateGamepads()
{
    // ImGuiIO& io = ImGui::GetIO();
    // memset(io.NavInputs, 0, sizeof(io.NavInputs));
    // if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0)
    //     return;

    // // Update gamepad inputs
    // #define MAP_BUTTON(NAV_NO, BUTTON_NO)       { if (buttons_count > BUTTON_NO && buttons[BUTTON_NO] == GLFW_PRESS) io.NavInputs[NAV_NO] = 1.0f; }
    // #define MAP_ANALOG(NAV_NO, AXIS_NO, V0, V1) { float v = (axes_count > AXIS_NO) ? axes[AXIS_NO] : V0; v = (v - V0) / (V1 - V0); if (v > 1.0f) v = 1.0f; if (io.NavInputs[NAV_NO] < v) io.NavInputs[NAV_NO] = v; }
    // int axes_count = 0, buttons_count = 0;
    // const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axes_count);
    // const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &buttons_count);
    // MAP_BUTTON(ImGuiNavInput_Activate,   0);     // Cross / A
    // MAP_BUTTON(ImGuiNavInput_Cancel,     1);     // Circle / B
    // MAP_BUTTON(ImGuiNavInput_Menu,       2);     // Square / X
    // MAP_BUTTON(ImGuiNavInput_Input,      3);     // Triangle / Y
    // MAP_BUTTON(ImGuiNavInput_DpadLeft,   13);    // D-Pad Left
    // MAP_BUTTON(ImGuiNavInput_DpadRight,  11);    // D-Pad Right
    // MAP_BUTTON(ImGuiNavInput_DpadUp,     10);    // D-Pad Up
    // MAP_BUTTON(ImGuiNavInput_DpadDown,   12);    // D-Pad Down
    // MAP_BUTTON(ImGuiNavInput_FocusPrev,  4);     // L1 / LB
    // MAP_BUTTON(ImGuiNavInput_FocusNext,  5);     // R1 / RB
    // MAP_BUTTON(ImGuiNavInput_TweakSlow,  4);     // L1 / LB
    // MAP_BUTTON(ImGuiNavInput_TweakFast,  5);     // R1 / RB
    // MAP_ANALOG(ImGuiNavInput_LStickLeft, 0,  -0.3f,  -0.9f);
    // MAP_ANALOG(ImGuiNavInput_LStickRight,0,  +0.3f,  +0.9f);
    // MAP_ANALOG(ImGuiNavInput_LStickUp,   1,  +0.3f,  +0.9f);
    // MAP_ANALOG(ImGuiNavInput_LStickDown, 1,  -0.3f,  -0.9f);
    // #undef MAP_BUTTON
    // #undef MAP_ANALOG
    // if (axes_count > 0 && buttons_count > 0)
    //     io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    // else
    //     io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
}

static void Update()
{
    UpdateMouseCursor();
    UpdateGamepads();
}
};  // namespace ImGui

