# Muyo RenderGraph

The RenderGraph is a declarative, data-driven frame graph for the Vulkan renderer. A frame is
described as a set of **resources** and **nodes** (passes); the graph derives execution order,
GPU synchronisation and queue scheduling from the declared reads/writes.

It is intended to replace the hand-written `RenderPassManager` pipeline and is the home for
GPU-driven rendering (compute-generated draws, frustum culling), async compute and ray tracing.

## Status

The RenderGraph currently drives the **tests**; `helloVulkan` still renders through
`RenderPassManager`. Note the following while the migration is in flight:

- **Ordering is explicit.** `AddDependency(from, to)` is the only thing that orders nodes; sharing a
  resource does *not* yet create an implicit edge, so declare every ordering you rely on.
- **The builder serialises executions.** `Execute()` records into an in-flight slot, submits, and waits
  for that execution's terminal timeline values before returning. The `async` flag expresses intent and
  generates the correct cross-queue synchronisation, but work does not yet overlap across frames.
  `Execute(RenderGraphExecuteInfo)` submits without waiting, and the executor supports several
  in-flight slots - but the builder runs one (`inFlightCount == 1`), so a second call blocks until the
  first completes. Raising that count needs more than a slot number: descriptor sets are written at
  `Build()` now, so the record-time write hazard is gone, but transient resources are still
  single-instance, so two in-flight executions would alias them.
- **CPU nodes are producers.** They run host-side before any GPU segment is submitted, so they may
  only feed data into the graph; a GPU-to-CPU dependency is rejected.
- **Descriptor sets are derived from shader reflection and content-addressed.** Graphics, compute and
  ray tracing all take one path: a `ResourceUse` names its `(set, binding)`, reflection supplies the
  layout and descriptor type, and the set is keyed on the *concrete views* bound into it. Nodes binding
  the same resources therefore share one set, so a material-binding node no longer costs a bindless array
  each. Sets are rebuilt per `Build()`; frame retention and `framesInFlight` are the design note's next
  step ([DescriptorSet-Lifecycle-Design.md](DescriptorSet-Lifecycle-Design.md)).
- **Device floor.** The engine requires Vulkan 1.3 and enables `synchronization2` and
  `timelineSemaphore` at device creation; both are verified against `vkGetPhysicalDeviceFeatures2`
  and device creation fails with a named error if either is missing. The executor **uses** timeline
  semaphores rather than merely requiring them: completion is a value on a per-queue timeline, so
  `RenderGraphExecuteInfo` carries `waitSemaphore`/`waitValue` and
  `signalSemaphore`/`signalValue`/`signalFence`. A caller can therefore chain one execution onto
  another's completion by passing a timeline it owns, or wait for an execution through a fence, without
  submitting anything of its own to find out.

## Core concepts

| Concept | Type | Description |
| --- | --- | --- |
| Resource handle | `ResourceHandle` | A stable string name for a resource. |
| Resource desc | `ResourceDesc` | `BufferResourceDesc` / `ImageResourceDesc`; how a graph-owned resource is allocated. |
| Resource use | `ResourceUse` | A node's declaration that it reads/writes a resource (usage, binding, optional explicit descriptor location). |
| Node | `RenderGraphNodeCreateInfo` | A pass: queue, shaders, resources, PSO, execute callback. |
| Builder | `RenderGraphBuilder` | Declares the graph, `Build()` compiles it, `Execute()` runs it. |

## Resource lifetimes

Every graph-owned resource declares a `ResourceLifetime`, and the three values differ in what content a
node may rely on:

| Lifetime | Content is valid | Who allocates |
| --- | --- | --- |
| `Transient` (default) | from its **first write to its last read within one execution** - nothing carries to the next one | the graph |
| `Persistent` | across execution boundaries (TAA history, accumulation) | the graph |
| `Imported` | wherever the caller says - the graph never writes it unless a node does | the caller, via `ImportResource` |

`Build()` enforces the first row rather than leaving it as an assumption: a node that **reads a transient
nothing writes first** fails with the node and the resource named, because the read would otherwise take
whatever the memory happens to hold - the first reader's barrier moves an image out of
`VK_IMAGE_LAYOUT_UNDEFINED`, and a transition from `UNDEFINED` discards contents rather than keeping them.
The message names the escape too, since the usual cause is a missing `Persistent`.

The other two are exempt for the same reason: their content is not this execution's to produce. A
graph-owned resource declaring `Imported` is rejected at `AddResource`, because that call is precisely what
makes the graph allocate it.

