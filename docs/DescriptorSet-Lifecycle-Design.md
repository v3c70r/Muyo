# Descriptor set lifecycle — design note

**Status:** design only, not implemented. Captures the reasoning behind the N2 finding from the
PR #9 review and the follow-up discussion. The implementation is deferred to a dedicated
descriptor-set refactor branch (see *Sequencing*).

This note deliberately sits outside the Doxygen API reference: it describes a design we have not
committed to yet.

## The problem (N2)

`MATERIAL` is one of the three built-in semantic descriptor sets. Its binding 1 is the bindless
texture array:

```cpp
// RenderGraphDescriptorSets.h
{ .binding = BINDING_TEXTURS,
  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
  .descriptorCount = MAX_BINDLESS_TEXTURE_COUNT /* 1024 */ }
```

Descriptor pool accounting charges a set's declared descriptor counts **at allocation time**, and
the pool only holds `COMBINED_IMAGE_SAMPLER = 2048` (`DescriptorManager.h`). So each set that
contains this binding costs 1024 descriptors, and **three such sets exhaust the pool**.

This is a cliff, not a current failure: no shader in the tree declares set 2 yet, so the set is
never allocated. It becomes real the first time a graphics shader samples materials.

## Root cause: why sets are allocated per node at all

The frame is recorded into a single command buffer, and the two calls involved have different
timing:

- `vkUpdateDescriptorSets()` mutates the set **immediately, host-side**.
- `vkCmdBindDescriptorSets()` records the set **handle**, not a snapshot.

With one shared set, every draw therefore observes the *final* contents:

```
record: node A update(set0, cameraA); bind(set0); draw
        node B update(set0, cameraB); bind(set0); draw
submit: draw A reads set0 -> cameraB   (wrong)
        draw B reads set0 -> cameraB
```

Per-node sets are the fix for that (this was C5). The mistake was applying it uniformly: the three
semantic sets were treated as one mechanism, so `MATERIAL` — which is frame-invariant — also got
duplicated per node, and its 1024-descriptor array went with it.

## Design questions

### 1. Should a descriptor set be a graph *resource*?

Partially. A descriptor set is a **binding-time container**, not a data dependency. If the graph's
unit of dependency became "node uses set S", `RecordBarriers` would either have to expand S back
into its members (the container buys nothing) or synchronise on the whole group (false
dependencies, over-wide barriers). The resources inside a set must stay individually declared for
barrier purposes.

Where it *is* a resource is **identity and lifetime**: a set is allocated, bound, and released, and
(in the target design) shared and cached. That can be modelled without making it the unit of
dependency.

Externally built sets (a process-global bindless table, a legacy set) are the one case where the
graph genuinely cannot derive the contents — those should be **imported**, exactly like an imported
buffer.

### 2. Should resources be grouped into a container resource?

As the primary model, no:

- It duplicates information. SPIR-V reflection already declares the set layout; `ResourceUse`
  already declares who fills each slot. A separate container is a second source of truth that can
  drift.
- It coarsens dependency tracking (see above).

As sugar, yes — `AddResourceGroup("GBuffer", { albedo, normal, ... })` that simply expands into
individual `ResourceUse`s is fine, because the graph still tracks the members.

### 3. Infer the set from the resources — how?

By **binding content**, not by resource name. The desired behaviour (`PerView` reuses a set,
`PerView_mirror` gets its own) follows from resource identity, so name heuristics
(`PerView` vs `PerView_mirror` string matching) are unnecessary and fragile.

The key the graph can already build from reflection + `ResourceUse`:

```cpp
struct DescriptorSetKey
{
    uint32_t set;                                          // descriptor set index
    ShaderReflection::DescriptorSetLayoutSignature layout; // from SPIR-V reflection
    std::vector<BoundResource> resources;                  // (binding, resource handle, image layout)

    bool operator==(const DescriptorSetKey&) const;
};
```

- Two nodes binding `PerView` at (set 0, binding 0) → same key → **one `VkDescriptorSet`**, one
  `vkUpdateDescriptorSets`. The first node pays, the rest hit the cache.
- A node binding `PerView_mirror` there → different resource identity → different key → its own set.
- The layout is part of the key so two nodes that bind different resource *types* at the same set
  index never share.

