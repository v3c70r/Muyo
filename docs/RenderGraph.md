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
- **In-flight executions are opt-in.** `Execute()` records into a slot, submits, and waits for that
  execution's terminal timeline values; `Execute(RenderGraphExecuteInfo)` submits without waiting. The
  builder defaults to one slot. Passing `inFlightCount = 2` gives each slot its own command buffers and
  root arena, and **imported** resources are bound per execution from that slot's descriptor ring, so a
  caller may re-point an imported handle between executions without a Build (#83). Graph-owned transients
  and persistent resources are still single-instance: a graph that declares them must stay at one
  in-flight execution, because two overlapping executions would alias them (#40).
- **CPU nodes are producers.** They run host-side before any GPU segment is submitted, so they may
  only feed data into the graph; a GPU-to-CPU dependency is rejected.
- **The graph path is descriptor-heap-only.** B1b removed descriptor sets, layouts, pools and pipeline
  layouts from `src/RenderGraph/`; every graphics, compute and ray-tracing pipeline is created with
  `VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT` and `layout = VK_NULL_HANDLE`. A node declares the
  resources it uses (`ResourceUse`, with an optional buffer range); the graph writes their descriptors
  into the injected `DescriptorHeapService` at `Build()` and exposes the slot through
  `GetHeapIndex`. Root data and push data are opaque bytes: a node writes them with
  `RenderGraphNodeContext::PushData` (and optionally `AllocateRoot`). Nothing in the graph path calls
  `vkCmdBindDescriptorSets`, `vkCmdPushConstants`, `vkCreateDescriptorSetLayout` or
  `vkCreatePipelineLayout`; `scripts/check_descriptor_path.py` enforces that in CI. The legacy
  `RenderPassManager` keeps its descriptor-set path outside the graph. The historical descriptor-set
  design (`DescriptorSet-Lifecycle-Design.md`) is kept in the repository, outside this reference.
- **Device floor.** The engine requires Vulkan 1.4 and enables `synchronization2` and
  `timelineSemaphore` at device creation; both are verified against `vkGetPhysicalDeviceFeatures2`
  and device creation fails with a named error if either is missing. The descriptor-heap path also
  requires `VK_EXT_descriptor_heap` (`descriptorHeap`) and `VK_KHR_shader_untyped_pointers`
  (`shaderUntypedPointers`), verified at device creation; there is no descriptor-set fallback. The executor **uses** timeline
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
| Resource use | `ResourceUse` | A node's declaration that it reads/writes a resource (usage, optional buffer range). |
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
`TRANSFER_WRITE`**, and **a host write must say `HOST_WRITE`**: for a buffer, `WRITE` is a *shader* store, so
none of the three is interchangeable. And **a clear needs no role of its own**: `vkCmdClear*Image`
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
// The graph is given its device, resource allocator and descriptor-heap service - it does not look
// them up.
DescriptorHeapService heap;
heap.Initialize(GetRenderDevice());
RenderGraphBuilder builder(RenderGraphServices{
    .device          = GetRenderDevice(),
    .resourceManager = GetRenderResourceManager(),
    .descriptorHeap  = &heap,
});

// There is also RenderGraphBuilder(VkRenderDevice*), which fills the resource manager from the engine's
// globals and creates an owned heap service when the device supports the extension. A consumer that
// wants to run a graph without the renderer's singletons uses the form above.

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
    .shaderNames = {"forward.vert.heap", "forward.frag"},
    .psoDesc = {.depthStencilState = {.depthTestEnable = false}},
    .execute = [](RenderGraphNodeContext& ctx) {
        // The graph has already bound the pipeline, the heaps and the render pass.
        vkCmdDrawIndexed(ctx.commandBuffer, indexCount, 1, 0, 0, 0);
    }});

builder.Build();
builder.Execute();
```

`Build()` topologically sorts the nodes, allocates every graph-owned resource, writes the heap
descriptors, compiles pipelines and plans the barriers. `Execute()` records and submits the frame.
Nodes with no edges are still scheduled.

## What the graph handles for you

- **Dependencies and ordering** — `AddDependency(from, to)` declares edges; cycles are rejected.
- **Barriers** — image layouts, access masks and pipeline stages are derived from each resource's
  `ResourceUsage` (see `K_USAGE_POLICIES` in `ResourceUseResolver.h`). Two details matter when
  declaring a use: `WRITE` is a **shader** store and `HOST_WRITE` is a write through a mapping — a `CPU`
  node must use the latter, and a host write is rejected on device-only memory — and a stage the device
  does not enable is filtered out of the mask rather than emitted.
- **Dynamic rendering** — graphics nodes are wrapped in `vkCmdBeginRendering`/`vkCmdEndRendering`
  automatically; the first writer of an attachment clears it.
- **Pipelines and descriptors** — built from the declared shaders, PSO and resource uses.
- **Queue scheduling** — see *Async compute* below.

## Pipeline caching

`Build()` compiles a `VkPipeline` for every graphics and compute node. The cache key is the **full
pipeline state** — the `PSODesc`, the shader SPIR-V content, the attachment formats, the specialization
values and the queue type — as a byte string, not a hash of a subset. Two declarations that differ in
anything the driver reads therefore get different pipelines; nothing descriptor- or layout-related is in
the key, because the heap path passes `layout = VK_NULL_HANDLE` and binds no sets. Ray tracing pipelines
are not cached: their shader binding table is per node.

Without any extra object a builder caches its own pipelines for as long as it lives, which is what
`helloVulkanRG` relies on: it keeps one builder and calls `Build()` per frame. To share pipelines
**across builders and graphs** — a second graph, or a builder recreated after a swapchain change — inject
a caller-owned `PipelineCache`:

```cpp
PipelineCache pipelines(GetRenderDevice());   // device-scoped, outlives the builders
RenderGraphBuilder a(RenderGraphServices{
    .device = GetRenderDevice(), .resourceManager = GetRenderResourceManager(), .pipelineCache = &pipelines});
