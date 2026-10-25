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
- **generic push data**: the graph records `vkCmdPushDataEXT` bytes and an optional root-memory helper;
  the host owns the root layout, size and ABI, instead of a graph-imposed root table;
- **direct heap access in shaders** (`Texture2D.Handle`, `StructuredBuffer<T>.Handle`,
  `ResourceDescriptorHeap[...]`) as the default; the mapping path is reserved for the extension's
  embedded-sampler and input-attachment exceptions;
- **`Build()` safe to call every frame**, because the expensive things a build used to do are caches or
  do not exist any more.

**Out of scope: GPU-driven execution.** DGC, meshlets, cluster LOD, GPU culling, state bucketing and work
graphs are **host renderer** features. The graph does not own a draw list or a command-generation
schedule; it provides the binding primitives those features consume (the heap, generic push data and
optional root memory, and the existing indirect/draw-command usages). A host can record one indirect/DGC call in a node callback
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
| `vkCmdPushConstants` / reflected push ranges | `vkCmdPushDataEXT` with host-defined bytes |
| pipeline key includes layouts and descriptor types | key is `PSODesc` + shaders + specialization constants |

The only "layout" left is the **host's root data**. The graph treats it as bytes passed through
`vkCmdPushDataEXT`; it does not inspect or prescribe the structure.

## 3. Decisions

| # | Decision | Reopened by |
| --- | --- | --- |
| D1 | `VK_EXT_descriptor_heap` is a **hard requirement**. The graph does not keep a descriptor-set fallback. | A supported consumer device without the extension. |
| D2 | The language/API floor becomes **Vulkan 1.4**, plus `VK_KHR_shader_untyped_pointers`. | A device or CI runner that cannot meet 1.4. |
| D3 | Shaders use **direct heap access by default**. Embedded samplers, input attachments and YCbCr/subsampled samplers require the mapping path; they are supported there or rejected with named diagnostics, never left undefined. | A shader toolchain that cannot emit `SPV_EXT_descriptor_heap`. |
| D4 | **Push data is opaque to the graph.** The host defines the root layout and ABI; the graph records bytes and optionally provides root-memory allocation/lifetime. No graph-level `GraphRoot`, slot table or root-size policy. | A host root layout that the optional root-memory helper cannot serve. |
| D5 | **One resource heap and one sampler heap** — the extension has no per-type bound heaps. The resource heap is partitioned into typed regions (image/texel, buffer/acceleration structure); the sampler heap is separate. Static descriptors are immutable; per-execution descriptors live in per-slot rings. | A resource whose descriptor must change while an earlier execution is in flight. |
| D6 | `Build()` is **per frame**; pipelines, resources and descriptors are caches behind it. | A measured build cost that cannot be brought down. |
| D7 | **Per-execution state lives in the executor slot**: command buffers, optional root memory, transient arenas. The host may supply its own root memory. The compiled plan is shared. | A design that needs cross-execution state. |
| D8 | Heap-visible images live in `VK_IMAGE_LAYOUT_GENERAL`, which removes their layout transitions **now**. `VK_KHR_unified_image_layouts` is deferred until every target driver exposes it. | A measured cost of `GENERAL`, or a driver that needs the extension for a valid layout. |
| D9 | The legacy `RenderPassManager` keeps its descriptor-set shaders until P3; the graph gets **separate `.heap.slang`** sources rather than `#if` variants. | P3 arrives before the graph variants are done. |
| D10 | Heap allocation is a static free-list plus **one dynamic ring per in-flight execution**; preallocate 8-16 MB and double at a frame boundary. | A measured need for a larger budget, or a low-memory target. |
| D11 | RT pipelines use the heap flag with a **null layout**; the SBT remains, and per-hit shader-record resources are a later slice. | A driver rejecting the null RT layout. |
| D12 | The heap service writes **separate image and sampler descriptors**; combined `Sampler2D` is a shader-side composition of two heap indices, not a combined descriptor. Root layout is the host's concern. | A resource type that requires a combined or embedded descriptor. |
| D13 | **GPU-driven execution is host-owned.** The graph guarantees the binding primitives (heap, push data/root memory, indirect/draw usage) are usable from a host-recorded node; it does not add a DGC, meshlet or work-graph API. | A host feature the binding primitives cannot express. |
| D14 | **Direct-heap exceptions are explicit.** Input attachments and YCbCr/subsampled/embedded samplers go through mappings or are rejected with a named diagnostic; custom-border samplers carry a registered index. | A driver or format that needs another exception. |

### Device requirements and the deliberate floor raise

`AGENTS.md` section 3 currently states a Vulkan 1.3 floor. This design **deliberately raises it to 1.4**
and requires the following, each verified at device creation rather than enabled blindly:

| Requirement | Extension / feature | Verified at |
| --- | --- | --- |
| Vulkan 1.4 | core (includes the `maintenance5` / extended-flags path the extension depends on) | instance/device creation |
| Descriptor heaps | `VK_EXT_descriptor_heap`, `descriptorHeap` | device creation |
| Direct heap access SPIR-V | `VK_KHR_shader_untyped_pointers`, `shaderUntypedPointers` | device creation |
| Root/device pointers | `bufferDeviceAddress` and `shaderInt64` (already required by the engine) | device creation |
| Ray tracing | `VK_KHR_ray_tracing_pipeline` (RT path only) | RT device creation |

The implementation PR updates `AGENTS.md` section 3 and `Consumers.md`; this design note does not change the
process file by itself. B0 records the same list on llvmpipe and each hardware driver, and unsupported
configurations are recorded rather than treated as a green run. `VK_KHR_unified_image_layouts` is optional
and deferred, not part of the floor.

