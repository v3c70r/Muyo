#version 450
#extension GL_ARB_separate_shader_objects : enable

const uint GBUFFER_POS = 0;
const uint GBUFFER_ALBEDO = 1;
const uint GBUFFER_NORMAL = 2;
const uint GBUFFER_UV = 3;
const uint GBUFFER_COUNT = 4;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D inGBuffers[GBUFFER_COUNT];

vec4 getFragColor()
{
    vec2 coord = texCoords.xy - vec2(0.5, 0.5);
    if (coord.x < 0.0)
    {
        coord.x = texCoords.x * 2.0;
        if (coord.y < 0.0)
        {
            coord.y = texCoords.y * 2.0;
            return texture(inGBuffers[GBUFFER_POS], coord);
        }
        else
        {
            coord.y *= 2.0;
            return texture(inGBuffers[GBUFFER_NORMAL], coord);
        }
    }
    else
    {
        coord.x *= 2.0;
        if (coord.y < 0.0)
        {
            coord.y = texCoords.y * 2.0;
            return texture(inGBuffers[GBUFFER_ALBEDO], coord);
        }
        else
        {
            coord.y *= 2.0;
            return texture(inGBuffers[GBUFFER_UV], coord);
        }
    }
}

void main() {
    outColor = getFragColor();
    outColor.a = 1.0;
}
