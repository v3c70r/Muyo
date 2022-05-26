// Ray miss shader

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "pathTracingPayload.h"
layout(location = 0) rayPayloadInEXT RayPayload ray;

void main()
{
    ray.vColorAndDistance = vec4(0.0, 0.0, 0.0, 1.0);
}