**Evidence for the floor table.** The registry dependency expression for `VK_EXT_descriptor_heap` is
`((VK_KHR_extended_flags,VK_KHR_maintenance5)+(VK_KHR_buffer_device_address,VK_VERSION_1_2),VK_VERSION_1_4)`:
the `VK_VERSION_1_4` arm is the core path this design takes, while the KHR arm is the pre-1.4 route. The
engine already enables and **requires** `shaderInt64` and `bufferDeviceAddress`
(`src/RenderDevice/VkRenderDevice.cpp:352`, `:362`, `:541`, `:551`), so the "already required" row is
checked against the current tree, not assumed. B0 queries `descriptorHeap` and
`shaderUntypedPointers` at device creation and records them. `VK_KHR_unified_image_layouts` is absent
from the dependency expression; newer llvmpipe Mesa builds expose it, older ones and RADV 1.4.354 do
not, which is why it remains optional and deferred rather than part of the floor.

## 4. Push data and root data

Push data is **opaque to RenderGraph**. `vkCmdPushDataEXT` takes a byte blob; the extension does not require
a pipeline layout and does not prescribe a root structure. The graph records the bytes; the host owns the
layout.

| RenderGraph responsibilities | Host-renderer responsibilities |
| --- | --- |
| Bind the resource/sampler heaps and create heap-flagged pipelines with `layout = VK_NULL_HANDLE`. | Define the root struct, its size, alignment and CPU/shader ABI. |
| Record generic `vkCmdPushDataEXT(offset, bytes)` from a node callback; check the device's `maxPushDataSize`. | Choose handles, slot numbers, named fields, pointers and inline constants. |
| Optionally allocate host-writable root memory per execution, with a device address, lifetime and a host-write-to-shader barrier. | Decide whether root data is inline, indirect, GPU-generated or supplied to device-generated commands. |
| Track the resource uses the host declares and derive synchronization independently of root data. | Declare every resource the workload accesses, including bindless and indirect accesses. |

The graph does **not** know a `GraphRoot`, `images[128]`, `rootSlot`, a root size, or a slot-to-resource
agreement. Those are host choices. A host can push a 64-bit pointer, a pointer plus inline values, a
small inline struct, or nothing at all.

The generic API is intentionally small:

```cpp
// Host-facing, on RenderGraphNodeContext:
void PushData(uint32_t offset, std::span<const std::byte> bytes);

// Optional helper: a host-writable slice in the execution's root ring.
struct RootSlice
{
    std::span<std::byte> host;  // mapped for the host to write
    VkDeviceAddress      device;
};
RootSlice AllocateRoot(size_t size, size_t alignment);
```

`PushData` records `vkCmdPushDataEXT` and validates `offset + size <= maxPushDataSize`. `AllocateRoot` is
convenience, not policy: it returns memory valid for the current execution slot, records the necessary
host-write barrier, and resets only after the slot's timeline completes. A host that already owns root
memory can ignore it and push its own address.

### Host example: a root pointer

The following is **a host-renderer convention**, not a RenderGraph contract. It is the convention the
first renderer is expected to use; another host may choose something else.

```cpp
// Host renderer, not RenderGraph:
inline constexpr uint32_t kMaxRootImages   = 128;
inline constexpr uint32_t kMaxRootSamplers = 32;
inline constexpr uint32_t kMaxRootBuffers  = 32;
inline constexpr uint32_t kMaxRootAccel    = 8;

struct MyRoot
{
    uint32_t images[kMaxRootImages];
    uint32_t samplers[kMaxRootSamplers];
    uint32_t buffers[kMaxRootBuffers];
    uint32_t accel[kMaxRootAccel];
};
```

The shader declares the matching type and a push block:

```slang
struct PushBlock
{
    MyRoot* root;
    uint    drawIndex;
};

[[vk::push_constant]]
ConstantBuffer<PushBlock> push;
```

and the host writes the struct into a root slice, pushes the pointer, and uses the indices:

```slang
[shader("fragment")]
float4 shade(float2 uv : TEXCOORD0) : SV_Target
{
    // .Handle() is function-local: at module scope Slang treats the declaration as a
    // shader parameter and emits an ordinary DescriptorSet/Binding instead of heap access.
    Texture2D    albedo = Texture2D.Handle(uint2(push.root->images[kAlbedo], 0));
    SamplerState linear = SamplerState.Handle(uint2(push.root->samplers[kLinear], 0));
    Sampler2D    mixed  = Sampler2D.Handle(
        uint2(push.root->images[kAlbedo], push.root->samplers[kLinear]));
    StructuredBuffer<Material> materials =
        StructuredBuffer<Material>.Handle(uint2(push.root->buffers[kMaterial], 0));
    return mixed.Sample(uv);
}
```

**`.Handle()` must be function-local.** A module-scope declaration such as
`StructuredBuffer<float> buf = StructuredBuffer<float>.Handle(idx);` compiles, but Slang treats it as a
shader parameter: the SPIR-V carries `DescriptorSet`/`Binding` decorations and no heap access. On a
null-layout heap pipeline that is a binding against a layout that does not exist, or a silent route
through the mapping path. The global-scope shape is kept as a negative control in
`docs/DescriptorHeap-Probes/`; B0 rejects any positive `.heap.spv` that contains a `Binding` decoration.

The host chooses the sizes. The current lighting pass uses 11 images, 11 samplers and 3 buffers; a
128/32/32/8 layout fits it with room for a whole scene. A 10,000-texture scene stores heap indices in
material data and a 10,000-object scene stores transforms in an instance buffer; the root names the
top-level tables one draw reads, not every resource. These are host decisions, not graph checks.

The host also owns the shader/root agreement. The graph cannot prove that a shader reads the field the
host populated; the host's shared header and a readback test per pass are the mitigation. That diagnostic
does not move into RenderGraph with the root layout.

**Buffer device addresses remain a host option.** A host can push a buffer device address for large
constants or structured data and use `PhysicalStorageBuffer`, instead of consuming a typed buffer
descriptor. The heap remains the default path for shader-visible buffers, but the choice belongs with the
root layout, not the graph.

### Resource uses are independent of root data

`ResourceUse` describes access, not handles. A node that samples a texture through a heap index still
declares `ResourceUsage::SAMPLED` for that image; the graph barriers it, tracks its lifetime and aliases
it like any other use. The root representation never enters `ResourceUse`, so a host can change its root
layout without changing the graph's synchronization model.

