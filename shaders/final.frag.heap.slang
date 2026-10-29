// The FINAL pass on the descriptor-heap path (#20 increment 1). The legacy scene's composite is imported
// as a Sampler2D read from two heap indices in the node's push data; no DescriptorSet/Binding is declared.
// This replaces the deleted legacy RenderPassFinal/triangle.frag, and matches its output: opaque alpha, so
// the present target never carries scene alpha.

struct Push
{
    uint image;
    uint sampler;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[shader("fragment")]
float4 main(float2 uv : TEXCOORD0) : SV_Target
{
    Sampler2D composite = Sampler2D.Handle(uint2(push.image, push.sampler));
    float4 color = composite.SampleLevel(uv, 0.0);
    return float4(color.rgb, 1.0);
}
