// B1b-3: the UI fragment stage on the descriptor-heap path. The font is a combined Sampler2D read from
// two heap indices in the node's push data; no DescriptorSet/Binding is declared. The legacy ui.frag
// stays for RenderPassManager.

struct Push
{
    float2 scale;
    float2 translate;
    uint fontImage;
    uint fontSampler;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[shader("fragment")]
float4 main(float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target
{
    Sampler2D font = Sampler2D.Handle(uint2(push.fontImage, push.fontSampler));
    return color * font.SampleLevel(uv, 0.0);
}