This is also the boundary for opaque accesses. If a node can reach resources the graph cannot see in the
root data, the host must declare those uses:

- the declaration is the source of truth for barriers, transient lifetime, aliasing and resource reuse;
- a resource the host does not declare has no graph-side barrier or lifetime guarantee, even if a shader
  can reach it through the heap;
- the declaration is independent of root layout, so changing handles or root fields does not change the
  synchronization model;
- debug mode should flag bindless accesses to resources the node did not declare where the tooling can
  observe them.

Declaring every possible bindless resource can scale poorly. A host-facing resource-group or
scoped-bindless model could make the declaration coarser, but it is a future extension with its own
correctness argument, not a prerequisite of this design and not a graph-level GPU-driven feature.

### Resolving a declared use to a heap index

The graph does not put resources into the root data for the host, but it does resolve the declared uses.
The node context exposes the typed heap index for a heap-bound use:

```cpp
std::optional<uint32_t> GetHeapIndex(const ResourceHandle&, ResourceUsage) const;

// Optional sugar: write the 32-bit index at a host-chosen offset in a root slice.
bool WriteRootIndex(std::span<std::byte> root, size_t offset,
                    const ResourceHandle&, ResourceUsage) const;
```

`GetHeapIndex` succeeds for sampled/storage images, uniform/storage buffers and acceleration structures;
it returns `nullopt` for attachments, vertex/index/indirect buffers and transfer uses, which the graph
binds directly or which have no shader-visible heap descriptor. The index is resolved per execution, so
a host callback that runs per execution gets the current value rather than a stale one from `Build()`.

The host decides where the index goes:

```cpp
node.execute = [](RenderGraphNodeContext& ctx)
{
    MyRoot root{};
    if (auto i = ctx.GetHeapIndex("Albedo", ResourceUsage::SAMPLED))
        root.albedo = *i;
    if (auto i = ctx.GetHeapIndex("MaterialBuffer", ResourceUsage::STORAGE_BUFFER))
        root.materialBuffer = *i;

    auto slice = ctx.AllocateRoot(sizeof(MyRoot), alignof(MyRoot));
    std::memcpy(slice.host.data(), &root, sizeof(root));
    ctx.PushData(0, std::as_bytes(std::span{&slice.device, 1}));
};
```

That is the whole relationship between input/output management and root data:

- **Input/output is `ResourceUse`.** The host declares handles, `io` (READ/WRITE/READ_WRITE) and usage;
  the graph resolves them for barriers, transient lifetime, aliasing and reuse. The root data is not the
  dependency record.
- **Root data is shader access.** The host decides which declared uses need an index, where the index
  lives, and whether it is inline, indirect or GPU-generated. The graph supplies the current index; it
  does not require a slot, a layout or an agreement check.
- **Attachments need no index.** Color/depth attachments are bound through `vkCmdBeginRendering`; vertex,
  index and indirect buffers through their own commands. Only heap-visible shader resources are resolved
  to an index.
- **A resource used twice with different roles** (for example sampled and storage) is declared per role;
  `GetHeapIndex` takes the usage, so the host gets the descriptor for that role. If the roles need
  different layouts or descriptor types, they are separate uses and separate indices.

A host that wants a fixed layout can assign slots in its own header and copy the indices; a host that
wants a GPU-generated root can have a compute pass write the same indices into its own buffer. The graph
neither knows nor checks either layout.

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

| Region | Descriptor sizing and alignment | Index |
| --- | --- | --- |
| Images **and texel buffers** | `vkGetPhysicalDeviceDescriptorSizeEXT(type)`, at most `imageDescriptorSize`; aligned to `imageDescriptorAlignment` | `byteOffset / size` |
| Buffers **and acceleration structures** | `vkGetPhysicalDeviceDescriptorSizeEXT(type)`, at most `bufferDescriptorSize`; aligned to `bufferDescriptorAlignment` | `byteOffset / size` |
| Samplers (separate heap) | `vkGetPhysicalDeviceDescriptorSizeEXT(VK_DESCRIPTOR_TYPE_SAMPLER)`, at most `samplerDescriptorSize`; aligned to `samplerDescriptorAlignment` | `byteOffset / size` |

The table follows the property definitions: `imageDescriptorSize` is the maximum size of **image and
texel-buffer** descriptors, and `bufferDescriptorSize` is the maximum size of **unformatted buffer and
acceleration-structure** descriptors. `vkGetPhysicalDeviceDescriptorSizeEXT` is the authoritative
per-type size; the property is an upper bound. Texel buffers therefore belong in the image-sized,
image-aligned region, not the buffer region.

Each region has its own free-list and its own ring. The shader's handle type selects the stride:
`Texture2D`/`TextureBuffer<T>` use the image stride, `StructuredBuffer<T>`/`ByteAddressBuffer` the
buffer stride, `SamplerState` the sampler stride. The host's root data carries type-specific indices;
a buffer descriptor does not waste image-size padding.

**Acceleration structures are provisional.** Slang's default AS heap representation is a literal 8-byte
device address, but the heap descriptor written by `vkWriteResourceDescriptorsEXT` is sized by
`bufferDescriptorSize` and aligned by `bufferDescriptorAlignment`. An 8-byte shader-side representation
does **not** prove that an 8-byte packed heap stride is valid. B0 must query
`vkGetPhysicalDeviceDescriptorSizeEXT(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)`, write a real TLAS
descriptor and test the direct path. If the representation does not match on an implementation, AS uses
the mapping path (still heap-flagged, null layout) or is rejected with a named diagnostic; it is not
silently packed at 8 bytes.

**There is a heap ceiling, but it is not a root table.** RADV reports `maxResourceHeapSize = UINT64_MAX`
and llvmpipe about 8 GB; the practical bound is device memory, not a descriptor count. The host decides
how many heap indices its root data carries; the graph does not impose a root layout.

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
  divided by the type stride and do not change, so host root-data indices remain valid; the next command buffer
  binds the new range.

