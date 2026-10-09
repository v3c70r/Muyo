// Outputs its specialization constant as the red channel, so a test can tell a value supplied at
// pipeline creation from the default baked into the SPIR-V. The graphics counterpart of
// testSpecConstant.comp.slang: that one covers the compute stage, this one the pipeline path that goes
// through PipelineStateBuilder.
[[vk::constant_id(0)]] const float kRed = 0.0;

[shader("fragment")]
float4 main() : SV_Target
{
    return float4(kRed, 0.0, 0.0, 1.0);
}