RenderGraphBuilder b(RenderGraphServices{
    .device = GetRenderDevice(), .resourceManager = GetRenderResourceManager(), .pipelineCache = &pipelines});
```

The cache owns the entries and a builder only borrows them, so destroying one builder — or calling its
`ReleaseCachedDeviceObjects()` on a swapchain recreation — does not invalidate a pipeline another graph
is still recording with. Two rules follow from that ownership:

- The cache must outlive every builder that refers to it, and the device must be idle before the cache
  is destroyed or reset. Neither the builder nor the cache retains a reference count.
- A cache belongs to one `VkDevice`. A builder given a cache created on another device rejects it at
  construction rather than borrowing a handle the driver cannot use.

A builder that is given no cache keeps its existing per-builder behaviour, so a small caller or a test
pays for nothing it does not use.

### Serializing the driver cache

The pipeline cache also owns a `VkPipelineCache`, passed to every graphics, compute and ray-tracing
creation, so the driver can reuse its own compiled state across runs. (A ray-tracing `VkPipeline` is still
not kept in the graph's map — its shader binding table is per node — but the driver cache records that
creation like any other.) The graph does not decide where those bytes live:

```cpp
PipelineCache pipelines(GetRenderDevice());
// ... declare, Build(), Execute() ...
const std::vector<std::uint8_t> blob = pipelines.GetSerializedData();
// the caller writes blob wherever it likes

// next run
PipelineCache pipelines(GetRenderDevice(), blob);
```

`GetSerializedData()` returns what `vkGetPipelineCacheData` produces; the span constructor hands those
bytes to `vkCreatePipelineCache`. Saving and loading the file is caller policy, exactly as it is in
`src/app/PSOCompiler.cpp`. The bytes are meaningful only on the device that produced them — Vulkan's
header carries the vendor, device and pipeline-cache UUID — and a reloaded cache starts with an empty map
of `VkPipeline` handles: the driver's data is reused, the process-local handles are not. The device must
be idle while data is read, like any other host access to a `VkPipelineCache`.

## Shader caching

Loading a shader means reading its `.spv`, creating a `VkShaderModule` and running SPIR-V reflection;
`Build()` needs the reflection to derive heap indices and push-constant ranges. Without a shared cache
each builder repeats that work and owns its modules. Inject a caller-owned `ShaderCache` to share both:

```cpp
ShaderCache shaders(GetRenderDevice()->GetDevice());
RenderGraphBuilder a(RenderGraphServices{
    .device = GetRenderDevice(), .resourceManager = GetRenderResourceManager(), .shaderCache = &shaders});
RenderGraphBuilder b(RenderGraphServices{
    .device = GetRenderDevice(), .resourceManager = GetRenderResourceManager(), .shaderCache = &shaders});
```

An entry is keyed by the shader **name and a hash of its SPIR-V words**, not by the name alone. A `.spv`
rebuilt while the process is running is therefore a new entry instead of a stale reflection with a new
module, and a builder that still refers to the old entry keeps it alive. A hit creates no module and runs
no reflection; `ShaderCache::GetModuleCreateCount()` is the observable reuse. The cache owns the modules
and must outlive every builder that uses it. `ShaderCache::LoadShader()` still reads the file to identify
its content, since that is the only way to know whether it changed.

Both caches are independent: a builder may inject either, both, or neither. A `PipelineCache` caches the
compiled result (keyed on shader content); a `ShaderCache` caches the loaded input.

## Descriptor binding

Every node declares the resources it uses; the graph writes their heap descriptors at `Build()` and
resolves the slot for the current execution. The node puts the slot in its own root data:

```cpp
const std::optional<uint32_t> tlas = ctx.GetHeapIndex("TLAS", ResourceUsage::ACCEL_STRUCTURE);
REQUIRE(tlas.has_value());
struct Push
{
    uint2 tlas;
};
const Push push{*tlas, 0};
ctx.PushData(0, std::as_bytes(std::span{&push, 1}));
```

The graph imposes no set or binding convention; the root layout is the host's. The engine's built-in
sets (`PER_VIEW` / `PER_OBJ` / `MATERIAL`) are a legacy `RenderPassManager` convention, kept in
`shaders/shared/RenderGraph/bindings.h`, not something the library knows about.

To bind part of a buffer rather than all of it, add a `bufferRange` to the use:

```cpp
{.handle = "Weights",
 .io = ResourceIOType::READ,
 .usage = ResourceUsage::STORAGE_BUFFER,
 .kind = ResourceKind::BUFFER,
 .bufferRange = BufferRange{.offset = 4096, .size = 1024}},   // bytes