The shader-side stride is **not an offline ABI**. Leave `-spirv-resource-heap-stride` and
`-spirv-sampler-heap-stride` at their default value of `0`: Slang then emits `OpConstantSizeOfEXT` and
`ArrayStrideIdEXT`, and the implementation resolves the descriptor size when it compiles the pipeline.
The same `.heap.spv` can run on implementations with different descriptor sizes. The heap allocator uses
`vkGetPhysicalDeviceDescriptorSizeEXT` and the matching alignment at runtime; it does not feed those
values into the shader compiler. B0 inspects the emitted SPIR-V for `OpConstantSizeOfEXT` /
`ArrayStrideIdEXT` and runs the same module on more than one implementation, rather than checking
extension strings alone. Acceleration structures are the explicit exception covered above.

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
and break the host root data that points at them. The ring belongs to the dynamic region, sized
`maxDescriptorsPerExecution * inFlightCount` per region.

**Debug validation.** The service should carry a generation tag per slot and check it in debug builds, so
an index into a retired or in-flight slot is flagged instead of being read as a stale descriptor. The
same validation should cover root-memory slices when `AllocateRoot` is used.

**Preallocate, then grow rarely.** Start with a configured budget (8-16 MB is ~128k-256k 64-byte image
descriptors, far more than a scene needs; buffer and AS regions cost less per entry). If it is exhausted,
double at a frame boundary: allocate a new heap, copy every live descriptor, bind the new range from the
next command buffer, and retire the old heap once the last execution using it has completed. Indices are
byte offsets divided by the type stride, so they survive the copy and host root-data indices stay valid. If growth is
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
- stable per-frame variation is a **different value in the host root data**, not a rewritten descriptor.

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

**Input attachments are an exception.** The extension requires a binding/mapping for them; direct heap
access is not the path. B1 rejects `INPUT_ATTACHMENT` with a named diagnostic until the mapping slice is
implemented.

### Samplers

`vkWriteSamplerDescriptorsEXT` takes `VkSamplerCreateInfo`, not a `VkSampler`. The engine currently
creates samplers from local create-info structs and does not retain them
(`Texture.h:167`, `SamplerManager.h:36`). The heap service therefore needs a **sampler cache keyed on
create info**, or the resources must retain the create info. That is a prerequisite, not a detail.

**Custom border colors have a lifecycle.** A sampler whose `borderColor` is
`VK_BORDER_COLOR_FLOAT_CUSTOM_EXT` or `VK_BORDER_COLOR_INT_CUSTOM_EXT` must carry a
`VkSamplerCustomBorderColorIndexCreateInfoEXT` in its `pNext`, and the index must come from
`vkRegisterCustomBorderColorEXT` (VUID 11444/11205). The service owns that registry: register on first
use, key the sampler cache on the registered index, and unregister only when no cached sampler uses it.
This requires `VK_EXT_custom_border_color` and its `customBorderColors` feature; if the device does not
enable it, a custom-border sampler is a `Build()` error. A custom-border sampler without a registered
index is also a `Build()` error.

**YCbCr and subsampled samplers cannot be written to the sampler heap.** `vkWriteSamplerDescriptorsEXT`
forbids `VkSamplerYcbcrConversionInfo` in the create info (VUID 11204), and the proposal requires YCbCr
and subsampled samplers to be embedded through the mapping path. Direct-heap nodes reject them with a
named diagnostic until mapping support lands; they are not silently treated as ordinary samplers.

## 6. Build and Execute

`Build()` becomes a **frame build**:

1. Topological sort and declaration validation (unchanged).
2. Resolve every `ResourceUse` to a resource. **Resources are cached by `ResourceDesc`, not by handle
   name**, which fixes #209: a changed extent or format recreates; an unchanged declaration reuses.
3. Validate the host-declared resource uses and the node's push-data/root requirements; root layout and
   size are host-owned.
4. Derive the execution plan (queue segments, transfers).
5. Compile a pipeline **on a cache miss only**. The key is `PSODesc` + shader hashes + specialization
   constants. There is no descriptor layout or pipeline layout in the key, because there are none.

`Execute()`:

1. Acquire an executor slot (command buffers, optional root-memory ring, transient arena for that slot).
2. For each node, resolve the declared resources for barriers and `GetHeapIndex`, call the node callback,
   and let it record its own `vkCmdPushDataEXT` and root data. If the callback uses `AllocateRoot`, the
   graph records the host-write-to-shader barrier and resets that ring only after the slot's timeline
   completes. The graph never writes or interprets the host's root layout.
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
the layout field from the barrier path, but it is **not available on RADV 1.4.354** on the RDNA2 box, and
llvmpipe support is Mesa-version dependent (newer builds expose it, older ones do not). B3 is therefore
optional and driver-gated, not a B1 dependency. Until then, the
barrier code keeps only the present transition and the one-time conversion of imported images that are
not created in `GENERAL`.

The tradeoff is explicit: `GENERAL` can forgo a vendor's optimal-layout fast path. If a measured cost
appears, the response is a per-resource layout policy with a descriptor rewrite behind the timeline,
not a silent return to per-node descriptors.

## 9. Shader migration and the legacy renderer

Direct heap access is a shader-source change, not a compiler flag:

```slang
// before: descriptor-set declarations, module scope
[[vk::binding(0, SET_PER_VIEW)]] ConstantBuffer<PerViewData> perView;
[[vk::binding(0, SET_MATERIAL)]] StructuredBuffer<PBRMaterial> materials;

// after: handles inside the entry/function, addressed through the host's own root data
// (example; not a graph ABI). `push.root` is the host-defined pointer from §4.
[shader("fragment")]
float4 shade(float2 uv : TEXCOORD0) : SV_Target
{
    ConstantBuffer<PerViewData> perView = ConstantBuffer<PerViewData>.Handle(
        uint2(push.root->buffers[kPerViewSlot], 0));
    StructuredBuffer<PBRMaterial> materials = StructuredBuffer<PBRMaterial>.Handle(
        uint2(push.root->buffers[kMaterialSlot], 0));
    return materials[0].albedo.Sample(uv);
}
```

