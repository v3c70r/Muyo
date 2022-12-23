#ifndef RAY_TRACING_PAYLOAD_H
#define RAY_TRACING_PAYLOAD_H
struct RayPayload
{
	vec4 vColorAndDistance; // rgb + t
	vec4 vScatterDirection; // xyz + w (is scatter needed)
	uint uRandomSeed;
};
#endif // RAY_TRACING_PAYLOAD_H
