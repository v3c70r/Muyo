# Descriptor heap and direct bindless — design note

**Status:** design draft, not implemented. Supersedes
[`DescriptorSet-Lifecycle-Design.md`](DescriptorSet-Lifecycle-Design.md), which described the
content-addressed descriptor-set cache. That cache was the right answer to the question it was asked;
`VK_EXT_descriptor_heap` removes the question.

This note is deliberately **not** in the Doxygen `INPUT` set. It describes a design the repository has
not committed to yet, and `AGENTS.md` section 3 keeps unimplemented designs out of the API reference.

## 1. Goal

Move the RenderGraph to a bindless model built on `VK_EXT_descriptor_heap`, with:

- **no `VkDescriptorSetLayout`**, **no `VkDescriptorSet`**, **no descriptor pool** and **no
  `VkPipelineLayout`** on the graph path;
- **one resource heap and one sampler heap** for the process, bound once per command buffer;
- **one graph-wide root struct in GPU memory**, pointed to by an 8-byte push-data value, instead of a
  layout object or a fixed slot table per pipeline;
- **direct heap access in shaders** (`Texture2D.Handle`, `StructuredBuffer<T>.Handle`,
  `ResourceDescriptorHeap[...]`), not the set/binding mapping path;
- **`Build()` safe to call every frame**, because the expensive things a build used to do are caches or
  do not exist any more.

**Out of scope: GPU-driven execution.** DGC, meshlets, cluster LOD, GPU culling, state bucketing and work
graphs are **host renderer** features. The graph does not own a draw list or a command-generation
schedule; it provides the binding primitives those features consume (the heap, the root pointer, and
the existing indirect/draw-command usages). A host can record one indirect/DGC call in a node callback
without the graph knowing what it expands to.

The result is the model in Sebastian Aaltonen's [No Graphics API](https://www.sebastianaaltonen.com/blog/no-graphics-api/):
a root structure of handles and constants per draw, a global heap of descriptors, and no retained
binding objects between them.

**Not changed by this note:**

- the executor's timeline model, queue planning and completion semantics;
- GPU-driven execution, DGC, meshlets and work graphs, which remain host renderer concerns;
- the barrier stage/access derivation, except that heap-visible images use one layout;
- transient aliasing policy, except that B2 gives each execution its own arena;
- the legacy `RenderPassManager` and its descriptor sets, which remain until P3;
- the graph's error-reporting policy (it still throws at `Build()` and aborts on Vulkan failure).

## 2. What the extension actually removes

The useful correction to the original framing: there is no "single descriptor layout and pipeline
layout" to create. A pipeline created with `VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT` **must** have
`layout = VK_NULL_HANDLE`, and the extension does not add descriptor-set layouts at all. The proposal is
explicit:

> When a pipeline is created with this flag, the pipeline layout must be `NULL`, and resources used by
> its shaders will be sourced from a descriptor heap.

| Today (`src/RenderGraph/`) | With the heap |
| --- | --- |
| `BuildReflectionDescriptorSets` creates a layout per reflected set, per node | gone |
| `DescriptorSetCache` allocates and writes sets at `Build()` | gone |
| `CompileRenderGraphNode` creates a `VkPipelineLayout` per node (`RenderGraphBuilder.cpp:350`) | gone; `layout = VK_NULL_HANDLE` |
| `vkCmdBindDescriptorSets` per node (`RenderGraphBuilder.cpp:2201`) | gone; heaps bind once |
| `vkCmdPushConstants` / reflected push ranges | `vkCmdPushDataEXT` with a graph root pointer |
| pipeline key includes layouts and descriptor types | key is `PSODesc` + shaders + specialization constants |

The only "layout" left is the graph's **root struct**, a plain block of typed heap indices in GPU memory.
It is data, not a Vulkan object, and it is the thing every pipeline can share.

## 3. Decisions

| # | Decision | Reopened by |
| --- | --- | --- |
| D1 | `VK_EXT_descriptor_heap` is a **hard requirement**. The graph does not keep a descriptor-set fallback. | A supported consumer device without the extension. |
| D2 | The language/API floor becomes **Vulkan 1.4**, plus `VK_KHR_shader_untyped_pointers`. | A device or CI runner that cannot meet 1.4. |
| D3 | Shaders use **direct heap access**. There is no set/binding mapping path. | A shader toolchain that cannot emit `SPV_EXT_descriptor_heap`. |
| D4 | **Root data is a pointer** in push data to a graph-wide struct in GPU memory; the root is not a push-data-sized slot table. | A device without `PhysicalStorageBuffer` support, or a root buffer that cannot be kept coherent. |
| D5 | **One resource heap and one sampler heap** — the extension has no per-type bound heaps. The resource heap is partitioned into typed regions (image, buffer, AS); the sampler heap is separate. Static descriptors are immutable; per-execution descriptors live in per-slot rings. | A resource whose descriptor must change while an earlier execution is in flight. |
| D6 | `Build()` is **per frame**; pipelines, resources and descriptors are caches behind it. | A measured build cost that cannot be brought down. |
| D7 | **Per-execution state lives in the executor slot**: command buffers, root data, transient arenas. The compiled plan is shared. | A design that needs cross-execution state. |
| D8 | Heap-visible images live in `VK_IMAGE_LAYOUT_GENERAL`, which removes their layout transitions **now**. `VK_KHR_unified_image_layouts` is deferred until every target driver exposes it. | A measured cost of `GENERAL`, or a driver that needs the extension for a valid layout. |
| D9 | The legacy `RenderPassManager` keeps its descriptor-set shaders until P3; the graph gets **separate `.heap.slang`** sources rather than `#if` variants. | P3 arrives before the graph variants are done. |
| D10 | Heap allocation is a static free-list plus **one dynamic ring per in-flight execution**; preallocate 8-16 MB and double at a frame boundary. | A measured need for a larger budget, or a low-memory target. |
| D11 | RT pipelines use the heap flag with a **null layout**; the SBT remains, and per-hit shader-record resources are a later slice. | A driver rejecting the null RT layout. |
| D12 | The root struct holds **separate image, buffer, AS and sampler arrays**; combined `Sampler2D` is a shader-side composition of two indices, not a combined descriptor. | A pass that needs more top-level entries than the arrays hold. |
| D13 | **GPU-driven execution is host-owned.** The graph guarantees the binding primitives (heap, root pointer, indirect/draw usage) are usable from a host-recorded node; it does not add a DGC, meshlet or work-graph API. | A host feature the binding primitives cannot express. |

