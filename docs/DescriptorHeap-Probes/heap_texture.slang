struct Push
{
    uint2 imageIndex;
    uint2 samplerIndex;
    uint2 outputIndex;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

// Function-local .Handle() for an image, a sampler and the combined Sampler2D.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    Texture2D<float4> texture = Texture2D<float4>.Handle(push.imageIndex);
    SamplerState sampler = SamplerState.Handle(push.samplerIndex);
    Sampler2D combined = Sampler2D.Handle(uint2(push.imageIndex.x, push.samplerIndex.x));
    RWTexture2D<float4> output = RWTexture2D<float4>.Handle(push.outputIndex);
    float2 uv = float2(tid.xy);
    output[tid.xy] = texture.SampleLevel(sampler, uv, 0.0) + combined.SampleLevel(uv, 0.0);
}
