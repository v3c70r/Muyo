// B0 probe: closest-hit stage. A hit reports 42 through the payload.

#include "heapProbePayload.h"

[shader("closesthit")]
void main(inout HeapProbePayload payload, in BuiltInTriangleIntersectionAttributes attribute)
{
    payload.hit = 42;
}
