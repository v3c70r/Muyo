// 16-bit inside a storage-buffer block, which demands UniformAndStorageBuffer16BitAccess rather than
// StorageBuffer16BitAccess. Paired with test16BitStorage.comp so that both features #78 enables are
// proven by SPIR-V that actually requires them, instead of by the device's own bookkeeping.

[[vk::binding(0, 0)]] RWStructuredBuffer<half> gOutput;

[numthreads(1, 1, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    gOutput[dtid.x] = half(1.0);
}