## 4. Root data

The root is a **pointer**, not a table. Push data carries `GraphRoot* root`; the struct lives in GPU
memory and can be as large as the node needs. That is the difference between "the heap has a slot
budget" and "the heap has a table budget": the heap can hold millions of descriptors, and the root only
names the top-level tables a draw needs directly.

```cpp
// shaders/shared/RenderGraph/GraphRoot.h, also included by the graph
namespace Muyo::RenderGraph
{
inline constexpr uint32_t kMaxRootImages   = 128;
inline constexpr uint32_t kMaxRootSamplers = 32;
inline constexpr uint32_t kMaxRootBuffers  = 32;
inline constexpr uint32_t kMaxRootAccel    = 8;

struct GraphRoot
{
    uint32_t images[kMaxRootImages];      ///< Resource-heap index, image descriptor stride.
    uint32_t samplers[kMaxRootSamplers];  ///< Sampler-heap index.
    uint32_t buffers[kMaxRootBuffers];    ///< Resource-heap index, buffer descriptor stride.
    uint32_t accel[kMaxRootAccel];        ///< Resource-heap index, acceleration-structure stride.
};
static_assert(sizeof(GraphRoot) == 800, "the shared root header");
}
```

The snippet spells `uint32_t` for readability; the shared header uses an `unsigned int` typedef so the same
file parses as both C++ and Slang. The `static_assert` is C++-side; the Slang side uses the same field
order, and a C++ test asserts the 800-byte layout.

The sizes are chosen from the passes, not from a round number: the current lighting pass uses 11 images,
11 samplers and 3 buffers, and a whole-scene pass adds a few more tables. The arrays are **top-level
tables**, not the resource limit. A 10,000-texture scene stores heap indices in its material data; a
10,000-object scene stores transforms in an instance buffer. The heap holds the descriptors; the root
arrays only name the tables one draw reads directly.

The shader declares a pointer in its push block:

```slang
struct PushBlock
{
    GraphRoot* root;
    uint       drawIndex;   // small node-owned constants can stay inline
};

[[vk::push_constant]]
ConstantBuffer<PushBlock> push;
```

and dereferences it directly:

```slang
Texture2D    albedo  = Texture2D.Handle(uint2(push.root->images[kAlbedoSlot], 0));
SamplerState linear  = SamplerState.Handle(uint2(push.root->samplers[kLinearSlot], 0));
Sampler2D    mixed   = Sampler2D.Handle(
    uint2(push.root->images[kAlbedoSlot], push.root->samplers[kLinearSlot]));
StructuredBuffer<Material> materials =
    StructuredBuffer<Material>.Handle(uint2(push.root->buffers[kMaterialSlot], 0));
```

**Images and samplers are separate.** The image index names a descriptor in the resource heap, the
sampler index names one in the sampler heap, and `Sampler2D` is only a shader-side composition of the two
indices. There is no combined descriptor and no separate eight-slot sampler table: a standalone sampler
is one entry in `samplers`, and a texture used with two samplers is two `Sampler2D` compositions of the
same image index. The current lighting pass therefore fits as 11 image entries + 11 sampler entries + 3
buffer entries, not 25 mixed slots.

**Why a pointer works.** Slang emits `PhysicalStorageBuffer` for the root and `spirv-val` accepts it at
Vulkan 1.4 (evidence in §16). The device already enables buffer device address and `shaderInt64`; the
root buffer is a normal `SHADER_DEVICE_ADDRESS` buffer, not a descriptor and not a heap slot. Push data
holds 8 bytes for the pointer; the rest of the 256-byte budget is available for small inline values.

**One ABI, not two.** The pointer is the only root path. If the indirection measures badly for tiny
roots, an inline fast path is a later optimization gated on that measurement, not a second ABI now.

**The root buffer.** The graph allocates one root buffer per node per execution from a **host-visible
ring**, writes the typed arrays, and records the pointer with `vkCmdPushDataEXT`. The node writes its own
constants after `sizeof(GraphRoot)`, or passes a uniform-buffer handle in `buffers` when the constants are
large. A pass that extends the root declares `struct PassRoot { GraphRoot graph; ... };`, sets
`rootDataSize = sizeof(PassRoot)`, and uses `PassRoot*` in its push block; the graph writes the `GraphRoot`
prefix and the node writes the rest. A node that needs a different root per draw can allocate another
root slice and push a different pointer; the common case is one root per node plus `drawIndex` selecting
material/instance data.

The root buffer is not free:

- it is host-visible, so it needs a `HOST_WRITE` to shader-read dependency (or a staging copy if it is
  device-local), once per execution rather than per draw;
- the ring is per in-flight execution, reset only after that execution's timeline value completes;
- the node declares `rootDataSize` (at least `sizeof(GraphRoot)`, at most a fixed budget such as 4 KB),
  and `Build()` rejects a declaration outside it.

`RenderGraphNodeContext` exposes the mapped root: `Root<T>()` returns a `T*` into the root buffer,
`SetRootImage/Buffer/Sampler(slot, resource)` fill typed entries by resolving the heap index, and
`PushRoot()` records `vkCmdPushDataEXT` with the device address. The graph fills the typed arrays from
the node's `ResourceUse`s before the callback; the callback writes its constants or per-draw overrides.

**Slot assignment.** `ResourceUse::rootSlot` is interpreted by the resource's usage: an image use writes
`images[]`, a buffer use writes `buffers[]`, an acceleration structure writes `accel[]`, and a sampler
writes `samplers[]`. A **combined** image+sampler use also sets `rootSamplerSlot`: the graph writes the
image index to `images[rootSlot]` and the texture's sampler index to `samplers[rootSamplerSlot]`, and the
shader composes the pair with `Sampler2D.Handle(uint2(...))`. Separate texture and sampler uses are the
same mechanism with only one of the two slots set.

