# RenderGraph as a library: target architecture

Companion to [Roadmap.md](Roadmap.md). The roadmap says *when*; this says *what the boundary should be*.
No part of this is implemented — it is the shape to build toward, written down while there is one
consumer so the second one is not what discovers it.

## The stack

The proposal was `RenderDevice → RenderGraph → RenderPipeline`. That is right, with three corrections:
a fourth layer above it, a boundary that is per-concern rather than a single line, and one piece of
app policy that has to come out of the device layer.

```
  ┌──────────────────────────────────────────────────────────────┐
  │ host / app        window, swapchain, frame pacing, present,  │
  │                   input, ImGui                           (a) │
  ├──────────────────────────────────────────────────────────────┤
  │ render pipeline   a specific arrangement of nodes:           │
  │                   GBuffer → lighting → transparent → UI       │
  │                   no device calls, no present             (b) │
  ├──────────────────────────────────────────────────────────────┤
  │ muyo_rg           resources and lifetimes, barriers, the     │
  │  (RenderGraph)    execution plan and executor, pipeline and  │
  │                   descriptor compilation                  (c) │
  ├──────────────────────────────────────────────────────────────┤
  │ muyo_device       instance, device, queues, memory, sync     │
  │  (RenderDevice)   primitives, command pools               (d) │
  └──────────────────────────────────────────────────────────────┘
```

**(a) The host is a layer and it owns presentation.** This is the same conclusion as the A1.5 split in
the roadmap: acquire, images-in-flight and present ids belong to whoever owns a window. A test harness,
an offline renderer and the future tool are all hosts too, which is why it is a layer rather than a
detail of the renderer.

**(b) The render pipeline is a consumer of the public API only.** If it cannot be written against the
public surface, the surface is wrong. This is the boundary test the roadmap asks for, and it is
checkable: `helloVulkan`'s passes must not include anything under `src/RenderGraph/` that is not public.

**(c) The graph owns orchestration, not the device.**

**(d) The device layer is Vulkan.** The graph is Vulkan-shaped on purpose — its public API carries
`VkImageLayout`, `VkCommandBuffer`, pipeline bind points — so an abstract RHI would be an abstraction
with one implementation. That is a cost with no benefit until a second backend exists.

## Where the line goes, per concern

The mistake to avoid is drawing one line and calling it "the boundary". Each concern has its own, and
several are currently on the wrong side of it.

| Concern | Owner | Received by injection | Note |
| --- | --- | --- | --- |
| Instance, device, queues, memory, sync primitives, command pools | **device** | — | Already exists; needs a target of its own rather than a header inside the engine. |
| Queue *use* — which segment runs where | **graph** | queue-family facts | The graph plans; it must not *require* a graphics family (compute-only profile). |
| Transient resources, aliasing, lifetimes | **graph** | allocator | The policy is the graph's; the memory is not. |
| Barriers and layouts | **graph** | — | The reason the graph exists. |
| Pipeline creation | **graph** | shader source provider, pipeline cache | It builds pipelines from node descriptions today. Keep that — but a client should be able to supply SPIR-V bytes and a cache, not only a file name on disk. |
| Descriptor sets | **graph** | descriptor allocator | Per #15's content-addressed design. |
| Swapchain, present, frame pacing | **host** | — | Not the graph's, not the pipeline's. |
| Validation-layer enablement | **host** | — | See below; this is currently the device's and should not be. |

## Three decisions

### 1. C++ headers now; a C ABI is a later wrapper

Decided: the public surface is C++ headers, and the library is open source. If ABI stability or
cross-language use is needed later, a C ABI can be wrapped *around* it — that is a strictly additive move,
whereas designing for it now would constrain the API for a client that does not exist.

Recorded with its trigger so the decision can be revisited rather than drifted into: wrap a C ABI when
there is a consumer that cannot use C++ headers.

### 2. The device is a shared library, not an inverted dependency

Two ways to get a clean boundary:

- **shared library (chosen)** — `muyo_device` is its own target with a documented surface, and `muyo_rg`
  links it. Both ship. Simple, and honest about the graph being Vulkan-only.
- dependency inversion — the graph declares the device interface it needs and the device implements it.
  More flexible, and pays indirection for a second backend that does not exist.

