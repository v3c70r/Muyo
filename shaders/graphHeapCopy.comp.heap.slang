// B1b-1: a graph compute node compiled with the heap flag and layout = VK_NULL_HANDLE. The root data
// carries two heap indices; no DescriptorSet/Binding is declared.

struct Push
{
    uint inputIndex;
    uint outputIndex;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[shader("compute")][numthreads(1, 1, 1)] void main(uint3 tid
                                                   : SV_DispatchThreadID)
{
    StructuredBuffer<uint> input = StructuredBuffer<uint>.Handle(uint2(push.inputIndex, 0));
    RWStructuredBuffer<uint> output = RWStructuredBuffer<uint>.Handle(uint2(push.outputIndex, 0));
    output[tid.x] = input[tid.x] + 1;
}
