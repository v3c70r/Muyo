// B0 probe: descriptor-heap compute path.
//
// Reads a structured buffer, a uniform texel buffer and a push-data addend, and writes the result through
// function-local heap handles. No DescriptorSet/Binding decorations are emitted; the `.heap.spv` gate
// checks that, because a module-scope `.Handle()` would silently lower back to a parameter binding.

struct Push
{
    uint inputIndex;
    uint outputIndex;
    uint texelIndex;
    uint addend;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    StructuredBuffer<uint> input = StructuredBuffer<uint>.Handle(uint2(push.inputIndex, 0));
    Buffer<uint> texel = Buffer<uint>.Handle(uint2(push.texelIndex, 0));
    RWStructuredBuffer<uint> output = RWStructuredBuffer<uint>.Handle(uint2(push.outputIndex, 0));
    output[tid.x] = input[tid.x] + texel[tid.x] + push.addend;
}
