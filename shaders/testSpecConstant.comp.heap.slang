// B1b-5b-5b: writes both specialization constants into the output buffer through a heap handle, so a
// test can tell a value supplied at pipeline creation from the default baked into the SPIR-V. Stored
// directly rather than combined: arithmetic between two constants is a good way to end up measuring
// constant folding instead of specialization.

struct Push
{
    uint2 output;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[[vk::constant_id(0)]] const uint kMarker = 1234;
[[vk::constant_id(1)]] const uint kScale = 1;

[numthreads(1, 1, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    RWStructuredBuffer<uint> output = RWStructuredBuffer<uint>.Handle(push.output);
    output[0] = kMarker;
    output[1] = kScale;
}
