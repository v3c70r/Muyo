// Slang compute shader (Vulkan: storage buffers at set=0 bindings 0,1)
// Copies gPC.byteCount bytes from gInput -> gOutput.

[[vk::binding(0, 0)]] ByteAddressBuffer    gInput;   // set 0, binding 0
[[vk::binding(0, 1)]] RWByteAddressBuffer  gOutput;  // set 1, binding 0

// Push constants block (Vulkan)
[[vk::push_constant]] ConstantBuffer<uint> byteCount;

[numthreads(256, 1, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= byteCount / 4) return;

    // each thread copies one uint (4 bytes)
    uint value = gInput.Load(i * 4);
    gOutput.Store(i * 4, value);
}
