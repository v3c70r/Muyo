// triangle_no_tex.frag.slang

// Slang uses HLSL-like syntax, but can be compatible with Vulkan targets.

struct PSInput {
    float2 texCoords : TEXCOORD0;
};

struct PSOutput {
    float4 outColor : SV_Target0;
};

[shader("fragment")]
PSOutput main(PSInput input)
{
    PSOutput output;
    output.outColor = float4(input.texCoords.x, input.texCoords.y, 1.0, 1.0);
    return output;
}
