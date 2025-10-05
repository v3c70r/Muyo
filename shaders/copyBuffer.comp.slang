// Slang compute shader (Vulkan: storage buffers at set=0 bindings 0,1)
// Copies gPC.byteCount bytes from gInput -> gOutput.

[[vk::binding(0, 0)]] ByteAddressBuffer    gInput;   // set 0, binding 0
[[vk::binding(0, 1)]] RWByteAddressBuffer  gOutput;  // set 1, binding 0

// Push constants block (Vulkan)
[[vk::push_constant]] ConstantBuffer<uint> byteCount;

// --- byte helpers (because ByteAddressBuffer exposes 32-bit loads/stores) ---
inline uint readByte(ByteAddressBuffer buf, uint addr)
{
    uint base  = addr & ~3u;
    uint shift = (addr & 3u) * 8u;
    uint word  = buf.Load(base);
    return (word >> shift) & 0xFFu;
}

inline void writeByte(RWByteAddressBuffer buf, uint addr, uint value)
{
    uint base  = addr & ~3u;
    uint shift = (addr & 3u) * 8u;
    uint mask  = ~(0xFFu << shift);

    uint word  = buf.Load(base);
    word       = (word & mask) | ((value & 0xFFu) << shift);
    buf.Store(base, word);
}

[numthreads(256, 1, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= byteCount) return;

    uint b = readByte(gInput, i);
    writeByte(gOutput, i, b);
}
