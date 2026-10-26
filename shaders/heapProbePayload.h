#pragma once

// Shared between the B0 ray-generation, closest-hit and miss shaders. The size and layout are the SBT's
// payload contract, not a descriptor; keeping one definition avoids a silent per-stage mismatch.
struct HeapProbePayload
{
    uint hit;
};