The check reads each node's **declaration order**, not what its callback does. A node that lists `[READ,
WRITE]` for one transient - the read first - is rejected even if it writes before it reads, because the
graph cannot see inside a callback. Both escapes say what you mean: list the write first, or declare the
single use `READ_WRITE`, which is the flag for "this node both reads and writes it, and the order is its
own business". Conservative is the right default for a graph that cannot look into the callback, but it is
a rule a caller can trip on, so it is worth knowing before the first graph does.

## Declaring a transfer

A node that only moves data - a copy, an upload, a clear, a fill - declares the **role**, not the command:

```cpp
ResourceUse{.handle = "Upload",
            .io = ResourceIOType::TRANSFER_WRITE,
            .usage = ResourceUsage::TRANSFER_DEST,
            .kind = ResourceKind::IMAGE},
```

`TRANSFER_SOURCE` and `TRANSFER_DEST` are the two roles, and the graph moves an image into
`TRANSFER_SRC_OPTIMAL` / `TRANSFER_DST_OPTIMAL` accordingly - so a node passes the layout its use declared
to `vkCmdCopy*` and the transition in between is the graph's.

Two things the table is precise about, because they are easy to get wrong. **A copy destination must say
`TRANSFER_WRITE`**: for a buffer, `WRITE` means a *host* write here, which is how a mapped upload declares
itself, so the two are not interchangeable. And **a clear needs no role of its own**: `vkCmdClear*Image`
and `vkCmdFillBuffer` are transfer writes in the specification's own terms, so `TRANSFER_DEST` covers them
and the node records whichever command it means. A pair with no policy is rejected at `Build()` naming the
resource, both values and the likely fix.

**Aliasing.** Non-overlapping transient live ranges are the resource the graph may share memory across,
which is why the lifetime is a *declaration* rather than a hint: two transients whose ranges do not overlap
may occupy the same bytes, and a `Persistent` one never may.

`Build()` consumes the ranges. **Transient buffers and images alike** are created unbound, their
requirements are queried, and they are bound into one block per compatibility class at offsets the planner
chooses - so two transients whose ranges do not overlap share an address. **They must also run on the same
queue family.** The graph's barriers order a hand-over within one family (a barrier's first scope covers
earlier commands on the same queue, so the new resource's first-use barrier waits on the old resource's last
use), but nothing orders one across families: the executor waits only on a *shared handle*, and an aliased
pair is two. A transient whose uses span families therefore shares its bytes with nothing. An image's
compatibility class is its create info, so two images share a block only when format, extent, mips, layers and
usage all match; both
the graph path and the resource-manager path take the usage flags from `EffectiveImageUsageFlags`, so the two
cannot disagree about what an image is. An image's **view is created only after it is bound** - a view of an
image with no memory bound is a validation error (`VUID-VkImageViewCreateInfo-image-01020`) - so the order is
create unbound, query, plan, bind, view. `GetTransientMemoryPlan()` reports what a build did: `reservedBytes`
is what will be allocated, `individualBytes` is the sum of the transients, and `SharesMemory()` is the
comparison - it answers false for a graph whose transients all overlap, which is a legitimate result rather
than a failure.

Two things to read correctly. **`reservedBytes` can exceed `individualBytes`** on a device with strict
alignment: a slot's offset is aligned up to its widest member's alignment, and alignment limits are device
properties, so padding is real cost and not planner waste. And **a mapping must not outlive the node whose
use declared the resource** - a graph-owned transient is mapped through its block, so nothing else can be
bound to those bytes while the pointer is held, which is exactly what the live range guarantees for that
node and nothing beyond it. `Execute()` **enforces** this rather than only documenting it: after every node
callback it scans the graph's aliased buffers, and one still host-mapped throws naming the node and the
resource. Unmap before the callback returns.

## Minimal example

```cpp
// The graph is given its device, resource allocator and descriptor allocator - it does not look them up.
RenderGraphBuilder builder(RenderGraphServices{
    .device            = GetRenderDevice(),
    .resourceManager   = GetRenderResourceManager(),
    .descriptorManager = GetDescriptorManager(),
});

// There is also RenderGraphBuilder(VkRenderDevice*), which fills the other two from the engine's
// globals. It exists so the in-tree callers that still use them keep working; a consumer that wants to
// run a graph without the renderer's singletons uses the form above.

// Graph-owned resources.
builder.AddResource("Color", ImageResourceDesc{
    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
    .extent = {width, height},
    .usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT});

// Externally owned resources (mesh buffers, scene data, ...).
builder.ImportResource("Vertices", meshManager.m_pVertexBuffer);
builder.ImportResource("Indices",  meshManager.m_pIndexBuffer);

builder.AddNode({
    .nodeName = "OpaquePass",
    .queueType = QueueType::GRAPHICS,
    .resourceUses = {
        {.handle = "Vertices", .io = ResourceIOType::READ,  .usage = ResourceUsage::VERTEX_BUFFER, .kind = ResourceKind::BUFFER},
        {.handle = "Indices",  .io = ResourceIOType::READ,  .usage = ResourceUsage::INDEX_BUFFER,  .kind = ResourceKind::BUFFER},
        {.handle = "Color",    .io = ResourceIOType::WRITE, .usage = ResourceUsage::COLOR_ATTACHMENT, .kind = ResourceKind::IMAGE},
    },
    .shaderNames = {"forward.vert.slang", "forward.frag.slang"},
    .psoDesc = {.depthStencilState = {.depthTestEnable = false}},
    .execute = [](RenderGraphNodeContext& ctx) {
        // The graph has already bound the pipeline, descriptor sets and render pass.
        vkCmdDrawIndexed(ctx.commandBuffer, indexCount, 1, 0, 0, 0);
    }});

