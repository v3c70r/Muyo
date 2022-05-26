struct RayPayload
{
	vec4 vColorAndDistance; // rgb + t
	vec4 vScatterDirection; // xyz + w (is scatter needed)
	uint uRandomSeed;
};