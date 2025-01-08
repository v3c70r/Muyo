#extension GL_EXT_mesh_shader : require
#include "Camera.h"

[[vk::binding(0)]] ConstantBuffer<CameraUBO> uboCamera;
[[vk::binding(0, 1)]] StructuredBuffer<float3> positions;
[[vk::binding(1, 1)]] StructuredBuffer<float3> scales;
[[vk::binding(2, 1)]] StructuredBuffer<float4> rotations;
[[vk::binding(3, 1)]] StructuredBuffer<float> alphas;
[[vk::binding(4, 1)]] StructuredBuffer<float3> colors;

// Vertex Shader Output Structure
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 outUV : TEXCOORD0;
    [[vk::location(1)]] float4 outColor : COLOR0;
};

static const float2 vertices[6] = {
    float2(-1, -1),  // Bottom left
    float2(1, -1),   // Bottom right
    float2(-1, 1),   // Top left
    float2(-1, 1),   // Top left
    float2(1, -1),   // Bottom right
    float2(1, 1)     // Top right
};
[[vk::push_constant]] int shDegree;

float3 ComputeQuadPos(float3 position, float3 upVector, matrix pv, int vertexID)
{
    float4 clipPos = mul(float4(position, 1.0), (pv));
    clipPos /= clipPos.w;
    float4 clipPosUp = mul(float4(position + upVector, 1.0), pv);
    clipPosUp /= clipPosUp.w;
    float offestLength = min(length(clipPosUp.xy - clipPos.xy), 0.01);
    float3 clipPosOffset = float3(vertices[vertexID] * offestLength, 0);
    return clipPos.xyz + clipPosOffset;
}
float3 ExtractUpVector(matrix view)
{
    return normalize(view[1].xyz);
}

struct VertexInput
{
    uint instanceID : SV_InstanceID;
    uint vertexID : SV_VertexID;
};

// Vertex Shader
[shader("vertex")] VS_OUTPUT
main(VertexInput input)
{
    VS_OUTPUT output = (VS_OUTPUT)0;

    const matrix pv = mul(uboCamera.view, uboCamera.proj);

    output.pos =
        float4(ComputeQuadPos(positions[input.instanceID], ExtractUpVector(uboCamera.view), pv, int(input.vertexID)), 1.0);
    output.outUV = float2((vertices[input.vertexID].x + 1) / 2, (vertices[input.vertexID].y + 1) / 2);
    output.outColor = float4(colors[input.instanceID], 1);
    
    //output.outUV = input.inUV;
    //output.outColor = input.inColor;
    //output.pos = float4(input.inPos * scale + translate, 0.0, 1.0);
    return output;
}