```

That becomes the heap descriptor's `offset` and `range`, so a shader sees one tensor of a packed
weights buffer, or one row of a KV cache, as if it were the whole buffer. Three things bound the feature:

- **It is resolved at `Build()`.** The range is written into the heap descriptor once, so changing it
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

## Failure index

Every declaration error is reported at the call that declared it or at `Build()`, before any GPU work, and
**each message names the graph entity** — the node, the resource, the binding — rather than a Vulkan
handle. Find the bracketed part of the message in this table to see its cause and fix.

`Build()` validates in this order: cycle → transient lifetimes → descriptor bindings → allocation. A test
for a later check therefore has to satisfy the earlier ones (usually by declaring the resources
`Persistent`).

| Message contains | Cause | Fix |
| --- | --- | --- |
| `AddResource('X') declares a buffer with count 0` / `stride 0` | R1 | Give the buffer a non-zero `count` and `stride`. |
| `... declares an image with a zero extent` | R2 | Give the image a non-zero `extent`. |
| `... declares an image with VK_FORMAT_UNDEFINED` | R2 | Choose a real `format`. |
| `... declares an image with 0 mips and … layers` | R2 | `mips` and `layers` are at least 1. |
| `... declares usage 0` | R3 | Set the Vulkan usage flags the resource needs. |
| `... declares lifetime Imported, but AddResource …` | lifetime | Use `ImportResource` for a resource the graph does not own. |
| `Node 'N' uses resource 'R' which is neither declared … nor imported` | use | Declare it with `AddResource` or `ImportResource`. |
| `Node 'N' uses resource 'R' as a buffer, but the resource is an image` | U1 | Fix `ResourceUse::kind`. |
| `Node 'N' uses image 'R' as STORAGE_IMAGE, but it was declared without VK_IMAGE_USAGE_STORAGE_BIT` | U2 | Add the flag the role needs to the declaration. |
| `... which is not a buffer usage` | U2 | The role is image-only (or buffer-only); use a role that matches the kind. |
| `... is a CPU node and declares WRITE of buffer 'R'` | host write | Declare `HOST_WRITE`: a CPU node writes through the host, and `WRITE` is a shader store. |
| `... is a CPU node and declares WRITE of image 'R'` | host write | There is no host image write; stage it and copy with `TRANSFER_DEST`. |
| `... declares HOST_WRITE of image 'R'` | host write | There is no host image write; stage it and copy with `TRANSFER_DEST`. |
| `... declares HOST_WRITE of buffer 'R', which was declared with device-only memory` | host write | Declare the buffer `VMA_MEMORY_USAGE_CPU_TO_GPU` or `CPU_ONLY`. |
| `Node 'N' needs the descriptor heap, but RenderGraphServices::descriptorHeap is null` | heap | Inject a `DescriptorHeapService`; the graph path is heap-only since B1b. |
| `Node 'N' declares descriptor bindings; the graph path is heap-only since B1b` | heap | Convert the shaders to a `.heap.slang` variant. |
| `Node 'N' reads transient resource 'R' with nothing writing it first` | lifetime | Add the writer, or declare the resource `Persistent`. |
| `Node 'N' … is a GPU node with no execute callback` | shape | Give the node an `execute` callback. |
| `Graphics node 'N' declares K shader(s)` / `Compute node 'N' declares K shader(s)` | shape | A graphics node takes vertex + fragment, a compute node exactly one. A node with no shader is allowed. |
| `Node 'N' attaches 'A' at WxH and 'B' at WxH` | extent | One framebuffer needs one extent. |
| `Render graph contains a cycle` | dependency | Remove the edge that closes the cycle. |

An `async` compute node on a device without a dedicated compute family is **not** an error: it still gets
its own compute segment, but on the graphics family, so it is serialised rather than concurrent. `Build()`
warns once, and `VkRenderDevice::IsComputeQueueDedicated()` is the query an app uses to choose.

## Documentation

- **API reference** — `RenderGraph-api.md` (same directory), generated from the Doxygen comments
  in `src/RenderGraph/*.h`.
- **Coverage** — the `render_graph_docs` build target runs Doxygen with `WARN_AS_ERROR`, so an
  undocumented public entity fails the build.
- Regenerate with `cmake --build <build-dir> --target render_graph_docs`.
