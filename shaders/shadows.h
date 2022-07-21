#ifndef SHADOWS_H
#define SHADOWS_H

/**
 * PCF shadow filter with fixed hardness
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
#endif
