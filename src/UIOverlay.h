#pragma once
#include "../thirdparty/imgui/imgui.h"
// ImGui integration
class RenderContext;
class UIOverlay
{
public:
    UIOverlay(){};
    bool Initialize(RenderContext& context)
    {
        mCreatePipeline();
        return true;
    }
private:
    bool mCreatePipeline();

};