builder.Build();
builder.Execute();
```

`Build()` topologically sorts the nodes, allocates every graph-owned resource, compiles pipelines
and descriptor sets, and plans the barriers. `Execute()` records and submits the frame. Nodes with
no edges are still scheduled.

## What the graph handles for you

- **Dependencies and ordering** — `AddDependency(from, to)` declares edges; cycles are rejected.
- **Barriers** — image layouts, access masks and pipeline stages are derived from each resource's
  `ResourceUsage` (see `K_USAGE_POLICIES` in `ResourceUseResolver.h`).
- **Dynamic rendering** — graphics nodes are wrapped in `vkCmdBeginRendering`/`vkCmdEndRendering`
  automatically; the first writer of an attachment clears it.
- **Pipelines and descriptors** — built from the declared shaders, PSO and resource uses.
- **Queue scheduling** — see *Async compute* below.

## Descriptor binding

Every node binds resources into descriptors by **explicit set/binding** declared on the
`ResourceUse`; the graph builds each node's set layouts from the shader reflection:

```cpp
{.handle = "TLAS",
 .io = ResourceIOType::READ,
 .usage = ResourceUsage::ACCEL_STRUCTURE,
 .kind = ResourceKind::ACCELERATION_STRUCTURE,
 .descriptorBinding = DescriptorBinding{.set = 0, .binding = 1}},
```

The engine's built-in sets (`PER_VIEW` / `PER_OBJ` / `MATERIAL`) are a *consumer* convention now,
kept in `src/RenderGraphSemanticBindings.h`, not something the library knows about.

To bind part of a buffer rather than all of it, add a `bufferRange` to the use:

```cpp
{.handle = "Weights",
 .io = ResourceIOType::READ,
 .usage = ResourceUsage::STORAGE_BUFFER,
 .kind = ResourceKind::BUFFER,
 .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0},
 .bufferRange = BufferRange{.offset = 4096, .size = 1024}},   // bytes
```

That becomes the descriptor's `offset` and `range`, so a shader sees one tensor of a packed weights
buffer, or one row of a KV cache, as if it were the whole buffer. Three things bound the feature:

- **It is resolved at `Build()`.** The range is written into the descriptor set once, so changing it
  means rebuilding the graph. A base that is only known per execution is a different mechanism, not a
  different value of this one — see issue #81 for the options and their costs.
- **Alignment is the device's.** `offset` must be a multiple of
  `minStorageBufferOffsetAlignment` (or the uniform equivalent), which may be far larger than the four
  bytes this project's machine reports. A bad range fails at `Build()` naming the limit rather than at
  bind time.
- **It is a binding claim, not a disjointness claim.** Barriers stay per-resource and conservative, so
  two uses of different bytes of one buffer are still ordered against each other. Nothing here makes
  hazard analysis range-aware.

## GPU-driven rendering

A compute node can generate `VkDrawIndexedIndirectCommand`s and an atomic count that a graphics
node then consumes with `vkCmdDrawIndexedIndirectCount`. See
`shaders/prepareDrawCmdBuffer.comp.slang` and the `[AsyncCompute]` test.

## Async compute

Async execution is **opt-in per node**:

```cpp
builder.AddNode({
    .nodeName  = "CullAndBuildDraws",
    .queueType = QueueType::COMPUTE,
    .async     = true,          // may run concurrently with the graphics queue
    ...});
```

When `async` is set on a compute node, `Execute()` splits the frame into per-queue command buffers,
submits them to the dedicated async compute queue and synchronises the handover with a semaphore
plus a queue-family ownership transfer. Nodes that do not set `async` are recorded on the graphics
queue and generate no cross-queue synchronisation.

## Ray tracing

A `QueueType::RAY_TRACING` node is compiled into a ray tracing pipeline with a graph-managed shader
binding table; `Execute()` issues `vkCmdTraceRaysKHR` over the extent of the node's first
`STORAGE_IMAGE`. Provide the raygen/miss/closest-hit shaders via `rtShaderNames` and bind the TLAS
with an explicit `DescriptorBinding`.

## Documentation

- **API reference** — `RenderGraph-api.md` (same directory), generated from the Doxygen comments
  in `src/RenderGraph/*.h`.
- **Coverage** — the `render_graph_docs` build target runs Doxygen with `WARN_AS_ERROR`, so an
  undocumented public entity fails the build.
- Regenerate with `cmake --build <build-dir> --target render_graph_docs`.