The `before` declarations are module-scope parameters by design; the `after` handles must be
**function-local**. Moving a `.Handle()` expression to module scope silently turns it back into a
binding (H1 in the review); B0 checks the emitted SPIR-V for `Binding` decorations as a negative
assertion, and the committed probes include the module-scope negative control.

The graph shaders are converted in B1b. The legacy renderer keeps its GLSL/set-binding shaders until
P3, so a pass shared by both paths gets two **separate sources**: `foo.slang` for the binding variant and
`foo.heap.slang` for the direct-access variant. The CMake shader build gains a second rule:
`.heap.slang` emits `.heap.spv` with `-capability spvDescriptorHeapEXT`, `-capability spirv_1_4`.
Leave `-spirv-resource-heap-stride` and `-spirv-sampler-heap-stride` at their default `0`, so the module
uses `OpConstantSizeOfEXT`/`ArrayStrideIdEXT`; no device-specific stride is baked. The existing rule is
unchanged, and no unified stride is used.
The graph loads the `.heap.spv` name; the legacy path loads the old `.spv`. That is deliberate debt with
an end date (the P3 deletion), not an argument for keeping a descriptor-set fallback in the graph. When
P3 deletes the legacy renderer, the old sources go and the `.heap` suffix can be dropped mechanically.

A pass that needs a direct-heap exception (input attachment, YCbCr/subsampled/embedded sampler) uses a
**mapped shader variant** with the required `DescriptorSet`/`Binding` decorations and
`VkShaderDescriptorSetAndBindingMappingInfoEXT`. It is still a heap-flagged, null-layout pipeline; the
mapping path is not a descriptor-set fallback. B1 rejects those passes with a named diagnostic until the
variant exists.

The graph also uses GLSL shaders (`GBuffer.frag`, `GBuffer.vert`, `test16BitStorage.comp`), so the
conversion is not Slang-only. glslang 16.6 advertises `GL_EXT_descriptor_heap` and
`GL_EXT_structured_descriptor_heap`; B0 checks the exact syntax and that the installed validator emits
the same capabilities, and B1b converts those shaders or replaces them with Slang equivalents.

`SPIRV-Reflect` in `thirdparty/SPIRV-Reflect` already understands `SPV_EXT_descriptor_heap` and
untyped pointers (`spirv_reflect.c:522`, `:3848`), so reflection still supplies push-constant ranges,
specialization constants and heap-access metadata. It will no longer supply descriptor bindings,
because there are none.

### Push constants become push data

`vkCmdPushConstants` requires a `VkPipelineLayout` and a `VkPushConstantRange`; a heap pipeline has
`layout = VK_NULL_HANDLE`, so the graph drops `pipelineLayout`, `vkCmdPushConstants` and push-range
plumbing from its API.

The shader-side block does **not** disappear. A `[[vk::push_constant]]` block is still compiled to the
SPIR-V `PushConstant` storage class; with a heap pipeline that storage is backed by push data. The host
keeps its existing block layout and offsets; only the recording call changes:

```cpp
// before:
vkCmdPushConstants(cmd, ctx.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, size, &value);

// after:
ctx.PushData(0, std::as_bytes(std::span{&value, 1}));
```

`PushData` records `vkCmdPushDataEXT` and copies the bytes into the command buffer; there is no host
lifetime to manage. A typed `PushData<T>(offset, value)` is convenience only; the byte form is the
contract. The graph checks `offset + size <= maxPushDataSize`.

Two ordering rules matter:

- **Keep heap and non-heap state separate.** `vkCmdPushDataEXT` invalidates non-heap descriptor state
  (descriptor sets and push constants); descriptor-set/push-constant commands invalidate heap state and
  push data. The graph path is heap-only: bind the heaps once and never call `vkCmdPushConstants` or
  descriptor-set commands in a node. Push data and heap binding coexist; the executor keeps both heaps
  bound while nodes push their data.
- **Bind heaps once, not per node.** Rebinding a heap is expensive and can invalidate non-heap state; the
  executor binds the resource and sampler heaps at the start of the command buffer and keeps them.

Push data is not per-stage the way push-constant ranges are; every stage sees the same blob. The host's
block layout is responsible for placing stage-specific values at the offsets each shader expects. The
common single-block case is unchanged. `helloVulkanRG.cpp:273` is the worked migration: the block stays,
the `pipelineLayout` argument and `vkCmdPushConstants` call go.

## 10. Synchronization

- Descriptors uploaded by a transfer need a dependency whose destination access is
  `VK_ACCESS_2_RESOURCE_HEAP_READ_BIT_EXT` or `VK_ACCESS_2_SAMPLER_HEAP_READ_BIT_EXT`, executed before
  the first shader that reads the heap. The extension defines both access bits.
- A host write straight into a host-visible heap needs the same destination access with a host-write
  source; a device-local heap plus a staging copy is the default and is what the sample uses.
- Root memory is host-written. If the host uses the graph's optional `AllocateRoot`, the graph records a
  `VK_ACCESS_2_HOST_WRITE_BIT` to shader-read dependency before the first node that reads it, and the
  root ring is per execution. If the host owns the memory, the host owns that dependency.
- `vkCmdPushDataEXT` is command-buffer state; no queue-level synchronization is needed beyond the
  existing node barriers.
- Rebinding a heap is avoided; the spec notes implementations may synchronize internally, and the
  recommendation is to keep the same heap for the application's lifetime.

## 11. Failure modes the graph must name

