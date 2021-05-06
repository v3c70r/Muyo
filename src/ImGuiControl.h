#pragma once
#include <imgui.h>
#include "EventSystem.h"


namespace ImGui
{
static uint32_t               g_Time = 0.0;

static void installEventHandlers()
{
   //install callbacks
    auto pChar = EventSystem::sys()->globalEvent<EventType::CHAR,
                                                 GlobalCharEvent>();
    pChar->Watch([](uint32_t timestamp, unsigned int c) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter(c);
        g_Time = timestamp;
    });

    auto pKey = EventSystem::sys()->globalEvent<EventType::KEY,
                                                GlobalKeyEvent>();
    pKey->Watch([](uint32_t timestamp, Input::Key key, uint16_t mods,
                   EventState state) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            io.KeysDown[key] = (state == EventState::PRESSED) ? true : false;
            io.KeyCtrl = (mods & Input::MOD_CTRL);
            io.KeyShift = (mods & Input::MOD_SHIFT);
            io.KeyAlt = (mods & Input::MOD_ALT);
#ifdef _WIN32
            io.KeySuper = false;
#else
            io.KeySuper = (mods & Input::MOD_META);
#endif
        }
        g_Time = timestamp;
    });

    auto pScroll = EventSystem::sys()->globalEvent<EventType::MOUSEWHEEL,
                                                   GlobalWheelEvent>();
    pScroll->Watch([](uint32_t timestamp, float xoffset, float yoffset) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            io.MouseWheelH += (float)xoffset;
            io.MouseWheel += (float)yoffset;
        }
        g_Time = timestamp;
    });

    auto pBtn = EventSystem::sys()->globalEvent<EventType::MOUSEBUTTON,
                                                GlobalButtonEvent>();
    pBtn->Watch([](uint32_t timestamp, Input::Button btn, EventState state) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            io.MouseDown[btn] = (state == EventState::PRESSED) ? true : false;
        }
        g_Time = timestamp;
    });

    auto pMotion = EventSystem::sys()->globalEvent<EventType::MOUSEMOTION,
                                                   GlobalMotionEvent>();
    pMotion->Watch([](uint32_t timestamp, float sx, float sy) {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(sx, sy);
        g_Time = timestamp;
    });
}

static bool Init()
{
    g_Time = 0.0;

    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    //io.BackendPlatformName = "imgui_impl_glfw";

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
    io.KeyMap[ImGuiKey_Tab] = Input::KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = Input::KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = Input::KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = Input::KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow] = Input::KEY_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = Input::KEY_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown] = Input::KEY_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home] = Input::KEY_HOME;
    io.KeyMap[ImGuiKey_End] = Input::KEY_END;
    io.KeyMap[ImGuiKey_Insert] = Input::KEY_INSERT;
    io.KeyMap[ImGuiKey_Delete] = Input::KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = Input::KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Space] = Input::KEY_SPACE;
    io.KeyMap[ImGuiKey_Enter] = Input::KEY_RETURN;
    io.KeyMap[ImGuiKey_Escape] = Input::KEY_ESCAPE;
    //io.KeyMap[ImGuiKey_KeyPadEnter] = GLFW_KEY_KP_ENTER;
    io.KeyMap[ImGuiKey_A] = Input::KEY_A;
    io.KeyMap[ImGuiKey_C] = Input::KEY_C;
    io.KeyMap[ImGuiKey_V] = Input::KEY_V;
    io.KeyMap[ImGuiKey_X] = Input::KEY_X;
    io.KeyMap[ImGuiKey_Y] = Input::KEY_Y;
    io.KeyMap[ImGuiKey_Z] = Input::KEY_Z;

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
    auto pCursor = EventSystem::sys()->globalEvent<EventType::CURSORSET,
                                                   GlobalCursorSetEvent>();
    if (type == ImGuiMouseCursor_None || io.MouseDrawCursor) {
        pCursor->Emit(g_Time, Input::Cursor::CURSOR_NONE); //use the last time
    } else if (type < ImGuiMouseCursor_COUNT)  {
        pCursor->Emit(g_Time, (Input::Cursor)type);
    }
}

//Dont have a joystick, couldn't test
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
}; // namespace ImGui