Choose the shared library, and revisit on the same trigger as the C ABI: a client that cannot use
`muyo_device`. The current problem is not the *direction* of the dependency — it is that there is no
target boundary at all (13 cross-includes, 15 singleton call sites). Fix the boundary, keep the direction.

### 3. Error reporting is unresolved, deliberately

`FatalError` and `FatalAbort` abort the process. That is defensible for a renderer, where every failure
is unrecoverable, and hostile to a long-running inference service that links the library and would rather
lose one request than the process. Options are to return a result from the public entry points, to throw
on the graph-level failures (the graph already throws for declaration mistakes) and keep aborting only
for Vulkan call failures, or to let the host install a handler.

Not decided. Recorded as a design question rather than left implicit, because it is the kind of thing
that gets decided by whoever writes the next call site.

## What the device layer must shed

`VkDebugRenderDevice::Initialize` appends the validation layer and extension **unconditionally**, and
`DebugUtilsMessenger` is created with the instance. A library's client should choose whether it wants a
validation layer, a debug messenger, and their cost — an inference service does not.

So: instance/device/queues/memory stay in the device layer; **validation policy moves to the host**, with
the device offering a debug-messenger hook it can be given. Until then, every consumer of the library
gets validation whether it asked or not.

## Consumers include coding agents

Stated as a requirement, and it is a stronger one than "good errors" — it changes what an error and a
document have to contain. An agent reading a failure has no debugger, no replay, no ability to ask a
follow-up question, and reads the message in a log. Three consequences, all cheap while the graph is
small:

**Fail at `Build()`, not at the GPU.** A graph can check its own declarations before recording anything:
every read has a writer, no cycles, attachment extents agree, descriptor bindings match reflection, the
queue families a node asks for exist. Each of those is a *declaration* error, and reporting it at Build
points at the line that is wrong. Today several of them surface as a validation error during execution,
several frames later, naming a handle.

**Name the graph entity, not the handle.** The validation layer can only say
`VkBuffer 0x…`. The graph knows which node and which resource that is.

```
[RenderGraph] node 'ShadowPass' reads 'DepthTarget' (texture, 2048x2048, D32)
              which node 'GBuffer' writes as R16_UNORM (1024x1024)
              -> extents must match; see docs/RenderGraph-Errors.md
```

**Attribute GPU errors for free with debug labels and object names.** The engine already has
`ScopedMarker` and `setDebugUtilsObjectName`; if the graph brackets every node and names every resource
it creates, then a VUID arrives *already attributed*:

```
Validation Error: ... vkCmdDrawIndexed(): ... [node: ShadowPass, resource: ShadowMap0]
```

For an agent that is the difference between "read the message and act" and "reproduce under RenderDoc".
It is also what the vendor tools need, so it pays twice.

**And make the docs actionable.** Doxygen coverage is already enforced for every public entity; the bar
for a library is one step further — each entry states its **preconditions** and its **failure modes**
(what makes it throw, and what the caller must guarantee), not only what it does. Plus an index of
failures with their fixes, in the shape of AGENTS.md's gotchas section, because an agent resolves an
error by searching for it.

## Tooling: a trace first, vendor tools second

Not in scope now — the tool is a separate project, and it may be a graph node with ImGui or a headless
CLI. Two things in the library make either possible, and one of them is worth designing early because it
is also our own test story.

**An execution-plan trace.** The graph already computes the plan: segments, queue families, transfers,
timeline values, resource dependencies. Emitting that as machine-readable data (not a picture) is
backend-agnostic, costs one serializer, and powers the visual tool, the headless CLI, and the tests —
"the second execution waited on value 4 of timeline Q" is a fact a test can assert and an agent can read.

**A capture-tool abstraction.** RenderDoc covers general validation but replay shows neither ray tracing
nor queue scheduling; the vendor profilers cover those (Radeon GPU Profiler, Nsight) and disagree
platform to platform. So: one small interface — begin/end capture, markers, resource annotations — with
a backend per tool and a no-op default. The graph already emits markers through `ScopedMarker`; routing
them through that interface means every backend gets them, and the per-platform scoping lives in the
backend rather than in the graph.

The reason to note this now rather than when the tool is built: the trace and the markers are the
*interface*, and retrofitting an interface onto code that never emitted one is the expensive version.