| Failure | When | Message / action |
| --- | --- | --- |
| Extension or feature absent | device creation | throw naming `VK_EXT_descriptor_heap`, `descriptorHeap`, `shaderUntypedPointers`, or `PhysicalStorageBuffer` support |
| Shader compiled without the heap capability | `Build()` | reject the node; the shader has no `ResourceHeapEXT` built-in |
| `.heap.spv` carries `Binding`/`DescriptorSet` decorations | `Build()` / B0 | reject; a module-scope `.Handle()` lowered to a parameter binding, see §4 |
| Push data over `maxPushDataSize` | record time | name the offset/size and the device limit; the host must split or move data into root memory |
| Optional root arena exhausted | `Execute()` | grow the arena or throw naming the node; never return a slice that aliases an in-flight root |
| Input attachment declared under direct heap access | `Build()` | reject with a named diagnostic until the mapping path is implemented |
| YCbCr / subsampled / embedded sampler declared under direct heap access | `Build()` | reject with a named diagnostic; these require the mapping path |
| Custom-border sampler without a registered index | `Build()` | reject; the create info must carry `VkSamplerCustomBorderColorIndexCreateInfoEXT` |
| Acceleration-structure direct representation unverified/mismatched | B0 / `Build()` | record the implementation; use the mapping path or reject, never assume an 8-byte packed stride |
| Descriptor heap full past `maxResourceHeapSize` | `Build()` / allocation | name the resource and the size; do not silently drop a descriptor |
| Sampler has no create info | `Build()` | ask the resource for it; a sampler handle alone cannot be written |

## 12. Migration slices

| Slice | Scope | Gate |
| --- | --- | --- |
| **B0 — probe** | Device features and heap properties; the committed `docs/DescriptorHeap-Probes/` probes; one compute and one graphics draw through a null-layout pipeline, host-defined push data and direct heap access; one RT pipeline with the heap flag; an AS direct/mapping decision; readback. | The same `.heap.spv` runs on RADV REMBRANDT, the RTX 3090 **and** a llvmpipe whose Mesa exposes `VK_EXT_descriptor_heap` + `shaderUntypedPointers`; the probes validate and contain `OpConstantSizeOfEXT`/`ArrayStrideIdEXT`; **no positive `.heap.spv` contains a `Binding` decoration**, while the module-scope control does; descriptor sizes/alignments, `maxPushDataSize`, max heap sizes, reserved ranges, RT acceptance and `VK_KHR_unified_image_layouts` are recorded. The device line records driver, Mesa and loader versions plus heap/untyped support; absence is recorded, not counted as green. This is the go/no-go. |
| **B1a — heap service** | `DescriptorHeapService` with image/texel and buffer/AS regions, per-region caches, debug slot generations; optional root-memory ring; bind once; device layer 1.4 features and function pointers. | A test writes image, texel-buffer, buffer, AS and sampler descriptors with queried sizes/alignments, reads them through host-defined push data, and creates no descriptor set or pipeline layout. AS direct access is either proven or routed to the mapping path. |
| **B1b — graph bind** | Generic `PushData` + optional `AllocateRoot`; `GetHeapIndex`/`WriteRootIndex` resource-index resolver; host-defined root ABI; separate `.heap.slang` sources; named rejection of input attachments/YCbCr until mapping; delete `DescriptorSetCache`, set/pool/layout creation, `VkPipelineLayout` and graph push-constant plumbing from `src/RenderGraph/`. | Existing graph suite green in Debug and Release; `grep` for `vkCreateDescriptorSetLayout`, `vkAllocateDescriptorSets`, `vkCreatePipelineLayout`, `vkCmdPushConstants` in `src/RenderGraph/` is empty; no `GraphRoot`, `rootSlot` or `SetRoot*` in `src/RenderGraph/`; a node queries a heap index, writes its own root, and records `PushData`. |
| **B2 — frame build** | Per-frame `Build()` with pipeline/resource/descriptor caches; optional per-slot root arena and transient arenas; `inFlightCount > 1`. | Resize and swapchain rotation with a build per frame; #209 regression test fails before the fix; #40's overlap test. |
| **B3 — unified layouts** | **Deferred, not scheduled.** Adopt `VK_KHR_unified_image_layouts` when every target driver exposes it (newer llvmpipe Mesa does; older llvmpipe builds and RADV 1.4.354 do not). The GENERAL policy already removes the transitions. | No gate now; when adopted, no image-layout transition in a graph execution and validation clean. |
| **B4 — GPU-written root data** | The optional root arena is storage-capable so a **host** compute pass can fill it; a bindless draw consumes the result. Optional direct `ResourceDescriptorHeap` sugar. | A host compute pass writes root data and a bindless draw reads it; the graph adds no GPU-driven API. |

B1 is the point of no return for the descriptor-set path. B0 is where the hardware claim is settled.

## 13. Roadmap reconciliation

This section is a proposal for `docs/Roadmap.md`; the roadmap PR applies it after this note is accepted.

| Current item | Disposition |
| --- | --- |
| **#15** descriptor-set retention | **Re-scoped** to cross-frame retention (issue updated). The cache/cliff half landed in #108/#109; close it after B1 if the heap path lands. |
| **#16** transient lifetime/aliasing | **Closed; no new work.** Aliasing remains a B2 optimization; the per-execution arena work belongs to #40's re-scope, not the closed issue. |
| **#17** pipeline/shader cache | **Prerequisite for B2**, and simpler: no layout/descriptor state in the key. |
| **#40** in-flight > 1 | **Re-scoped.** The descriptor-set half is gone; the remaining work is per-execution transients and the optional root-memory arena. |
| **#83** per-execution bindings | **Close after B2.** Different root data is a different value per execution; no rebuild and no descriptor rewrite. |
| **#209** rebuild keeps stale resource | **Fixed in B2** by the desc-keyed resource cache. |
| **#12** barriers | **Closed; no new work.** The stage/access work landed; the unified-layout removal is B3/deferred. |
| **#14/#19** RT parity | RT shaders use heap handles. B0 verifies a null-layout RT pipeline; if a driver rejects it, RT waits on that driver rather than getting a second binding path — the graph does not keep descriptor sets for it. |
| **#57** client frame helper | Unaffected; runs in parallel. |
| **#20** `helloVulkan` port | **Freeze further ports after #111 until B2.** #111 already ported the UI pass; it is accepted double-conversion debt. Porting more passes now would convert twice; if work continues, record the double conversion as accepted debt. #20 remains open for the final port after B2. |
| **#51** delete `RenderPassManager` | Unchanged, after P2. |
| **#53/#58/#54** library track | `DescriptorManager` becomes the heap service; device floor becomes 1.4; #54's compute-only test runs on the heap. |
| **#59** cost criterion | Add per-frame build CPU time and heap/descriptor traffic. Host-side draw-call and GPU-driven costs are the renderer's own metrics, not the graph's. |
| **`AGENTS.md` / CI** | Raise the floor to 1.4, enable and verify `descriptorHeap` + `shaderUntypedPointers`, and record driver, Mesa, loader and heap/untyped support in the `[test device]` line. Lavapipe gates the path only when its Mesa exposes both; absence is recorded, not green. |
| **`docs/DescriptorSet-Lifecycle-Design.md`** | Mark superseded, and remove it from the Doxygen `INPUT` set and `scripts/render_graph_docs.py` in B1b so the API reference stops describing deleted types. |
| **`Consumers.md` / muyo-llm** | The hard requirement and the 1.4 floor are a breaking change; name the consumer and add the guarantee row before B1b lands. |

