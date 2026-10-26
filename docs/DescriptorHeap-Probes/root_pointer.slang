struct Root
{
    uint images[128];
    uint samplers[32];
    uint buffers[32];
};

struct Push
{
    Root* root;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

// Host-defined root pointer; handles are function-local.
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    StructuredBuffer<float> input = StructuredBuffer<float>.Handle(uint2(push.root->buffers[0], 0));
    RWStructuredBuffer<float> output = RWStructuredBuffer<float>.Handle(uint2(push.root->buffers[1], 0));
    output[tid.x] = input[tid.x];
}
