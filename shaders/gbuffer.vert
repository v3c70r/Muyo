#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;

    mat4 objectToView;
    mat4 viewToObject;
    mat4 normalObjectToView;
} ubo;

// TODO: Eliminate small ubo? 
layout (set = 1, binding = 0) uniform WorldMatrix {
    mat4 mWorldMatrix;
} worldMatrix;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTexCoord;

layout (location = 0) out vec2 outTexCoord;
layout (location = 1) out vec4 outWorldPos;
layout (location = 2) out vec4 outWorldNormal;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    outTexCoord = inTexCoord.xy;
    outWorldPos = worldMatrix.mWorldMatrix * vec4(inPos, 1.0);
    outWorldNormal = worldMatrix.mWorldMatrix * vec4(inNormal, 0.0);
    gl_Position = ubo.proj * ubo.view * outWorldPos;
}