The shared header declares the per-pass slot constants and their kinds; the graph rejects an
out-of-range slot, a duplicate slot in one node, and a resource kind that does not fit the declared
array. Only shader-visible resources take a root entry; attachments, vertex/index/indirect buffers and
transfer uses keep their record-time binding and must not declare one.

The graph still **cannot** prove that the shader reads the same slot the node populated. The shared
per-pass header and a readback test per pass are the mitigation; a wrong slot is a wrong result, not a
validation error. That is the one diagnostic the heap model loses, and it is unchanged by moving from a
push table to a pointer.

**Whole-scene binding does not grow the root.** A scene's instances, materials, lights and visible-list
live in buffers whose handles occupy one buffer entry each; textures are heap indices stored *in those
buffers*. A 10,000-object scene uses the same root struct as a 10-object scene; per-object values come
from `drawIndex` or an indirect draw id, not from more root entries.

## 5. Descriptor heap service

The heap is a **host-owned service injected into the graph**, not graph-owned state:

```cpp
class DescriptorHeapService
{
public:
    // Returns the slot for a resource, writing the descriptor on first use.
    // The descriptor type comes from the use: sampled vs storage image, uniform vs storage buffer.
    virtual DescriptorSlot AcquireImage(const ImageResource&, VkDescriptorType, VkImageLayout) = 0;
    virtual DescriptorSlot AcquireBuffer(const BufferResource&, VkDescriptorType, VkDeviceSize offset,
                                         VkDeviceSize size) = 0;
    virtual DescriptorSlot AcquireAccelerationStructure(const AccelerationStructure&) = 0;
    virtual DescriptorSlot AcquireSampler(const VkSamplerCreateInfo&) = 0;

    // Slot -> index in the slot's typed heap region (byte offset / that type's stride).
    virtual uint32_t IndexOf(DescriptorSlot) const = 0;

    // Retire when the execution that last used the descriptor has completed.
    virtual void Retire(DescriptorSlot, uint64_t timelineValue) = 0;

    // One bind per command buffer.
    virtual void Bind(VkCommandBuffer) = 0;
};
```

`RenderGraphServices::descriptorManager` is replaced by this service. The engine still owns
`DescriptorManager` for the legacy renderer; the graph no longer sees it.

### Two physical heaps, typed regions

The extension defines exactly **two** heaps:

- the **sampler heap**, bound with `vkCmdBindSamplerHeapEXT`;
- the **resource heap**, bound with `vkCmdBindResourceHeapEXT`, holding images, buffers, acceleration
  structures, texel buffers and tensors.

There is no separate buffer heap, image heap or command heap. You cannot have a different bound heap per
type without rebinding, and the proposal warns that changing the heap is expensive. What you *can* have
is **typed regions and typed index spaces inside one resource heap**:

