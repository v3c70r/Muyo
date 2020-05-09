#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inViewPos;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outUV;

layout(binding = 1) uniform sampler2D texSampler;

void main() {
    outPosition = inViewPos;
    outAlbedo = texture(texSampler, inTexCoord);
    // Calculate screen space normal,,, for now.
    vec3 dFdxPos = dFdx( inViewPos.xyz );
	vec3 dFdyPos = dFdy( inViewPos.xyz );
	vec3 facenormal = normalize( cross(dFdxPos,dFdyPos ));
    outNormal = vec4(facenormal, 0.0);
    outUV = vec4(inTexCoord, 0.0, 0.0);
}
