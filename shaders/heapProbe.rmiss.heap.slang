// B0 probe: miss stage. A miss reports 7 through the payload.

#include "heapProbePayload.h"

[shader("miss")]
void main(inout HeapProbePayload payload)
{
    payload.hit = 7;
}