**New track `B Bindless (descriptor heap)`** alongside P0–P3 and L, with the B0–B4 gates above. There is no separate GPU-driven track: those are host renderer features, and the graph only has to stay out of their way.

**Critical path:** `#17 → B0 → B1 → B2 → #20 (final) → #51`. #111 landed the UI pass before the freeze and
is accepted double-conversion debt. #57 runs in parallel; the L chain is unchanged; #59 spans B2 and #20.
P1 cannot close until B2 and #59; P2 cannot start its full port until B2.

**The documentation PRs.** #208 was closed as superseded; this note keeps its
structure-at-`Build()`/handles-at-`Execute()` split. #210 has merged unchanged (`acc0e12`) as the
pre-bindless baseline snapshot. This note's roadmap follow-up removes the `#208` reference and applies
the B track; there are no post-approval edits to #210.

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

1. **Driver and tooling maturity.** The extension is new. `descriptorHeapCaptureReplay = false` on llvmpipe; RenderDoc and vendor profilers may not understand a capture yet. llvmpipe's heap/untyped
   support depends on the Mesa build (Mesa 26.2.5 exposes both; an LLVM 20.1.2 build exposes neither), so
   CI must record the versions rather than assume support. B0 records what each machine reports, and the
   first bug is attributed to the driver or to us before it is worked around.
2. **API floor.** Vulkan 1.4 is a deliberate break from the current 1.3 floor in `AGENTS.md`; it must
   be recorded there and in `Consumers.md`, not only in code.
3. **Shader rewrite cost.** B1b converts every graph shader, and the legacy renderer needs a second
   variant until P3. The count is small (19 Slang shaders; 8 with explicit `[[vk::binding]]` or a
   `SET_(PER_VIEW|PER_OBJ|MATERIAL)` mention by
   `grep -lE '\[\[vk::binding|SET_(PER_VIEW|PER_OBJ|MATERIAL)' shaders/*.slang | wc -l`) but the
   overlap is real.
4. **Host root memory.** If the host uses a raw pointer/`PhysicalStorageBuffer`, there are no bounds
   checks: a bad pointer is a GPU fault, not a validation error. If it uses the graph's optional
   `AllocateRoot`, the graph owns the ring and the host-write barrier, and exhaustion is an
   `Execute()` error. If it owns the memory, it owns both. The graph imposes no root table or size.
5. **`GENERAL` layout cost.** Unmeasured. Unified layouts are deferred because RADV 1.4.354 does not
   expose them; a measured regression is the trigger for a per-resource policy, not for blocking B1.
6. **Sampler create info and lifecycle.** The engine does not retain create info; custom-border samplers
   need registered indices and unregistration; YCbCr/subsampled samplers cannot use the standalone
   sampler heap and need the mapping path. B1 rejects the latter with named diagnostics until then.
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
11. **Acceleration-structure sizing is unproven.** Slang's 8-byte AS heap representation may not match
    the implementation's `bufferDescriptorSize`/`bufferDescriptorAlignment`. B0 must test a real TLAS;
    if it does not match, AS uses mappings or is rejected, not packed at 8 bytes.

## 16. Evidence collected for this draft

The probe sources are committed in `docs/DescriptorHeap-Probes/`; `run.sh` compiles them with Slang,
validates them with `spirv-val`, and checks the SPIR-V words with `spirv_check.py`. The parser is
deliberate: this SDK's `spirv-dis` does not know capability 5128 (`DescriptorHeapEXT`) and aborts on
some valid modules.

```bash
source <vulkansdk>/setup-env.sh
./docs/DescriptorHeap-Probes/run.sh
# heap_function_local: extensions=SPV_KHR_untyped_pointers,SPV_EXT_descriptor_heap,... bindings=0
# heap_texture:        extensions=SPV_KHR_untyped_pointers,SPV_EXT_descriptor_heap bindings=0
# root_pointer:        extensions=SPV_KHR_physical_storage_buffer,...,SPV_EXT_descriptor_heap bindings=0
# heap_global_negative: extensions=SPV_KHR_storage_buffer_storage_class bindings=2
# descriptor-heap probes: OK
```

**H1 result.** A module-scope `.Handle()` produced only ordinary shader/storage-buffer capabilities plus
`DescriptorSet`/`Binding` decorations; the function-local form produced `DescriptorHeapEXT`,
`UntypedPointersKHR`, `ResourceHeapEXT`/`SamplerHeapEXT`, `OpBufferPointerEXT` and
`OpConstantSizeOfEXT`/`ArrayStrideIdEXT`. The negative control preserves the module-scope shape, and the
positive probes must have no `Binding` decoration. The migration examples are function-local.

**Driver rows.**

