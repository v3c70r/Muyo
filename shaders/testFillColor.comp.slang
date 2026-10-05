// Fills a storage image with a constant color.
// Used by the test that verifies a later READ_WRITE color attachment preserves content produced
// by an earlier compute pass (i.e. the attachment is loaded, not cleared).

[[vk::binding(0, 0)]] RWTexture2D<float4> gOutput;

[numthreads(8, 8, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    gOutput[dtid.xy] = float4(0.0, 1.0, 0.0, 1.0);  // opaque green
}
