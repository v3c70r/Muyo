// Writes both of its specialization constants straight into a storage buffer, so a test can tell a
// value supplied at pipeline creation from the default baked into the SPIR-V. Stored directly rather
// than combined: arithmetic between two constants is a good way to end up measuring constant folding
// instead of specialization.
[[vk::binding(0, 0)]] RWStructuredBuffer<uint> gOut;
[[vk::constant_id(0)]] const uint kMarker = 1234;
[[vk::constant_id(1)]] const uint kScale = 1;

[numthreads(1, 1, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    gOut[0] = kMarker;
    gOut[1] = kScale;
}
