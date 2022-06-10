// Ray miss shader

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "pathTracingPayload.h"
layout(location = 0) rayPayloadInEXT RayPayload ray;
layout(set = 2, binding = 0) uniform samplerCube environmentMap;


void main()
{
    // Fetch skybox radiance when hitting the skybox
    const vec3 vSkyboxColor = texture(environmentMap, normalize(ray.vScatterDirection.xyz)).rgb;
    ray.vColorAndDistance = vec4(vSkyboxColor, 1.0);
}
