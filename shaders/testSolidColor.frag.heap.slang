// B1b-5b-4: solid color on the descriptor-heap path. The color is the tail of the shared camera push
// block so the vertex stage (testWorldPos.vert.heap) and this fragment see one blob; offsets 0..143 are
// the camera root data. No DescriptorSet/Binding is declared.

struct Push
{
    float4x4 mView;       // testWorldPos.vert.heap reads these; unused here but part of the block
    float4x4 mProj;
    uint2    perObjData;
    uint2    padding;     // bytes 136..143, keeps solidColor at 144
    float4   solidColor;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

[shader("fragment")]
float4 main() : SV_Target
{
    return push.solidColor;
}
