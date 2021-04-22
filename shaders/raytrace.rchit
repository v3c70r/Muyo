#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 vResColor;
hitAttributeEXT vec2 vHitAttribs;

void main()
{
    const vec3 barycentrics = vec3(1.0f - vHitAttribs.x - vHitAttribs.y, vHitAttribs.x, vHitAttribs.y);
    vResColor = vec3(barycentrics);
}
