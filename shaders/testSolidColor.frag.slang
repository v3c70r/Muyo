// Outputs a solid color supplied via push constants. Used by the render graph tests that need
// distinguishable colors from several passes (blending / write-after-write).

struct SolidColorParams
{
    float4 color;
};

[[vk::push_constant]] ConstantBuffer<SolidColorParams> gParams;

struct FS_OUTPUT
{
    float4 color : SV_Target0;
};

[shader("fragment")]
FS_OUTPUT main()
{
    FS_OUTPUT output;
    output.color = gParams.color;
    return output;
}
