// B0 probe: samples a heap image through a separate sampler and the combined Sampler2D composition
// (D12). The handles are function-local; the fragment stage writes through dynamic rendering, so the
// colour target is not a heap descriptor.

struct Push
{
    uint imageIndex;
    uint samplerIndex;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[shader("fragment")]
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    Texture2D<float4> texture = Texture2D<float4>.Handle(uint2(push.imageIndex, 0));
    SamplerState sampler = SamplerState.Handle(uint2(push.samplerIndex, 0));
    Sampler2D combined = Sampler2D.Handle(uint2(push.imageIndex, push.samplerIndex));
    return texture.SampleLevel(sampler, float2(0.5, 0.5), 0.0) +
           combined.SampleLevel(float2(0.5, 0.5), 0.0);
}
