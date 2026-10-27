// B1b-5b-3: buffer copy on the descriptor-heap path. `byteCount` and the two buffer indices arrive in
// the node's push data; no DescriptorSet/Binding is declared.

struct Push
{
    uint byteCount;
    uint input;
    uint output;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[numthreads(256, 1, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    ByteAddressBuffer input = ByteAddressBuffer.Handle(uint2(push.input, 0));
    RWByteAddressBuffer output = RWByteAddressBuffer.Handle(uint2(push.output, 0));

    uint i = dtid.x;
    if (i >= push.byteCount / 4) return;

    // each thread copies one uint (4 bytes)
    uint value = input.Load(i * 4);
    output.Store(i * 4, value);
}
