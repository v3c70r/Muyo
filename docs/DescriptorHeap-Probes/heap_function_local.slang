struct Push
{
    uint2 inputIndex;
    uint2 outputIndex;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

// Function-local .Handle() calls lower to heap access.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    StructuredBuffer<float> input = StructuredBuffer<float>.Handle(push.inputIndex);
    RWStructuredBuffer<float> output = RWStructuredBuffer<float>.Handle(push.outputIndex);
    output[tid.x] = input[tid.x];
}
