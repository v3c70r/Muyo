struct Push
{
    uint2 inputIndex;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

// Negative control: at module scope Slang treats this declaration as a shader
// parameter, not as heap access. The SPIR-V must contain no heap extension.
StructuredBuffer<float> input = StructuredBuffer<float>.Handle(push.inputIndex);
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    output[tid.x] = input[tid.x];
}