| Region | Descriptor stride | Index used by the root array |
| --- | --- | --- |
| Images | `imageDescriptorSize` (64 B on RADV and llvmpipe) | `byteOffset / imageStride` |
| Buffers | `bufferDescriptorSize` (16 B) | `byteOffset / bufferStride` |
| Acceleration structures | 8 B (Slang's AS heap entry is a 64-bit address) | `byteOffset / 8` |
| Texel buffers, if used | buffer stride | buffer-style index |
| Samplers (separate heap) | `samplerDescriptorSize` (16 B RADV, 32 B llvmpipe) | `byteOffset / samplerStride` |

Each region has its own free-list and its own ring. The shader's handle type selects the stride:
`Texture2D` uses the image stride, `StructuredBuffer<T>` the buffer stride,
`RaytracingAccelerationStructure` the 8-byte AS stride, `SamplerState` the sampler stride. That is why
the root arrays are typed and why `-spirv-unified-descriptor-heap-stride` is **not** needed: the arrays
carry type-specific indices, and a buffer descriptor does not waste 48 bytes padding itself to image
size. `-spirv-resource-heap-stride` stays at its default (type size); B0 records the device's AS
descriptor size and uses an explicit stride only if it is not 8 bytes.

**There is a heap ceiling, but it is not the root table.** RADV reports `maxResourceHeapSize =
UINT64_MAX` and llvmpipe about 8 GB; the practical bound is device memory, not a descriptor count. The
128/32/32/8 arrays in §4 are per-draw **top-level tables**, not the heap's capacity. A scene with a
million descriptors lives in the heap; the root names the tables one draw reads.

- **One resource heap**, `VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`,
  device-local, with the implementation's reserved range at the end (`minResourceHeapReservedRange`).
  Buffers placed in it need `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` too; the engine already creates
  its VMA allocator with the device-address flag, so this is a usage bit to add, not a new allocator.
- **One sampler heap**, same rules, `VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT`.
- The heap range address is aligned to `max(samplerHeapAlignment, resourceHeapAlignment)`. The NVIDIA
  sample records that the driver's buffer memory requirement can be **smaller** than the heap alignment,
  so the allocation has to request the latter explicitly.
- Descriptors are written with `vkWriteResourceDescriptorsEXT` / `vkWriteSamplerDescriptorsEXT` into a
  host staging buffer, then copied into the device-local heap once. Static descriptors are immutable
  afterwards; dynamic-region descriptors are copied per execution.
- The heap is bound once per command buffer with `vkCmdBindResourceHeapEXT` /
  `vkCmdBindSamplerHeapEXT`; the proposal warns that rebinding is expensive, so it is not per node.
- The heap is read on every queue the execution plan uses. Create it **concurrent** across the graph's
  graphics and compute families, or transfer ownership with the plan; an exclusive heap read on a
  second family is an ownership error, and the single-queue CI device cannot reveal it.
- Growth reallocates the heap buffer and re-uploads every live descriptor. Indices are byte offsets
  divided by the type stride and do not change, so root arrays remain valid; the next command buffer
  binds the new range.

The strides are part of the **shader ABI**. Every heap shader must be compiled with the same
`-spirv-resource-heap-stride` and `-spirv-sampler-heap-stride` values that the service uses; a shader
compiled with a different stride computes a different address from the same index, and the symptom is a
wrong result rather than a validation error. The values come from the device properties at startup and
are passed to both the shader build and the heap service.

### Allocation policy

The heap is split into two allocation classes, and the split is what makes a ring safe:

- **Static regions** — one typed free-list per region for imported and persistent resources. A descriptor
  is written when the resource is first seen and never rewritten; the slot is returned when the resource
  is destroyed and the last execution that used it has completed.
- **Dynamic regions** — one bump-allocated ring per region, per in-flight execution. A per-execution
  resource (a rotating uniform-buffer slice, a frame-specific transient) writes its descriptors into that
  execution's ring, and the ring is reset only when the execution's timeline value has completed. With
  `inFlightCount = N` there are N rings per region; a single global ring would let execution N+1
  overwrite descriptors that execution 1 is still reading.

A single ring for the whole heap is therefore the wrong shape: it would recycle immutable descriptors
and break the root arrays that point at them. The ring belongs to the dynamic region, sized
`maxDescriptorsPerExecution * inFlightCount` per region.

**Preallocate, then grow rarely.** Start with a configured budget (8-16 MB is ~128k-256k 64-byte image
descriptors, far more than a scene needs; buffer and AS regions cost less per entry). If it is exhausted,
double at a frame boundary: allocate a new heap, copy every live descriptor, bind the new range from the
next command buffer, and retire the old heap once the last execution using it has completed. Indices are
byte offsets divided by the type stride, so they survive the copy and root arrays stay valid. If growth is
impossible, `Build()` throws naming the resource rather than silently dropping a descriptor.

### Root buffers

Root data is not a descriptor and not part of the heap. The graph allocates one root buffer per node per
execution from a **host-visible ring**, writes the typed arrays, and pushes the pointer. The ring is per
in-flight execution, reset after completion. The root buffer is a normal buffer with
`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` and a device-address
allocation; there is no descriptor write and no heap slot. A node that needs per-draw roots allocates a slice per
draw; the common case is one root per node plus `drawIndex`.

### Why immutable descriptors matter

A descriptor that changes while an earlier execution still reads it is the descriptor analogue of the
per-node set problem. The static region removes the class by construction:

- a static resource gets one descriptor per distinct use (type, range, layout) when that use is first
  seen, and the descriptor names the resource's current storage;
- a resize or a changed declaration creates a new resource and a new descriptor, and retires the old one
  behind the timeline value of the last execution that used it;
- stable per-frame variation is a **different index in the root array**, not a rewritten descriptor.

The dynamic ring is the deliberate exception: its slots *are* reused, but only after the execution that
owns the ring has completed, so no in-flight command buffer can observe a rewritten descriptor. That is
why the ring is per execution rather than global.

This is also what makes the in-flight count tractable: there is no per-build descriptor state to
multiply by `framesInFlight`.

### Images

`vkWriteResourceDescriptorsEXT` builds an image descriptor from a `VkImageViewCreateInfo`, not from a
`VkImageView` handle. That is a simplification worth taking: a sampled image does not need a persistent
view at all, and `ImageResource` already builds the create info internally. The service needs it
exposed (a `GetViewInfo()`), while attachments keep their views because `vkCmdBeginRendering` takes a
`VkImageView`. An aliased transient's descriptor is written after its view info is finalized, so the
bind-then-descriptor order is the same as the current bind-then-view order.

### Samplers

`vkWriteSamplerDescriptorsEXT` takes `VkSamplerCreateInfo`, not a `VkSampler`. The engine currently
creates samplers from local create-info structs and does not retain them
(`Texture.h:167`, `SamplerManager.h:36`). The heap service therefore needs a **sampler cache keyed on
create info**, or the resources must retain the create info. That is a prerequisite, not a detail.

## 6. Build and Execute

`Build()` becomes a **frame build**:

1. Topological sort and declaration validation (unchanged).
2. Resolve every `ResourceUse` to a resource. **Resources are cached by `ResourceDesc`, not by handle
   name**, which fixes #209: a changed extent or format recreates; an unchanged declaration reuses.
3. Derive the typed root slot assignment (image/sampler/buffer/AS bounds, duplicates, and the node's
   `rootDataSize`).
4. Derive the execution plan (queue segments, transfers).
5. Compile a pipeline **on a cache miss only**. The key is `PSODesc` + shader hashes + specialization
   constants. There is no descriptor layout or pipeline layout in the key, because there are none.

`Execute()`:

1. Acquire an executor slot (command buffers, root-buffer ring, transient arena for that slot).
2. For each node, resolve its resource slots to typed heap indices, write the root buffer, push its
   device address with `vkCmdPushDataEXT`, call the node callback. A host-write-to-shader barrier is
   recorded before the node reads the root; the ring is reset only after the slot's timeline completes.
3. Record barriers using the existing sync2 path, submit, signal the timeline.

The **compiled plan is shared**; the **execution state is per slot**. That is what allows
`inFlightCount > 1`: two executions cannot alias root data or a transient arena, and descriptors are
immutable. The remaining work for #40 is the per-execution transient arena, not descriptor sets.

`Build()` every frame is only honest if the caches make it cheap. Pipeline creation (#17), resource
allocation and descriptor writes must be misses, not per-frame work. #59 gains a CPU-side build-time
metric so the claim is measured rather than asserted.

## 7. Pipeline creation

- Graphics: `VkGraphicsPipelineCreateInfo.layout = VK_NULL_HANDLE`, with
  `VkPipelineCreateFlags2CreateInfo{ .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT }` in the
  `pNext` chain alongside `VkPipelineRenderingCreateInfo`.
- Compute: the same, on `VkComputePipelineCreateInfo`.
- Ray tracing: the same flag on `VkRayTracingPipelineCreateInfoKHR`; the spec requires a null layout
  (`VUID-VkRayTracingPipelineCreateInfoKHR-flags-11311`), and B0 verifies that this driver accepts it.
  If a driver rejects it, RT waits on the driver rather than getting a second binding path: the mapping
  path is not a fallback because it also needs the heap flag and a null layout.
- Shader modules are compiled with `-capability spvDescriptorHeapEXT -capability spirv_1_4`. Slang emits
  `SPV_EXT_descriptor_heap` + `SPV_KHR_untyped_pointers`; the device must enable
  `VkPhysicalDeviceShaderUntypedPointersFeaturesKHR::shaderUntypedPointers` and
  `VkPhysicalDeviceDescriptorHeapFeaturesEXT::descriptorHeap`, and verify both at device creation.
- `CreatePipelineFromPSODesc` loses its `VkPipelineLayout` parameter. `CompiledRenderGraphNode` loses
  `pipelineLayout`, `descriptorSetLayouts`, `descriptorSets`, and the ownership flags.

## 8. Images and layouts

A heap image descriptor bakes a `VkImageLayout`. Rewriting it when a layout changes would destroy the
immutability property, so the first implementation gives every heap-visible image one canonical layout:
**`VK_IMAGE_LAYOUT_GENERAL`**. `GENERAL` is valid for sampled, storage, color and depth attachment use;
present images keep `PRESENT_SRC_KHR` and are attachments, not heap descriptors.

For an image that can be heap-sampled, `BeginRendering` uses `GENERAL` for its attachment as well;
otherwise the descriptor layout and the attachment layout would disagree. An imported image already in
another layout is transitioned to `GENERAL` once when it is registered, or its descriptor is written with
that layout and the image is never transitioned again. Present-only images are the exception: they use
`PRESENT_SRC_KHR`, are not heap descriptors, and keep the existing present transition.

That removes image-layout transitions from the sampled/storage/attachment path and lets a descriptor be
written once. `VK_KHR_unified_image_layouts` would make the same statement at the API level and delete
the layout field from the barrier path, but it is **not available on RADV 1.4.354** on the RDNA2 box (it
is present on llvmpipe). B3 is therefore optional and driver-gated, not a B1 dependency. Until then, the
barrier code keeps only the present transition and the one-time conversion of imported images that are
not created in `GENERAL`.

The tradeoff is explicit: `GENERAL` can forgo a vendor's optimal-layout fast path. If a measured cost
appears, the response is a per-resource layout policy with a descriptor rewrite behind the timeline,
not a silent return to per-node descriptors.

## 9. Shader migration and the legacy renderer

Direct heap access is a shader-source change, not a compiler flag:

```slang
// before: descriptor-set declarations
[[vk::binding(0, SET_PER_VIEW)]] ConstantBuffer<PerViewData> perView;
[[vk::binding(0, SET_MATERIAL)]] StructuredBuffer<PBRMaterial> materials;

// after: handles in the root data
ConstantBuffer<PerViewData> perView = ConstantBuffer<PerViewData>.Handle(
    uint2(push.root->buffers[kPerViewSlot], 0));
StructuredBuffer<PBRMaterial> materials = StructuredBuffer<PBRMaterial>.Handle(
    uint2(push.root->buffers[kMaterialSlot], 0));
```

The graph shaders are converted in B1b. The legacy renderer keeps its GLSL/set-binding shaders until
P3, so a pass shared by both paths gets two **separate sources**: `foo.slang` for the binding variant and
`foo.heap.slang` for the direct-access variant. The CMake shader build gains a second rule:
`.heap.slang` emits `.heap.spv` with `-capability spvDescriptorHeapEXT`, `-capability spirv_1_4`,
`-spirv-resource-heap-stride` / `-spirv-sampler-heap-stride` only if the device's defaults differ; the
existing rule is unchanged. No unified stride is used.
The graph loads the `.heap.spv` name; the legacy path loads the old `.spv`. That is deliberate debt with
an end date (the P3 deletion), not an argument for keeping a descriptor-set fallback in the graph. When
P3 deletes the legacy renderer, the old sources go and the `.heap` suffix can be dropped mechanically.

The graph also uses GLSL shaders (`GBuffer.frag`, `GBuffer.vert`, `test16BitStorage.comp`), so the
conversion is not Slang-only. glslang 16.6 advertises `GL_EXT_descriptor_heap` and
`GL_EXT_structured_descriptor_heap`; B0 checks the exact syntax and that the installed validator emits
the same capabilities, and B1b converts those shaders or replaces them with Slang equivalents.

`SPIRV-Reflect` in `thirdparty/SPIRV-Reflect` already understands `SPV_EXT_descriptor_heap` and
untyped pointers (`spirv_reflect.c:522`, `:3848`), so reflection still supplies push-constant ranges,
specialization constants and heap-access metadata. It will no longer supply descriptor bindings,
because there are none.

Push constants are part of the same change: `vkCmdPushConstants` is invalid without a pipeline layout,
so the app's call at `src/app/helloVulkanRG.cpp:273` becomes `RenderGraphNodeContext::PushData`, which
records `vkCmdPushDataEXT` at the reflected offset. A shader that keeps its `[[vk::push_constant]]`
block is otherwise unchanged except that it now starts with `GraphRoot* root` instead of a value block.

## 10. Synchronization

- Descriptors uploaded by a transfer need a dependency whose destination access is
  `VK_ACCESS_2_RESOURCE_HEAP_READ_BIT_EXT` or `VK_ACCESS_2_SAMPLER_HEAP_READ_BIT_EXT`, executed before
  the first shader that reads the heap. The extension defines both access bits.
- A host write straight into a host-visible heap needs the same destination access with a host-write
  source; a device-local heap plus a staging copy is the default and is what the sample uses.
- Root buffers are host-written. A `VK_ACCESS_2_HOST_WRITE_BIT` to shader-read dependency is recorded
  before the first node that reads the root; the root ring is per execution, so this is one dependency
  per execution rather than one per draw.
- `vkCmdPushDataEXT` is command-buffer state; no queue-level synchronization is needed beyond the
  existing node barriers.
- Rebinding a heap is avoided; the spec notes implementations may synchronize internally, and the
  recommendation is to keep the same heap for the application's lifetime.

## 11. Failure modes the graph must name

| Failure | When | Message / action |
| --- | --- | --- |
| Extension or feature absent | device creation | throw naming `VK_EXT_descriptor_heap`, `descriptorHeap`, `shaderUntypedPointers`, or `PhysicalStorageBuffer` support |
| Shader compiled without the heap capability | `Build()` | reject the node; the shader has no `ResourceHeapEXT` built-in |
| Push block smaller than the root pointer | `Build()` | name the shader and its reflected push-constant size; the block must hold at least 8 bytes |
| Push block (pointer + inline constants) over `maxPushDataSize` | `Build()` | name the node, the size and the limit; move constants into the root struct or a UBO |
| `rootDataSize` below `sizeof(GraphRoot)` or above the budget | `Build()` | name the node, the size and the min/max |
| Typed root index out of range or duplicated | `Build()` | name the node, the array (`images`/`buffers`/`accel`/`samplers`), the slot and the use |
| Resource kind does not fit the typed array | `Build()` | name the use and the array; a buffer use cannot write `images` |
| Shader reads a different slot than the node populated | runtime | **not detectable by the graph**; per-pass header + readback test, see §4 |
| Root ring exhausted | Build / `Execute()` | grow the ring, or throw naming the node; do not silently reuse an in-flight root |
| Descriptor heap full past `maxResourceHeapSize` | `Build()` / allocation | name the resource and the size; do not silently drop a descriptor |
| Sampler has no create info | `Build()` | ask the resource for it; a sampler handle alone cannot be written |

## 12. Migration slices

| Slice | Scope | Gate |
| --- | --- | --- |
| **B0 — probe** | Device features and heap properties; one compute and one graphics draw through a null-layout pipeline, a root pointer and direct heap access; one RT pipeline with the heap flag; readback. | Runs on RADV REMBRANDT, the RTX 3090 **and** llvmpipe; the properties (strides, `maxPushDataSize`, max heap sizes, reserved ranges), RT acceptance and `VK_KHR_unified_image_layouts` support are recorded. This is the go/no-go. |
| **B1a — heap service** | `DescriptorHeapService` with typed regions and per-region caches; root-buffer ring; bind once; device layer 1.4 features and function pointers. | A test writes an image, a buffer, an AS and a sampler descriptor, reads them through a root pointer, and creates no descriptor set or pipeline layout. |
| **B1b — graph bind** | Root-pointer / typed-array ABI; `ResourceUse::rootSlot`; separate `.heap.slang` direct-access sources; delete `DescriptorSetCache`, set/pool/layout creation and `VkPipelineLayout` from `src/RenderGraph/`. | Existing graph suite green in Debug and Release; `grep` for `vkCreateDescriptorSetLayout`, `vkAllocateDescriptorSets`, `vkCreatePipelineLayout` in `src/RenderGraph/` is empty. |
| **B2 — frame build** | Per-frame `Build()` with pipeline/resource/descriptor caches; per-slot root and transient arenas; `inFlightCount > 1`. | Resize and swapchain rotation with a build per frame; #209 regression test fails before the fix; #40's overlap test. |
| **B3 — unified layouts** | **Deferred, not scheduled.** Adopt `VK_KHR_unified_image_layouts` when every target driver exposes it (llvmpipe does; RADV 1.4.354 does not). The GENERAL policy already removes the transitions. | No gate now; when adopted, no image-layout transition in a graph execution and validation clean. |
| **B4 — GPU-written root data** | Root buffers are storage-capable so a **host** compute pass can fill them; a bindless draw consumes the result. Optional direct `ResourceDescriptorHeap` sugar. | A host compute pass writes a root and a bindless draw reads it; the graph adds no GPU-driven API. |

B1 is the point of no return for the descriptor-set path. B0 is where the hardware claim is settled.

## 13. Roadmap reconciliation

This section is a proposal for `docs/Roadmap.md`; the roadmap PR applies it after this note is accepted.

| Current item | Disposition |
| --- | --- |
| **#15** content-addressed descriptor sets | **Close as superseded by B1.** The cache, pool and layouts are deleted; the `MATERIAL` cliff cannot exist. |
| **#16** transient lifetime/aliasing | Keep. Aliasing becomes an optimization; B2 adds per-execution arenas. |
| **#17** pipeline/shader cache | **Prerequisite for B2**, and simpler: no layout/descriptor state in the key. |
| **#40** in-flight > 1 | **Re-scoped.** The descriptor-set half is gone; the remaining work is per-execution transients and root/arena slots. |
| **#83** per-execution bindings | **Close after B2.** A different root entry is a different value per execution; no rebuild and no descriptor rewrite. |
| **#209** rebuild keeps stale resource | **Fixed in B2** by the desc-keyed resource cache. |
| **#12** barriers | Keep. The deferred unified-layout slice would remove the layout half; the stage/access half stays. |
| **#14/#19** RT parity | RT shaders use heap handles. B0 verifies a null-layout RT pipeline; if a driver rejects it, RT waits on that driver rather than getting a second binding path — the graph does not keep descriptor sets for it. |
| **#57** client frame helper | Unaffected; runs in parallel. |
| **#20** `helloVulkan` port | **After B2.** Porting to the descriptor-set model and converting again is the retrofit this design exists to avoid. |
| **#51** delete `RenderPassManager` | Unchanged, after P2. |
| **#53/#58/#54** library track | `DescriptorManager` becomes the heap service; device floor becomes 1.4; #54's compute-only test runs on the heap. |
| **#59** cost criterion | Add per-frame build CPU time and heap/descriptor traffic. Host-side draw-call and GPU-driven costs are the renderer's own metrics, not the graph's. |
| **`AGENTS.md` / CI** | Raise the floor to 1.4, enable and verify `descriptorHeap` + `shaderUntypedPointers`, and record heap support in the `[test device]` line. Lavapipe supports both, so CI can gate the path. |
| **`docs/DescriptorSet-Lifecycle-Design.md`** | Mark superseded, and remove it from the Doxygen `INPUT` set and `scripts/render_graph_docs.py` in B1b so the API reference stops describing deleted types. |
| **`Consumers.md` / muyo-llm** | The hard requirement and the 1.4 floor are a breaking change; name the consumer and add the guarantee row before B1b lands. |

**New track `B Bindless (descriptor heap)`** alongside P0–P3 and L, with the B0–B4 gates above. There is no separate GPU-driven track: those are host renderer features, and the graph only has to stay out of their way.

**Critical path:** `#17 → B0 → B1 → B2 → #20 → #51`. #57 runs in parallel; the L chain is unchanged;
#59 spans B2 and #20. P1 cannot close until B2 and #59; P2 cannot start its full port until B2.

**The two open documentation PRs.** #208 records per-execution descriptor sets, push-constant slots and
dynamic offsets; this note replaces all three with root data, so #208 should be **closed as
superseded**, keeping its structure-at-`Build()`/handles-at-`Execute()` split in the new note. #210 is a
roadmap snapshot of the pre-bindless plan: it should land as the baseline with its `#208` reference
removed, and the roadmap changes above should be the follow-up PR. Landing the snapshot first keeps the
reconciliation record and the board edits, and keeps this pivot a separate reviewable concern.

## 14. Host GPU-driven work (out of scope)

GPU-driven execution is a **host renderer** concern, not a graph feature. DGC, meshlets, cluster LOD,
GPU culling, state bucketing, indirect command lists and work graphs are all host-side implementations.
This note does not add a G track, a DGC node, a meshlet API or a work-graph scheduler.

What the bindless design must do is **not block** a host that implements them. The compatibility surface
is deliberately small:

| Host feature | Bindless primitive it uses | Graph change |
| --- | --- | --- |
| GPU culling -> indirect draw | `DRAW_COMMAND_BUFFER` / `INDIRECT_BUFFER` uses, node callback | none; already exercised by `prepareDrawCmdBuffer.comp.slang` and `vkCmdDrawIndexedIndirectCount` |
| Device-address draws | buffer device address + the host records `vkCmdDrawIndexedIndirectCount2KHR` | none; the graph only sees the node's declared buffers |
| DGC | per-sequence root pointer, the heap, a null pipeline layout; the host records `vkCmdExecuteGeneratedCommandsEXT` | none; the graph sees one node plus the draw/preprocess buffers it declares |
| Meshlets / mesh shaders | root pointer, heap, `TASK_PAYLOAD` / `MESH_PAYLOAD` uses | none; the host builds the cluster pipeline and records the mesh-task draw |
| Work graphs | the host records the dispatch-graph command in a node callback | none; the graph does not schedule the graph nodes |

The host owns the DGC layout, execution set, preprocess buffer, culling pipelines, state buckets and
meshlet build. The graph owns ordering, lifetime and barriers for the resources the node declares.

**The one graph-side requirement is dependency declaration.** A GPU-driven node must still declare the
resources it reads and writes, including its indirect buffer, draw count, root buffer and any DGC
preprocess buffer. Bindless textures it samples must be declared on the node as uses; the existing
per-resource barrier model then applies. If a host later wants coarse group barriers for opaque
GPU-driven work, that is a separate host-facing proposal with its own correctness argument, not a
prerequisite of this design.

The current GPU-culling test is the proof that the surface works: the CPU records one compute node and
one graphics node, the GPU writes the draw commands and count, and the graph barriers the hand-off. That
is exactly the shape a host DGC or meshlet pass needs; the graph does not need to know what the generated
commands contain.

## 15. Risks and unknowns

1. **Driver and tooling maturity.** The extension is new. `descriptorHeapCaptureReplay = false` on llvmpipe; RenderDoc and vendor profilers may not understand a capture yet. B0 records
   what each machine reports, and the first bug is attributed to the driver or to us before it is
   worked around.
2. **API floor.** Vulkan 1.4 is a deliberate break from the current 1.3 floor in `AGENTS.md`; it must
   be recorded there and in `Consumers.md`, not only in code.
3. **Shader rewrite cost.** B1b converts every graph shader, and the legacy renderer needs a second
   variant until P3. The count is small (19 Slang shaders, 9 with bindings) but the overlap is real.
4. **Root pointer and root buffer.** `PhysicalStorageBuffer` has no bounds checks: a bad root pointer
   is a GPU fault, not a validation error. The root buffer is host-visible with a per-execution
   host-write-to-shader barrier, or device-local with a staging copy; the ring must be sized for the
   node's draws, and exhaustion is a `Build()`/`Execute()` error. This is a per-draw table cost, not a
   heap-capacity limit.
5. **`GENERAL` layout cost.** Unmeasured. Unified layouts are deferred because RADV 1.4.354 does not
   expose them; a measured regression is the trigger for a per-resource policy, not for blocking B1.
6. **Sampler create info** is not retained today; the heap cannot write a sampler descriptor from a
   `VkSampler` handle alone.
7. **Ray tracing** is spec-compatible with the heap: the flag requires a null layout
   (`VUID-VkRayTracingPipelineCreateInfoKHR-flags-11311`) and `vkCmdTraceRaysKHR` has heap VUIDs. The
   remaining risk is driver/validation acceptance, which B0 settles with an actual RT pipeline. SBT
   shader-record resources are a later optimization, not a B1 requirement.
8. **In-flight transients.** Bindless removes the descriptor half of #40, not the resource half. B2 must
   still give each execution its own transient arena; a green single-flight run does not prove it.
9. **Two machines is the minimum.** The feature is reported present on both, but driver behaviour at
   `inFlightCount > 1` and with heap growth still needs both.
10. **AMDVLK 1.4.313 on this box lacks the extension entirely.** The hard requirement excludes it until
    it is updated; RADV is the AMD validation driver for this work. That is a deliberate consequence of
    D1, not an oversight.

## 16. Evidence collected for this draft

All commands were run on llvmpipe unless stated. The RADV/AMDVLK probe needed the wider sandbox because
the workspace policy blocks opening the render node read-write; it was run on the RDNA2 box.

```bash
# Extension and feature, llvmpipe
vulkaninfo | grep -i descriptor_heap
# VK_EXT_descriptor_heap : extension revision 1
# descriptorHeap = true, descriptorHeapCaptureReplay = false
# VK_KHR_unified_image_layouts present, unifiedImageLayouts = true

# Properties used by the design
# samplerDescriptorSize 0x20, imageDescriptorSize 0x40, bufferDescriptorSize 0x10
# maxPushDataSize 0x100, minResourceHeapReservedRange 0, minSamplerHeapReservedRange 0

# RADV REMBRANDT, driver/api 1.4.354
# descriptorHeap = true, descriptorHeapCaptureReplay = true
# samplerDescriptorSize 16, imageDescriptorSize 64, bufferDescriptorSize 16
# maxPushDataSize 0x100, maxResourceHeapSize = UINT64_MAX, shaderUntypedPointers = true
# VK_KHR_ray_tracing_pipeline present
# VK_KHR_unified_image_layouts ABSENT
# AMDVLK 1.4.313: no VK_EXT_descriptor_heap and no VK_KHR_unified_image_layouts.

# Slot budget: the legacy lighting pass binds 14 distinct tables
grep -c "AddParameter\|AddImageParameter" src/RenderPasses/RenderPassOpaqueLighting.cpp
# 14

# Shader side: Slang 2026.17 emits the extension and untyped pointers
slangc probe.slang -target spirv -profile glsl_450 \
  -capability spvDescriptorHeapEXT -capability spirv_1_4 -entry main -o probe.spv
spirv-dis probe.spv | grep -E 'DescriptorHeapEXT|UntypedPointersKHR|ResourceHeapEXT|SamplerHeapEXT'
# OpCapability UntypedPointersKHR / DescriptorHeapEXT
# OpExtension "SPV_KHR_untyped_pointers" / "SPV_EXT_descriptor_heap"
# OpDecorate ... BuiltIn ResourceHeapEXT / SamplerHeapEXT
spirv-val --target-env vulkan1.4 probe.spv   # exits 0

# The same probe with a StructuredBuffer and RWStructuredBuffer compiles to
# OpBufferPointerEXT and validates; buffers are heap handles, not raw pointers.

# Combined Sampler2D handle: .x indexes the resource heap, .y the sampler heap,
# then OpSampledImage; compiles and validates.
spirv-dis combined.spv | grep -E 'ResourceHeapEXT|SamplerHeapEXT|OpSampledImage'
spirv-val --target-env vulkan1.4 combined.spv   # exits 0

# RT with the heap flag: the VUID requires a null layout, and trace commands
# have heap VUIDs, so RT can drop the pipeline layout at the API level.
# VUID-VkRayTracingPipelineCreateInfoKHR-flags-11311
# VUID-vkCmdTraceRaysKHR-None-11308/11309/11376

# Root pointer: push data carries GraphRoot*, Slang emits PhysicalStorageBuffer, and a
# root struct with separate image/sampler/buffer arrays validates.
slangc rootarrays.slang -target spirv -profile glsl_450 \
  -capability spvDescriptorHeapEXT -capability spirv_1_4 -entry main -o rootarrays.spv
spirv-dis rootarrays.spv | grep -E 'PhysicalStorageBuffer|ResourceHeapEXT|SamplerHeapEXT|OpSampledImage'
spirv-val --target-env vulkan1.4 rootarrays.spv   # exits 0

# GLSL path, not yet compiled here: glslangValidator 16.6.0 advertises
# GL_EXT_descriptor_heap and GL_EXT_structured_descriptor_heap in
# glslang/MachineIndependent/Versions.h. B0 settles the syntax and the emitted capabilities.
```

The RDNA2 and llvmpipe probes are measured here. The RTX 3090 support is **reported** by the maintainer,
not measured in this session; B0 is the measurement. AMDVLK 1.4.313 is not a candidate while it lacks
the extension.

## 17. Open questions

1. **Root arrays and root memory**: 128 images / 32 samplers / 32 buffers / 8 AS cover the current
   lighting pass with room to spare, and material data carries the rest. Does any first-port pass need
   more than 128 *top-level* images? Separately, should the root buffer start host-visible (one barrier
   per execution) or device-local with a staging copy?
2. **Combined vs separate at the shader level**: the ABI is separate arrays, so a shader may compose
   `Sampler2D` from an image and a sampler index, or pass `Texture2D` and `SamplerState` separately.
   Combined halves the number of root entries for material textures; separate is more explicit and lets
   one image use several samplers without duplicate image entries. Slang has open bugs around
   `DescriptorHandle` arrays and `ConstantBuffer.Handle`, so B1b tests the shapes the material and
   shadow passes actually use either way.
3. **Unified image layouts**: RADV 1.4.354 on this RDNA2 box does **not** expose
   `VK_KHR_unified_image_layouts`; llvmpipe does. B1's `GENERAL` policy already removes layout
   transitions, so B3 is optional cleanup when the driver exposes it, not a B1 dependency. The RTX 3090
   driver is recorded in B0.
4. **Heap budget**: static free-list plus per-slot rings, preallocated at 8-16 MB and doubled at a frame
   boundary. Is 16 MB acceptable on the lowest-memory target?
5. **RT shader records**: the null-layout RT pipeline is spec-required with the heap flag
   (`VUID-VkRayTracingPipelineCreateInfoKHR-flags-11311`) and `vkCmdTraceRaysKHR` has heap VUIDs, so RT
   can drop the layout. Per-hit resources from SBT shader records are a later slice; B0 only proves the
   pipeline and a global-descriptor trace.

GPU-driven questions (DGC layouts, state buckets, meshlet formats, work graphs, CPU draw budgets) are
**host renderer** questions, not this note's. The compatibility surface they rely on is §14; the answers
belong in the host renderer's own design and backlog.

## References

- [VK_EXT_descriptor_heap proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_descriptor_heap.html)
- [SPV_EXT_descriptor_heap](https://github.khronos.org/SPIRV-Registry/extensions/EXT/SPV_EXT_descriptor_heap.html)
- [Slang descriptor handles and `spvDescriptorHeapEXT`](https://github.com/shader-slang/slang/blob/master/docs/user-guide/03-convenience-features.md)
- [NVIDIA `vk_mini_samples` descriptor heap sample](https://github.com/nvpro-samples/vk_mini_samples/tree/main/samples/descriptor_heap)
- [No Graphics API](https://www.sebastianaaltonen.com/blog/no-graphics-api/)
- `docs/DescriptorSet-Lifecycle-Design.md` — superseded by this note.