- **llvmpipe, Mesa 26.2.5 (this development box):** exposes `VK_EXT_descriptor_heap` revision 1,
  `descriptorHeap = true`, `shaderUntypedPointers = true`, `VK_KHR_unified_image_layouts`, sampler/image/
  buffer sizes 0x20/0x40/0x10, `maxPushDataSize = 0x100`, zero reserved ranges.
- **llvmpipe, LLVM 20.1.2 (review machine):** exposes neither `VK_EXT_descriptor_heap` nor
  `shaderUntypedPointers`. This is the important negative: a lavapipe CI run is a heap gate only when the
  runner's Mesa exposes both. The `[test device]` line must record driver, Mesa, loader and the two
  feature bits; absence is recorded, not green.
- **RADV REMBRANDT, driver/API 1.4.354:** `descriptorHeap = true`, `shaderUntypedPointers = true`,
  sampler/image/buffer sizes 16/64/16, `maxPushDataSize = 0x100`, `maxResourceHeapSize = UINT64_MAX`,
  ray tracing present, `VK_KHR_unified_image_layouts` absent.
- **RTX 3090:** `VK_EXT_descriptor_heap` revision 1 and `shaderUntypedPointers = true` at enumeration
  level; the render path is B0, not claimed here.
- **AMDVLK 1.4.313:** no `VK_EXT_descriptor_heap`, no `VK_KHR_unified_image_layouts`.

**Quoted VUIDs.** `VkRayTracingPipelineCreateInfoKHR-flags-11311`,
`vkCmdTraceRaysKHR-None-11308/11309/11376`, and `vkWriteSamplerDescriptorsEXT-pSamplers-11204` /
`-borderColor-11444` / `-borderColor-11205` were checked against the SDK 1.4.363 `validusage.json` by the
review; they exist on the commands the note cites them for.

**Shader counts.** `ls shaders/*.slang | wc -l` is 19;
`grep -lE '\[\[vk::binding|SET_(PER_VIEW|PER_OBJ|MATERIAL)' shaders/*.slang | wc -l` is 8. The earlier
"9 with bindings" was wrong.

**Descriptor-size definitions.** `imageDescriptorSize` covers images and texel buffers;
`bufferDescriptorSize` covers unformatted buffers and acceleration structures;
`vkGetPhysicalDeviceDescriptorSizeEXT` is authoritative per type. The AS direct path is **not** tested:
B0 must write a real TLAS descriptor and compare the shader representation with the implementation's
size/alignment.

**GLSL path.** Not compiled here: glslangValidator 16.6.0 advertises `GL_EXT_descriptor_heap` and
`GL_EXT_structured_descriptor_heap` in `glslang/MachineIndependent/Versions.h`; B0 settles the syntax and
emitted capabilities.

## 17. Open questions

1. **Push-data and root-memory API.** Is `PushData(offset, bytes)` plus `AllocateRoot(size, alignment)`
   the right minimal surface? Should the helper start host-visible with a per-execution barrier, or
   device-local with a staging copy? The graph must not learn the host's root layout either way.
2. **Direct-heap exceptions.** B1 rejects input attachments and YCbCr/subsampled samplers with named
   diagnostics; should the mapping slice land in B1 or B2? The same decision applies to acceleration
   structures if B0 finds their direct representation does not match the implementation's descriptor
   size/alignment.
3. **Descriptor sizing and AS.** The allocator will use `vkGetPhysicalDeviceDescriptorSizeEXT` and the
   matching alignment per type; texel buffers sit in the image region. B0 must prove the AS direct path
   on each implementation, or route it to mappings. No 8-byte packed AS assumption is accepted yet.
4. **Heap budget**: static free-list plus per-slot rings, preallocated at 8-16 MB and doubled at a frame
   boundary. Is 16 MB acceptable on the lowest-memory target?
5. **RT shader records**: the null-layout RT pipeline is spec-required with the heap flag
   (`VUID-VkRayTracingPipelineCreateInfoKHR-flags-11311`) and `vkCmdTraceRaysKHR` has heap VUIDs, so RT
   can drop the layout. Per-hit resources from SBT shader records are a later slice; B0 only proves the
   pipeline and a global-descriptor trace.
6. **Unified image layouts**: RADV 1.4.354 on this RDNA2 box does **not** expose
   `VK_KHR_unified_image_layouts`; newer llvmpipe Mesa builds do, older ones do not. B1's `GENERAL`
   policy already removes layout transitions, so B3 is optional cleanup when the driver exposes it, not a
   B1 dependency. The RTX 3090 driver is recorded in B0.
7. **Opaque bindless declarations.** The first implementation requires a resource use per accessed
   resource and keeps the root representation out of it. Is a host-facing resource-group/scoped-bindless
   API needed before a GPU-driven pass declares hundreds of resources? If so, it must define the
   barrier/lifetime contract; it is not part of this design.

GPU-driven questions (DGC layouts, state buckets, meshlet formats, work graphs, CPU draw budgets) are
**host renderer** questions, not this note's. The compatibility surface they rely on is §14; the answers
belong in the host renderer's own design and backlog.

## References

- [VK_EXT_descriptor_heap proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_descriptor_heap.html)
- [Vulkan spec: Descriptor Heaps](https://docs.vulkan.org/spec/latest/chapters/descriptorheaps.html)
- [Descriptor heap properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceDescriptorHeapPropertiesEXT.html)
- [SPV_EXT_descriptor_heap](https://github.khronos.org/SPIRV-Registry/extensions/EXT/SPV_EXT_descriptor_heap.html)
- [Slang descriptor handles and `spvDescriptorHeapEXT`](https://github.com/shader-slang/slang/blob/master/docs/user-guide/03-convenience-features.md)
- [NVIDIA `vk_mini_samples` descriptor heap sample](https://github.com/nvpro-samples/vk_mini_samples/tree/main/samples/descriptor_heap)
- [No Graphics API](https://www.sebastianaaltonen.com/blog/no-graphics-api/)
- `docs/DescriptorSet-Lifecycle-Design.md` — superseded by this note.
