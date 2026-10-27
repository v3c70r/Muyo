// B1b-5b-1: storage-image fill on the descriptor-heap path. The output image index arrives in the
// node's push data.

struct Push
{
    uint2 output;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[numthreads(8, 8, 1)][shader("compute")] void main(uint3 dtid
                                                   : SV_DispatchThreadID)
{
    RWTexture2D<float4> output = RWTexture2D<float4>.Handle(push.output);
    output[dtid.xy] = float4(0.0, 1.0, 0.0, 1.0);  // opaque green
}
