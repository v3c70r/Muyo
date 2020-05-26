#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inViewPos;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outUV;

const uint TEX_ALBEDO = 0;
const uint TEX_NORMAL = 1;
const uint TEX_METALNESS = 2;
const uint TEX_ROUGHNESS = 3;
const uint TEX_AO = 4;
const uint TEX_COUNT = 5;
layout(binding = 1) uniform sampler2D texPBR[TEX_COUNT];

void main() {
    outPosition = inViewPos;
    outAlbedo = texture(texPBR[TEX_ALBEDO], inTexCoord);
    outNormal = texture(texPBR[TEX_NORMAL], inTexCoord);
    outUV = texture(texPBR[TEX_ROUGHNESS], inTexCoord);
    // Calculate screen space normal,,, for now.
    //vec3 dFdxPos = dFdx( inViewPos.xyz );
	//vec3 dFdyPos = dFdy( inViewPos.xyz );
	//vec3 facenormal = normalize( cross(dFdxPos,dFdyPos ));
    //outNormal = vec4(facenormal, 0.0);
    //outUV = vec4(inTexCoord, 0.0, 0.0);
}
