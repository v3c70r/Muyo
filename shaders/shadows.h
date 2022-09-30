#ifndef SHADOWS_H
#define SHADOWS_H
#include "random.h"

// PCF Shadow 
/**
 * PCF shadow filter with fixed kernel size in projected space
 * @param vShadingFragPos NDC position of the shading point
 * @param fStep Search step size
 * @param fNSampleDimension Number of samples in each dimension
 * @param fBias Shadow Biase to remove acne
 * @param shadowMap Shadow map
 */
float PCFShadowVisibililty(vec3 vShadingFragPos, float fStep, int nSampleDimension, float fBias, sampler2D shadowMap)
{
    float fVisibility = 0.0;
    int nSampleCount = 0;
    for (int i = - nSampleDimension / 2; i <= nSampleDimension / 2; i++)
    {
        for (int j = - nSampleDimension / 2; j <= nSampleDimension / 2; j++)
        {
            vec2 vOffset = vec2(i, j) * fStep;
            float fShadowDepth = texture(shadowMap, vShadingFragPos.xy + vOffset).r;
            if (vShadingFragPos.z - fShadowDepth <= fBias)
            {
                fVisibility += 1.0;
            }
            nSampleCount++;
        }
    }
    return fVisibility / nSampleCount;
}

vec3 WorldToNdc(vec3 vWorldPos, mat4 mProjView)
{
    vec4 vNdcPos = mProjView * vec4(vWorldPos, 1.0);
    vNdcPos = vNdcPos / vNdcPos.w;
    vNdcPos.xy = vNdcPos.xy * 0.5 + 0.5;

    return vNdcPos.xyz;
}
/**
 * PCF shadow filter with fixed kernel size in world space
 * @param vShadingPoint Shading point in world space
 * @param fStep Search step size
 * @param fNSampleDimension Number of samples in each dimension
 * @param fBias Shadow Biase to remove acne
 * @param shadowMap Shadow map
 */
float PCFShadowVisibililty(vec3 vShadingPoint, vec3 vLightDir, uint nNumSamples, float nSampleRadius, uint nSeed, float fBias, sampler2D shadowMap, mat4 mShadowProjViewMatrix)
{
    float fVisibility = 0.0;
    int nSampleCount = 0;
    vec3 vTangent = normalize(cross(vLightDir, vec3(0.0, 1.0, 0.0)));
    vec3 vBitangent = normalize(cross(vLightDir, vTangent));
    for (uint i = 0; i < nNumSamples; i++)
    {
        // offset sample in the disk oriented along the light direction
        const vec2 vOffset = RandomInUnitDisk(nSeed) * nSampleRadius;
        const vec3 vSamplePosition = vTangent * vOffset.x + vBitangent * vOffset.y + vShadingPoint;
        // project sample to the shadow map
        vec3 vNdcPos = WorldToNdc(vSamplePosition, mShadowProjViewMatrix);
        float fShadowDepth = texture(shadowMap, vNdcPos.xy).r;
        if (vNdcPos.z - fShadowDepth <= fBias)
        {
            fVisibility += 1.0;
        }
        nSampleCount++;
    }
    return fVisibility / nSampleCount;
}

// PCSS Shadow



#endif
