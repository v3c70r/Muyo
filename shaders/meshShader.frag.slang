struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
};

struct FS_OUTPUT
{
    float depth : SV_Depth;
};

[shader("fragment")]
FS_OUTPUT main(VS_OUTPUT input)
{
    FS_OUTPUT output = {0};
    output.depth = input.pos.z;
    return output;
}
