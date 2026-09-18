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
- **Execution synchronises at frame end.** `Execute()` submits and waits (`vkQueueWaitIdle`) before
  returning, so the `async` flag expresses intent and generates the correct cross-queue
  synchronisation, but does not yet overlap work across frames.
- **CPU nodes are producers.** They run host-side before any GPU segment is submitted, so they may
  only feed data into the graph; a GPU-to-CPU dependency is rejected.

## Core concepts

| Concept | Type | Description |
| --- | --- | --- |
| Resource handle | `ResourceHandle` | A stable string name for a resource. |
| Resource desc | `ResourceDesc` | `BufferResourceDesc` / `ImageResourceDesc`; how a graph-owned resource is allocated. |
| Resource use | `ResourceUse` | A node's declaration that it reads/writes a resource (usage, binding, optional explicit descriptor location). |
| Node | `RenderGraphNodeCreateInfo` | A pass: queue, shaders, resources, PSO, execute callback. |
| Builder | `RenderGraphBuilder` | Declares the graph, `Build()` compiles it, `Execute()` runs it. |

## Minimal example

```cpp
RenderGraphBuilder builder(GetRenderDevice());

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

Compute and ray tracing nodes bind resources by **explicit set/binding** declared on the
`ResourceUse`:

```cpp
{.handle = "TLAS",
 .io = ResourceIOType::READ,
 .usage = ResourceUsage::ACCEL_STRUCTURE,
 .kind = ResourceKind::ACCELERATION_STRUCTURE,
 .descriptorBinding = DescriptorBinding{.set = 0, .binding = 1}},
```

Graphics nodes use the three built-in semantic sets instead
(`ResourceBindingSemantic::PER_VIEW` / `PER_OBJ` / `MATERIAL`).

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
