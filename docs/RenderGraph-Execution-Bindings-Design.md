# Execution bindings: per-execution resources, push constants and dynamic state

**Status: design only, not implemented.** This note records a design discussion so it survives the
sessions that will implement it. It is deliberately **not** in the Doxygen `INPUT` list: unimplemented
designs are kept out of `docs/RenderGraph-api.md` (AGENTS.md section 3), and this file must not be linked
from a file that *is* in that set (`docs/RenderGraph.md` is).

**Trigger:** revisit this after the `helloVulkan` migration ([#20](https://github.com/v3c70r/Muyo/issues/20)).
The first demonstrated case already exists in the tests (see *Why now, and why not now*), so the trigger is
a scheduling decision rather than a missing consumer.

Related: [#83](https://github.com/v3c70r/Muyo/issues/83) (the priced mechanism table this note builds on),
[#40](https://github.com/v3c70r/Muyo/issues/40) (in-flight executions), [#57](https://github.com/v3c70r/Muyo/issues/57)
(presentation belongs to the host), [#59](https://github.com/v3c70r/Muyo/issues/59) (cost criterion),
[#17](https://github.com/v3c70r/Muyo/issues/17) (pipeline/shader caching), [#197](https://github.com/v3c70r/Muyo/issues/197)
(trace timings), and [`DescriptorSet-Lifecycle-Design.md`](DescriptorSet-Lifecycle-Design.md).

## The problem

`Build()` fixes everything a node binds: `ResourceUse` is resolved once (`RenderGraphBuilder.cpp`,
`CompileRenderGraphNode`), descriptor contents are written at `Build()` (`BuildReflectionDescriptorSets`),
and `Execute()` only reads the compiled node. A resource that rotates per frame therefore cannot be
expressed without rebuilding:

- The acquired swapchain image changes every frame. `src/tests/FrameSyncTest.cpp` builds a graph per frame
  for exactly this reason, and its comment already names the missing feature: *"the graph fixes its imports
  at Build; caching one graph per image is the optimisation and belongs with #40."*
- A per-frame uniform buffer (a ring of `framesInFlight` slices) has the same shape: the buffer is stable,
  the *offset* is not.
- `Build()` is a full recreate: it destroys the compiled graph, the aliased transients and the descriptor
  set cache, reallocates resources, recompiles every node (pipelines included) and re-derives the execution
  plan (`RenderGraphBuilder.cpp:1542`). The per-use barrier batch is separate: it is derived in
  `RecordBarriers` on every `Execute()`, rebuild or not. Rebuilding per frame pays all of the above every
  frame.

`inFlightCount` is 1 (`RenderGraphExecutor.h:90`), so even a single-execution-per-frame loop cannot overlap
CPU recording with GPU work yet. Raising it is [#40](https://github.com/v3c70r/Muyo/issues/40)'s subject and
needs per-execution state, which is the same mechanism this note describes.

## The model: three lifetimes

| Lifetime | Owner | Changes when | Holds |
| --- | --- | --- | --- |
| **Declaration** | the host | a pass/material/resource is added or removed | resources, nodes, dependencies, `PSODesc`, shader names |
| **Compiled plan** | the graph | the declaration or the attachment formats change | execution order, barrier plan, pipeline and descriptor *layouts*, pipeline objects (cached) |
| **Execution state** | one in-flight execution | every frame | command buffers, transient arena slot, descriptor set instances, **execution bindings** |

The rule that makes multiple frames cheap: **structure at `Build()`, concrete handles at `Execute()`**. A
graph is built once per declaration, not per frame. A rebuild is reserved for a declaration change, a
device change, or an attachment-format change; it is made cheap by the caches in
[#17](https://github.com/v3c70r/Muyo/issues/17), not avoided by caching the whole graph.

`ResolveResource()` is already called at **record time** by the barrier path, `BeginRendering` and
`RecordPresentTransition`. That is why the attachment case needs no descriptor work: the view is resolved
when the command buffer is recorded, so an execution binding is enough.

## 1. Resource slots

A slot is a graph-level declaration of "some resource will be bound here, per execution". It is **not**
allocated. Nodes reference it in `ResourceUse` exactly as they reference an imported resource today — one
mapping mechanism, not two.

```cpp
// Graph level. A slot describes what may be bound; it owns nothing.
builder.AddExecutionResource("Present", ExecutionImageDesc{
    .format = swapchainFormat,                       // pipeline attachment compatibility
    .usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .extent = ExtentPolicy::FromBinding});           // resize does not rebuild

builder.AddExecutionResource("PerView", ExecutionBufferDesc{
    .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    .range = RangePolicy::DynamicOffset});

// Node level: unchanged shape.
builder.AddNode({.nodeName = "Opaque",
                 .resourceUses = {
                     {.handle = "Present", .io = ResourceWrite, .usage = ResourceUsage::PRESENT,
                      .kind = ResourceKind::IMAGE},
                     {.handle = "PerView", .io = ResourceRead, .usage = ResourceUsage::UNIFORM_BUFFER,
                      .kind = ResourceKind::BUFFER,
                      .descriptorBinding = DescriptorBinding{.set = 0, .binding = 0}}},
                 .execute = ...});

// Execution level: the concrete handles for this frame.
RenderGraphExecutionBindings bindings;
bindings.Bind("Present", swapchain.GetImageResource(frame.imageIndex));
bindings.Bind("PerView", perFrameBuffer, frameSlot * stride, stride);
builder.Execute(bindings, sync.ExecuteInfo(frame));
```

`Build()` resolves a use against the **declaration**: kind, role, declared format, descriptor location,
barrier policy. `Execute()` resolves it against the **binding**: the concrete resource, its format and
extent, and the dynamic offset. `RenderGraphNodeContext::GetResource()` answers with the bound resource, so
node callbacks need no change.

### Mechanism per kind

The mechanism follows from the resource kind, and this is the part worth not generalising:

| Slot kind | Mechanism | Cost |
| --- | --- | --- |
| Image used only as an attachment or present target | resolve the view at record time; `VkRenderingAttachmentInfo` and the barriers are already built there | **zero** — no descriptor involved |
| Image **sampled or stored** (history, per-frame shadow map) | per-execution descriptor set instance; the content-addressed key already includes the concrete resource's identity, so the cache holds one per in-flight execution | one set per execution, retained until that execution's timeline value completes |
| Buffer, whole-buffer slot | the resource identity changes the set key | one set per bound buffer |
| Buffer, ranged slot | `*_DYNAMIC` descriptor + `pDynamicOffsets` in the `vkCmdBindDescriptorSets` that `Execute()` already emits | **zero** descriptor writes; the set stays shared |

The buffer row is why "write a new set per execution" is not the general answer: a per-frame UBO ring is
free with dynamic offsets, and [#83](https://github.com/v3c70r/Muyo/issues/83) already prices the
alternatives (dynamic offset, push descriptor, per-slot rewrite, rebuild). The image row is the one that
needs the descriptor retention already planned in
[`DescriptorSet-Lifecycle-Design.md`](DescriptorSet-Lifecycle-Design.md) (`framesInFlight` scope, release at
frame end).

`ExecutionBufferDesc::range = DynamicOffset` makes the graph map the reflected `UNIFORM_BUFFER` /
`STORAGE_BUFFER` type to its `*_DYNAMIC` form when it builds the set layout. A static `bufferRange` (#82)
and a dynamic offset compose: Vulkan adds the dynamic offset to the descriptor's static offset.

### Validation

Both sides are where this design earns its keep:

| At `Build()` | At `Execute()` |
| --- | --- |
| the slot is declared and used | every used slot is bound |
| the use's `kind` and `usage` agree with the declaration | the bound resource's kind and image format match the declaration |
| the declared format matches the attachment format the pipeline is created for | multiple attachments resolve to the same extent |
| `DynamicOffset` is only declared for uniform/storage buffers | the offset is aligned to `minUniformBufferOffsetAlignment` / `minStorageBufferOffsetAlignment`, naming the limit |
| a slot a node uses but no node ever binds is still a declaration error at the first `Execute()` | a missing binding throws **before** any command buffer is recorded, so there is no partial submission |

A binding is validated, never trusted: the declaration's format is what the pipeline was built for, and the
bound image's `GetImageFormat()` is what it actually is.

### Rules

1. **One binding per slot per execution.** A slot may be used by many nodes; that is its purpose.
2. **No mid-execution rebinding.** Descriptor contents must not change while a command buffer that binds
   them is pending. Per-node variation is bindless arrays plus a push constant (below), not a rebind.
3. **The graph does not own the bound resource.** Its lifetime is the caller's, until the execution's
   completion is observable.
4. **No swapchain, surface or present types in the API.** The slot is "an image with `PRESENT` usage"; the
   host binds its own image ([#57](https://github.com/v3c70r/Muyo/issues/57)). A library whose second
   consumer is an inference job must not contain presentation machinery.

## 2. Push constants

The pipeline layout's push-constant ranges already come from merged shader reflection
(`RenderGraphBuilder.cpp:344`), but there is no node-facing way to supply values: a callback must know the
offsets and call `vkCmdPushConstants` itself. Give it a typed surface.

```cpp
// Graph level: one block for every pipeline layout, so layouts can be shared.
builder.DeclarePushConstantBlock({.size = 64, .stageFlags = VK_SHADER_STAGE_ALL});
builder.AddPushConstantSlot({.name = "Frame",    .offset = 0,  .size = 16});
builder.AddPushConstantSlot({.name = "DrawData", .offset = 16, .size = 32});

// Node: which slots its shaders consume, and optional build-time bytes.
node.pushConstantSlots = {"Frame", "DrawData"};
node.pushConstantOverrides = {{"DrawData", materialBytes}};

// Execution: values for the per-execution slots.
bindings.SetPushConstant("Frame", frameBytes, sizeof(frameBytes));
```

The graph emits `vkCmdPushConstants(cmd, layout, stages, offset, size, data)` per node after
`vkCmdBindPipeline`: execution values first, node overrides second. Reflection validates that each slot lies
inside the shader's reflected push-constant range for every stage the node uses; an unknown slot is a
`Build()` error, like a descriptor binding the shader does not declare.

Two choices are deliberate:

- **One graph-wide block, not per-node blocks.** Every pipeline layout can then share the same
  `VkPushConstantRange`, which is a prerequisite for sharing pipeline layouts across nodes and for the
  pipeline cache. Per-node blocks would force a layout per node.
- **Per-node values as well as per-execution values.** Push constants live in the command buffer, so a
  per-execution value and a per-node value compose without a rebuild. The caller's bytes only need to live
  for the `Execute()` call.

The block size is checked against `VkPhysicalDeviceLimits::maxPushConstantsSize` when the block is declared.

## 3. Dynamic state

Viewport and scissor are already dynamic (`PSODesc.h:367`) and set per node from the attachment extent
(`RenderGraphBuilder.cpp:1715`). The same pattern extends to the Vulkan 1.3 core dynamic states: cull mode,
front face, primitive topology, depth test/write/compare, depth bias, stencil ops/masks/reference, blend
constants and rasterizer discard.

The win is **not** per-frame: dynamic state is command-buffer state. The win is that those fields stop being
baked into the `VkPipeline`, so nodes with different cull/depth settings share one pipeline and the
[#17](https://github.com/v3c70r/Muyo/issues/17) cache hits instead of missing. Per-node toggles become a
`vkCmdSet*` instead of a pipeline variant.

- Graph level: `EnableDynamicStates(mask)`. The set must be identical for every pipeline in a build, or the
  pipelines are not interchangeable.
- `PSODesc` values become the **defaults** emitted after `vkCmdBindPipeline`; a field that is dynamic is no
  longer what the pipeline was created with.
- `node.dynamicStateOverrides` for per-node values; per-draw changes remain the callback's business.

This is a semantic change to a public struct, so it belongs with the cache work (where its effect is
measurable: pipelines created per build, before and after) rather than with the execution bindings.

## Why now, and why not now

The first slice is deliberately small: the present-target slot needs **no descriptor work**, because
attachments are resolved at record time. It removes the per-frame `Build()` from
`FrameSyncTest.cpp` and is the demonstrated case
[#83](https://github.com/v3c70r/Muyo/issues/83) was waiting for.

It is still deferred until after the `helloVulkan` migration ([#20](https://github.com/v3c70r/Muyo/issues/20)),
because the migration is the thing that proves the graph is usable at all, and it must not wait for a
binding API. The cache work ([#17](https://github.com/v3c70r/Muyo/issues/17)) is independent and goes first:
it makes rebuilds cheap without changing what a build means.

## What is explicitly out of scope

- **One graph per swapchain image.** It ties graph count to image count, multiplies transient memory,
  descriptor sets and pipelines by that count, collides on `RenderResourceManager` handles unless every
  graph namespaces its resources, and conflates image count with frames in flight. It is an optimisation to
  a design that should not need it, not the model.
- **Writing the swapchain image through a storage descriptor selected by a push constant.** Swapchain images
  are created with `COLOR_ATTACHMENT` (+ transfer, if supported) and no `STORAGE` (`src/Swapchain.cpp:78`);
  present requires the `PRESENT_SRC_KHR` transition and the presentation engine's ownership. Push-constant
  selection is valid for graph-owned images (history, per-view targets), not for the image being presented.
- **Building every frame.** A rebuild is a declaration change, not a frame step.
- **Dynamic descriptor layouts or attachment formats.** Both are pipeline state; a format change rebuilds.
- **Mid-execution rebinding.** See rule 2.

## Slices

Each is independently testable, and none depends on the later ones:

1. **Image slot + `Execute(bindings, info)`.** `FrameSyncTest` builds once and executes six frames with the
   acquired image bound per execution; assert one `Build()` and no per-frame pipeline or set creation.
2. **Buffer slot + dynamic offset.** A per-frame UBO ring: two executions bind different offsets, each writes
   its frame index, read back and compare.
3. **Push-constant block + slots.** A compute node writes the `Frame` slot's value to a buffer; two executions
   with different values produce different results.
4. **Per-execution transients + descriptor retention → `inFlightCount = 2`**
   ([#40](https://github.com/v3c70r/Muyo/issues/40)). The sampled-image slot's per-execution set is exercised
   here.
5. **Extended dynamic states with the cache work** ([#17](https://github.com/v3c70r/Muyo/issues/17)), measured
   as pipelines created per build.

## Open questions

- **Sampled-image slots and the pool**: per-execution set instances are the first answer; push descriptors
  are capped at 32 here (#83) and descriptor buffers are the endgame. Revisit after the pool retention work
  lands.
- **Per-node dynamic-state overrides at execution time**: needed for split-screen; not in slice 1.
- **Bindless arrays for per-view data**: the "bind all slices once, index with a push constant" variant. It is
  a descriptor-write optimisation, not a binding mechanism; measure before adopting.
- **Thread safety**: all of this assumes one builder and one execution recording at a time, as today.
