#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inViewPos;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outUV;

layout(binding = 1) uniform sampler2D texSampler;

vec4 drawAxis(vec2 center, float width)
{
    const vec3 X_COLOR = vec3(1.0, 0.0, 0.0);
    const vec3 Y_COLOR = vec3(0.0, 1.0, 0.0);
    const float LENGTH = 0.01;
    vec2 pos = gl_FragCoord.xy;
    if (pos.x < center.x + width && pos.x > center.x && pos.y < center.y + LENGTH && pos.y > center.y)
        return vec4(Y_COLOR, 1.0);
    if (pos.y < center.y + width && pos.y > center.y && pos.x < center.x + LENGTH && pos.x > center.x)
        return vec4(X_COLOR, 1.0);
    return texture(texSampler, inTexCoord);
}

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
