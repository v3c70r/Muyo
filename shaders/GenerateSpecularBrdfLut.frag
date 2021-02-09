#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "brdf.h"

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec2 outColor;

void main()
{
    vec2 vIntegratedBRDF = IntegrateBRDF(texCoords.x, texCoords.y); 
    outColor = vIntegratedBRDF;
}
