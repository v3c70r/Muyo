// Ray miss shader

#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 vResColor;

void main()
{
    vResColor = vec3(0.0f, 0.7f, 1.0f);
}