## Recommended design: a content-addressed descriptor set cache

Keep the public API unchanged. Add a derived layer that owns set allocation, writing and lifetime:

```cpp
class DescriptorSetCache
{
public:
    // Hit -> reuse and bump the reference count. Miss -> allocate, write, insert.
    VkDescriptorSet Acquire(const DescriptorSetKey& key, ResourceLifetime scope);

    // Return frame-scoped sets to the pool. Called at the end of RenderGraphBuilder::Execute().
    void ReleaseFrameTransient();
};
```

- **Scope / retention.** A set whose members are all frame-invariant (`Persistent` / `Imported`)
  is retained across frames. Anything touching a transient or per-node resource is frame-scoped and
  released at frame end.
- **Lifetime.** A set's validity is the *intersection* of its members' valid intervals, and it must
  be rebuilt when membership changes. Because the key includes the concrete resource identity, this
  is automatic — including under future transient-resource aliasing, where two images share memory
  but have different views.
- **Import path.** `ImportResource("Bindless", pDescriptorSet)` for sets the graph cannot derive.
- **Sugar (optional, later).** Resource groups that expand to individual uses.

This removes N2 by construction: `MATERIAL` is a *content* that every material-using node shares,
so it is one set rather than N × 1024 descriptors. The asymmetry was never about the resource — it
was that one set's contents are frame-invariant, which the key expresses directly.

## Constraints

1. **No mid-frame recycling.** Descriptor set contents must not change while any command buffer that
   binds them is pending, unless the binding was created with
   `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`. So the release granularity is the **frame**:
   "no longer used" means "this frame is done", not "this node is done". Shorter-lived recycling
   requires update-after-bind.

2. **Frames in flight multiply retention.** With A1 (per-frame fences + timeline semaphores), a
   frame-scoped set becomes `PerFrame × framesInFlight`. Update-after-bind is what lets those share
   fewer instances.

3. **Pool headroom.** Until the cache lands, the pool can be bumped or
   `MAX_BINDLESS_TEXTURE_COUNT` reduced as a stopgap. Neither is the fix.

## Alternatives considered

| Option | Verdict |
| --- | --- |
| Share only `MATERIAL` across nodes (minimal stopgap) | Works, but keeps the hardcoded global/per-node split and does not generalise. Superseded by the content-addressed cache. |
| Explicit `DescriptorSetScope { Global, PerFrame, PerNode }` | Small and removes the cliff; the cache subsumes it. Good interim step if the cache is delayed. |
| Push descriptors (`VK_KHR_push_descriptor`) | Records writes into the command buffer, so the update/bind timing problem disappears entirely. Good fit, needs the extension and a layout flag. |
| Descriptor buffers (`VK_EXT_descriptor_buffer`) | Endgame: the descriptor table is a `BufferResource`, already a graph resource; the cache becomes a bump allocator and intra-frame reuse becomes legal. This is where A4 points. |

## Sequencing

1. **A1** — per-frame fences + timeline semaphores. Do this first: it changes the lifetime math, and
   doing it after the cache means redoing the retention counts.
2. **Descriptor set cache** (this note) — parameterised by `framesInFlight` from the start.
3. **A4** — descriptor buffers / bindless; the cache degrades to an allocator over a buffer
   resource.

## Accepted debt

Until step 2 lands, `MATERIAL` allocates one 1024-descriptor set per material-binding node. This is
accepted for now because the graph is not yet used by the application (it drives the tests only),
and no shader declares set 2. It must be resolved before the graph takes over a real material pass,
along with the rest of A4.

## References

- `src/RenderGraph/RenderGraphDescriptorSets.h` — layouts, per-node `AllocateSemanticSet`,
  `BindResourceToDescriptorSet`.
- `src/RenderGraph/RenderGraphBuilder.cpp` — `CompileRenderGraphNode` (per-node set allocation,
  lazy `nSetCount`), `Execute` (semantic write + bind), `DestroyCompiledRenderGraphNode` (release).
- `src/RenderResources/DescriptorManager.h` — pool sizes.
- PR #9 review, findings C5/C6/N2/A4.
