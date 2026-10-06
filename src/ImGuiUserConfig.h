#pragma once

// Project-wide Dear ImGui compile-time configuration.
// Loaded via IMGUI_USER_CONFIG (see CMakeLists.txt) so the imgui submodule stays untouched.

// This project stores 0-based descriptor-set indices inside ImTextureID, so a value of 0 is a
// valid texture id (the UI font texture). Override the default "invalid" sentinel (0) to keep
// ImDrawCmd::GetTexID() assertions happy.
#define ImTextureID_Invalid ((ImTextureID)-1)
