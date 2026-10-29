# Roadmap: ship the RenderGraph as a library; retire RenderPassManager on the way

## Goal

**Ship `RenderGraph` as an independent library/tool.** `helloVulkan` is its *first consumer* — a rendering
pipeline built on top of it — and the consumers after that do not look like a renderer at all: ML model
inference, LLM inference (long-running, compute-only), and hybrids where some queues span a frame
boundary.

**Retiring `RenderPassManager` is a milestone on that path, not the goal.** It matters because the first
consumer must actually use the library — a graph exercised only by its own tests is not a library anyone
else will adopt — but "the old renderer is deleted" is a consequence of the goal, not the goal.

**Definition of done:**

```bash
grep -rn "RenderPassManager" src/          # expect: no matches
ls src/RenderPasses/ 2>/dev/null           # expect: does not exist
```

## Where the plan lives

This document holds **reasoning**. GitHub holds **state**. They are split so each fact has one owner and
one place to edit:

| Fact | Owner | Where to edit |
| --- | --- | --- |
| Which phase an issue belongs to | Milestone | `P0`…`P3` |
| A phase's gate, and why it is the gate | Milestone description | the milestone itself |
| Which issues are on the critical path | Project field | `Critical path` |
| What is blocked by what | Project field | `Depends on` |
| Where an issue has got to | Project field | `Status` |
| The debt verdict for an issue | Project field | `Debt` (`Fix early` / `Subsumes debt` / `After P3`) |
| Why the phases are ordered this way | **This document** | below |
| The debt policy — including why some debt is deliberately *not* fixed | **This document** | below |
| The risks, and what mitigates them | **This document** | below |
| Where the project stands overall | **This document** | below |

**The consequence: this document does not list which issues are in a phase, and does not restate a
gate.** A phase's members are its milestone — open it, that is the list. Restating them here is what
made the first version of this document need editing twice for every backlog change, which is the
duplication it warns about three sections further down.

The debt tables below are the exception that proves the rule, and the exception is narrower than it once
read: the **verdict** is a field (`Debt`, one of the four custom fields below), and what the tables hold is
the **reasoning** — why this verdict rather than another — plus the items that were never filed as an issue
at all (the subsumed `TODO`s, which live in code). If a fact can be a field, make it a field.

## Where we are

Measured on 2026-10-29 after the #20 increment-1 port (`helloVulkan` presents through the graph).

| | Lines | State |
| --- | --- | --- |
| `src/RenderGraph/` | 8,429 | Drives the **tests**, `helloVulkanRG`, `helloVulkan`'s frame loop and present, and the external `muyo-llm`. Foundation, sync2, timeline execution, trace, heap-only bindless, per-frame `Build()` caches, per-execution rings and transient arenas. |
| `src/RenderPasses/` | 4,095 | The scene passes `helloVulkan` still records with, plus `ShadowPassManager`, which owns its own `RenderPassRSM` set. Its acquire/present ownership is gone; the remaining passes port one per PR (#20). |
| `src/app/helloVulkan.cpp` | 552 | `FrameSync` owns acquire/present and `FINAL` is a graph node; the scene passes are still legacy (#20 increment 1). |
| `src/app/helloVulkanRG.cpp` | 426 | The graph-first app: `FrameSync` acquires/presents, the graph owns the frame. |
| `src/FrameSync.{h,cpp}` | 252 | The client-side frame helper (#57), deliberately outside `src/RenderGraph/`. |

The counts are each tree's `*.cpp` and `*.h` concatenated and `wc -l`-ed —
`cat $(find src/RenderGraph -name '*.cpp' -o -name '*.h' | sort) | wc -l` — and they are a size signal rather
than a target. They rotted once already: the graph gained roughly 400 lines while this document sat still,
from `#18`, `#85`, `#90`, `#92` and `#94` alone.

The other two boundary measurements below are commands, not prose:
`grep -rnE 'GetRenderDevice\(\)|GetRenderResourceManager\(\)|GetDescriptorManager\(\)' --include=*.cpp src/RenderGraph/ | wc -l`
counts the singleton call sites, and the external-include scan is the Python block in
[`Roadmap-Visual.md`](Roadmap-Visual.md) section 7. Both were re-run at this head.

**Adoption has started in the in-repo renderer.** `helloVulkan` acquires and presents through `FrameSync`
and its `FINAL` pass is a graph node; `RenderPassManager` no longer owns the swapchain, acquire, present or a
per-image fence (#20 increment 1). The scene passes still record through `RenderPassManager` and the
composite is a manager-owned image the graph imports with its stated layout, so the asymmetry that remains
is the passes, not the frame loop. The suite, `helloVulkanRG`, `helloVulkan` and the external `muyo-llm`
consumer now all exercise the graph. Closing the pass gap is the risk this roadmap exists to close — see
Risks.

**The descriptor-heap direction is recorded, accepted, and implemented through B2.** `docs/DescriptorHeap-Bindless-Design.md`
merged in #211 (`80465ff`); it replaced the descriptor-set path with two physical heaps, null-layout
pipelines, host-owned push/root data and per-frame `Build()`. The **B track** (#212) is complete through
B2: B0 (#213), B1a (#215), B1b (#216) and B2 (#217) landed, the last one as PR #254. The graph path has
no descriptor sets, layouts, pools or `VkPipelineLayout`; `Build()` is per frame with resource, pipeline
and descriptor caches; and `inFlightCount > 1` now works with graph-owned transients (#40, #83). B3 is
deferred and driver-gated; B4 is optional. The final `#20` port is no longer frozen; the #111 UI pass is
the one deliberate double conversion, kept as accepted debt.

## The library goal, and what it constrains now

**Target shape, and where the line goes per concern:
[RenderGraph-Library-Design.md](RenderGraph-Library-Design.md).** It holds the stack
(`device → graph → pipeline → host`), which layer owns what, the decisions already made — C++ headers now
with a C ABI wrapped later if a non-C++ client appears; the device as a shared library rather than an
inverted dependency; error reporting left open on purpose — and the one piece of app policy that has to
come out of the device layer (validation setup).

Two pieces of work are imminent and would bake in assumptions that are expensive to remove. This section
is the reason to read the roadmap before starting them.

### A1.5 should be split: presentation is a client concern

`FrameSync` as planned — `NUM_BUFFERS` slots, per-frame acquire/render-finished binaries, an
images-in-flight map, present ids — is **swapchain machinery**. A library whose second consumer is an
inference job must not contain it.

The execution path is currently presentation-free: the only mentions of a swapchain anywhere in
`src/RenderGraph/` are comments describing what a *caller* might pass (`signalSemaphore`, "e.g. for
`vkQueuePresentKHR`"). A1.5 as written would be the first presentation-shaped code in the library, and
A1.4's timeline values are exactly what makes the split natural — completion becomes a value the client
waits on, and how it then presents is its own business.

- **library**: execution, completion as a timeline value, chaining one execution on another, slot
  reclamation. No surface, no present, no notion of a frame.
- **client**: acquire, images in flight, present ids, and the helper that wires a frame loop to the
  library's timelines. `helloVulkan` needs it; an inference client does not.

### Constraints to respect from here

**This table owns *why* a constraint is expensive to retrofit; [`Consumers.md`](Consumers.md) owns
*who depends on it and whether it holds*.** They list overlapping subjects by design — a constraint with
no consumer behind it is a guess, and a consumer with no constraint named is a hidden dependency — but
each fact has one owner: adding a constraint means a row here for the reasoning and a row there for the
state, and neither should try to carry the other's half.

| Constraint | Why it is expensive to retrofit |
| --- | --- |
| **No frame boundary in the library** | An inference client submits work that outlives many display frames. `Execute()` draining at frame end is a convenience to keep, not the model: completion has to be expressible as a timeline value the client can chain across frames. |
| **Device, allocator and descriptor allocation injected, not global** | The injection (#85) replaced the singleton lookups inside `src/RenderGraph/`; 1 transitional call site remains (`GetRenderResourceManager()` in the `RenderGraphBuilder(VkRenderDevice*)` delegation). A library cannot own the engine's singletons. |
| **Compute-only is a first-class profile** | No surface, no present, possibly no colour attachments. Nothing on the execution path may require a graphics family or a swapchain. |
| **Long-lived resources are first-class** | Weights and a KV cache live for the process, not a frame. The transient/persistent split (`#15`, `#16`) has to serve that, not only per-frame render targets. |
| **A stated public surface** | Every header in `src/RenderGraph/` is public today, reflection and descriptor internals included. Deciding what is API and what is detail is a prerequisite for shipping, and cheapest while there is one consumer. |
| **Diagnostics an agent can act on** | The library's consumers include coding agents, which read a log with no debugger and no follow-up question. That means failing at `Build()` on declaration errors, naming the graph node and resource rather than the handle, attributing GPU errors through debug labels, and documenting preconditions and failure modes. Cheap while the graph is small; a rewrite once errors are shaped like `VkBuffer 0x…`. |

### What had to land before the big P1 capabilities

`#53` is not one thing, and the order mattered. The service injection — the call sites that reached for
`GetRenderDevice()`, `GetRenderResourceManager()` and `GetDescriptorManager()` — **had to come before `#15`
and `#16`**, because those two rewrote the descriptor and transient paths; writing them against the
singletons would have replicated the coupling at scale and then had to be redone, which is the expensive
retrofit the constraints table warns about. It **landed in #85**, which is why `#15` and `#16` were written
against `RenderGraphServices` now. The rest of `#53` (public/private header split, standalone build,
packaging, client-supplied shader source) does not gate the P1 capabilities and can follow or run in
parallel — it gates the library track, which is where its two Consumers.md rows sit.

The reason to say it rather than leave it implied: "L overlaps P1 and constrains it" is a direction, not
an order, and the difference is one doubled workstream.

### Distance to a shippable library

Measured, not estimated:

- **`muyo_rg` is already a separate target, but a library in name only.** It now declares a dependency -
  `target_link_libraries(muyo_rg PUBLIC muyo)` - but that is the engine, not a standalone set of
  dependencies, and it still compiles inside the engine's include scope.
- **9 of its files include at least one header from outside `src/RenderGraph/`**, largest in
  `RenderGraphBuilder.cpp` (`Debug.h`, `PipelineStateBuilder.h`, `ShaderReflectionFetcher.h`,
  `VkExtFuncsLoader.h`), `RenderGraphBuilder.h` (`DescriptorHeapService.h`, `PerObjResourceManager.h`,
  `ShaderAsset.h`) and `RenderGraphNodeContext.h` (3). The scan is in `Roadmap-Visual.md` section 7.
- **1 code call site reaches for an engine singleton** rather than receiving it:
  `GetRenderResourceManager()` in the transitional `RenderGraphBuilder(VkRenderDevice*)` delegation. The
  services constructor (#85) is the path the consumer uses; the remaining work is making the transitional
  form unnecessary.

None of that blocks the first consumer; all of it blocks a second one.

And the boundary is not self-assessed: [`Consumers.md`](Consumers.md) states which consumers exist and
which guarantee each relies on, `holds` or `gap`, with the gap naming its issue. That is the checkable
form of everything in this section — a guarantee with no consumer behind it is a guess, and a consumer
with no guarantee named is a hidden dependency.

## Phases

Each phase has a **gate** — the observable that must hold before the next phase starts. The gate, and
the issues in the phase, live on the milestone; this section is only about why the phases are ordered
the way they are.

### Why this order

**P0 before P1** because frame synchronization is the thing the graph is *for*. Until it owns acquire,
present and images-in-flight, moving the application onto it would replace one hand-rolled frame sync
with another, and the migration would buy nothing that could be measured. P0 also supplies the
`signalFence` deferred in A1.3b, which is what a present path needs.

**P1 before P2** because the graph must express what the legacy passes do before the loop can move:
clears and resolves (`#18`, landed), descriptor heaps (B0–B2, landed; #15 closed as superseded),
transient memory (`#16` landed; B2 added the per-execution arenas, #40), pipeline caching (`#17`,
landed). These are capabilities, not polish — the frame loop cannot be ported around their absence.

**P2 before P3** because a deletion that requires behaviour changes is not a deletion. Everything that
still references `RenderPassManager` from outside `src/RenderPasses/` has to go first — which is why the
`DebugUI` coupling and `ShadowPassManager` are P2 work rather than P3 cleanup.

**Where this can go wrong** is P1 growing without P2 starting, because each missing capability is a
plausible reason to wait. The mitigation is in P2's gate itself: it is capability, not completeness.

**P1's cost criterion is not a gate yet, and says so.** It originally read "cost in the same range as the
legacy path", which cannot fail: no scene, resolution, metric, sample count, device or tolerance was
named. Correctness is the part that can be evaluated now, so it is the gate; the cost half is owned by
`#59`, which has to establish the method and a tolerance before P1 can close. Recording it with its
trigger, the same way the C-ABI decision is, rather than leaving a criterion that only looks like one.

### Cross-cutting track B — descriptor-heap bindless

The design in [DescriptorHeap-Bindless-Design.md](DescriptorHeap-Bindless-Design.md) merged in #211
(`80465ff`) and is the accepted direction: no descriptor set layouts/sets/pools or pipeline layouts,
one resource heap and one sampler heap, direct heap access, host-owned push/root data, per-frame `Build()`.
It is a **track**, not a phase; it ran inside P1 and gated the final `#20` port. B0–B2 all landed.

| Slice | What it settles | Gate |
| --- | --- | --- |
| B0 (#213) | Device features, heap properties, function pointers, standalone probe, AS/RT decision | Same `.heap.spv` on RADV, RTX 3090 and a qualifying llvmpipe; no `Binding`; heaps/descriptors/push data work |
| B1a | `DescriptorHeapService`, typed regions, static + per-execution rings, sampler lifecycle | Descriptors read through host push data with no descriptor set/layout/pipeline layout |
| B1b | Graph bind: `PushData`, heap index resolver, null-layout pipelines, remove descriptor-set code | Graph suite green; no descriptor-set/pipeline-layout calls in `src/RenderGraph/`; UI renders |
| B2 | Per-frame build, descriptor/root rings, in-flight above 1, #17/#209/#40/#83 | **Met** — resize/swapchain build per frame; #209/#17 landed; #40's overlap test and #83's per-execution bindings closed; #59 follows the final #20 |
| B3/B4 | Unified layouts deferred; GPU-written root optional | No work now |

#212 tracks B0–B4 and closed when B2 landed. B3 is deferred and driver-gated; B4 optional; if either is
scheduled it gets its own issue rather than reopening the tracker. The #20 port freeze ended with B2; the
#111 UI pass remains accepted double-conversion debt. #15 closed as superseded (the graph no longer has
descriptor sets to retain). #16 and #12 remain closed; their remaining work belonged to B2/B3, not to a
reopened issue. GPU-driven execution, DGC, meshlets and work graphs remain host concerns: no G track.

## Technical debt policy

The rule this roadmap is asked to enforce: **fix debt as early as possible, unless a planned feature
already replaces it.** The verdict per issue lives in the project's `Debt` field; what follows is the
reasoning behind the three verdicts, which no field holds. The second half matters as much as the first — the migration deletes a large
amount of code, and work spent polishing what is about to be removed is worse than leaving it.

### Fix early — not subsumed by anything below

| Item | Why now |
| --- | --- |
| **#36** `CMAKE_CXX_FLAGS` assigned after `project()` | A build flag that silently does not apply is a class of confusion on its own, and it costs one commit. |
| **#46** finish the assert audit | The class already produced a real bug (#42: an `assert`-gated check that could not fire, leaving a descriptor unwritten). Debt that already bit once. |
| **#47** swapchain four-image assumption and mis-named images | A live out-of-bounds index plus three wrong resource names. **And the proper fix — a container sized from the actual image count — is what A1.5 needs anyway**, so it pays into P0 instead of being thrown away. |
| **TODO hygiene** | Of the `TODO` markers under `src/`, one names what will handle it (`ResourceBarrier.cpp:64`, `TODO(A2)`) and the rest name nothing. Count them with `grep -rn 'TODO' src/ --include=*.cpp --include=*.h` — deliberately not quoted here, because the count moved twice while this document was being reviewed and a number is the part that rots. One is worse than a marker: `assert("TODO: update node date")` in `Scene.cpp` asserts **nothing** (a string literal is always non-null), so it reads like a check. AGENTS.md requires a marker to name what will handle it; without that, deferred work is invisible, which is the failure this document exists to avoid. |
| **#30**, **#35** | Landed in #45: the startup, device and validation error paths that `NDEBUG` removed. `#30` is closed; its remainder is `#46`. |

### Do not fix — a planned feature replaces it

| Debt | Replaced by | Note |
| --- | --- | --- |
| Every `TODO` under `src/RenderPasses/` (`RenderPassManager.h:24`, `.cpp:158,173`, `RenderPassParameters.cpp:321`, `RenderLayerIBL.h:82`) | **P3 deletion** | Do not file them, do not fix them. |
| `helloVulkan.cpp:347` "resizing doesn't work" | **A1.5 FrameSync** | Swapchain recreation with images in flight *is* the fix for resize. |
| Legacy frame-sync bugs (`m_imageAvailable` reused every frame, per-image fences) | **A1.5 FrameSync** | Fixing them in place is work on code P2 removes. |
| `Swapchain.h` `m_swapchainImageViews  // todo: remove this` | **A1.5** | Image bookkeeping is FrameSync's subject. |
| `Texture.h:50` "move this barrier out of the function" | **Resolved by B2 / #40** | The per-execution arena landed in B2 (#254); #16 and #40 are closed. |
| `ResourceBarrier.cpp:64` `TODO(A2)` | **#12 closed; B3 if unified layouts are scheduled** | The stage/access work landed; do not reopen #12. If B3 is scheduled it gets its own issue. |
| The three `PipelineStateBuilder.h` markers (finish other builders, add more builders, create setters) | **#17 A5** | Pipeline and shader caching is where that builder set is either finished or shown to be unnecessary. This is why `#17` carries `Debt: Subsumes debt` — a field verdict needs an explanation here, the same way a table verdict needed a field. |
| `RenderPassParameters` `assert(false)` on an unhandled descriptor type | **P3 deletion** | Decide in #46 as "leave it, deleted by P3" unless it is reachable before then. |

### Not on the parity path — schedule after P3

| Item | Why it can wait |
| --- | --- |
| **Copy queue** (`QueueType::COPY` resolves to the graphics queue; `VkRenderDevice.h:59` `// TODO: Handle copy queue`) | The legacy path uses the graphics queue for every transfer, so a real transfer queue is a new capability, not parity. Unfiled today; file it when it is scheduled rather than carrying it. |
| `ShaderReflectionFetcher` TODOs (input attachments, decorations, specialization constants) | They concern subpass features the graph does not use — it renders with dynamic rendering. Revisit only if subpasses return. |
| **#25** remaining (`-Wconversion` measurement, scan-build baseline) | Incremental adoption, none of it blocking. `-Wextra` and the warning baseline landed in #193/#194; **#24** (clang-tidy) landed in #205/#207; **#22** (CI) left this table by landing in #91, and AGENTS.md section 4 is now the honest statement of what a green run does and does not prove. |

## Critical path

There are two chains, because there are two things to reach: the library shipping, and the first
consumer adopting it. The project's `Critical path` field marks the union and the **Critical path** view
filters on it — the issues are not listed here, for the reason above.

- **Library** — frame sync (`#11` ✅) → the graph boundary (`#53`) → the device boundary (`#58`) → the
  boundary test (`#54`). Nothing on this chain can slip without moving the goal.
- **First consumer** — transfer and clear usages (`#18` ✅) → the client-side frame helper (`#57` ✅;
  `FrameSync` landed and `helloVulkanRG` uses it) → **B0 (`#213`) → B1a → B1b → B2** (all ✅; B2 is
  `#217`/#254) → the final port (`#20`; increment 1 is the #111 UI pass, accepted double conversion) →
  `#48`/`#49` → the deletion (`#51`). `#59` sits across this chain rather than in it: P1 cannot close
  without its number, so its slippage moves the date even though nothing waits for it.
- **Descriptor fallback** — #15 closed as superseded once B1 landed; it is no longer on any path. #16 and
  #12 are closed. #83 and #40 closed with B2, not after adoption.
- **B track** — B0 (#213) was the go/no-go; B1a/B1b implemented the heap and graph bind; B2 made per-frame
  build and in-flight overlap tractable. All are landed. B3 is deferred and B4 optional.

The union is deliberately broad: it is every issue whose slippage moves an end date, not a shortlist. If
it stops being useful, narrow the field to *what blocks the current phase* rather than widening the
definition of "critical".

**A closed issue clears its mark**, because the field answers whose slippage moves an end date and a
closed item has no slippage left. #11 is the first case: it closed with A1.5 delivered (as the split), and
leaving it marked would have the Critical path view claiming a finished item still decides the schedule.
`#18` and `#68` were cleared the same way, on the same reasoning, when they landed. The B-track and B2
items — `#17`, `#40`, `#83`, `#213`, `#215`, `#216`, `#217` — were cleared on the same rule after
2026-10-28. The two P2/P3 prerequisites `#48` and `#49` were marked Yes instead, because the critical path
diagram has always shown them between the final port and `#51`.

**The field is scoped to this project.** It answers "whose slippage moves *Muyo's* end date", so a
consumer's own schedule does not belong on it: `muyo-llm`'s M1 blocking list is tracked in muyo-llm's
repository, and the issues here that block it are indexed in [`Consumers.md`](Consumers.md). A
consumer-driven gap in the graph's capability is therefore usually `Critical path: No` even when it is
on a consumer's path — the file is the cross-project index, and the field is not. Recording the scoping
rather than leaving it implicit, because the two answers are both defensible and the field would
otherwise be read differently by each session that touched it.

## Risks

1. **The migration is deferred by one more graph feature.** This is the real risk, and the phase gates
   are its mitigation: **P2's gate is capability, not completeness.** A1.5 and #18 have landed, and
   `helloVulkanRG` proves the loop, so the graph and the legacy renderer can coexist during P2. The B
   track was the one deliberate sequencing exception: further #20 ports froze after #111 until B2,
   because converting a pass twice is the retrofit the heap design exists to avoid. **That freeze ended
   when B2 landed (2026-10-28, #254)**, and #17 landed with it; #19 can continue in parallel. `helloVulkan`
   itself is still legacy, which is the remaining half of this risk rather than a hypothetical.
2. **Two renderers means every fix lands twice.** Mitigated by the do-not-fix table above: the way to
   keep the cost bounded is to stop polishing the code being deleted, and to put new behaviour in the
   graph only.
3. **RT parity pulls P2 later.** If ray tracing must be at parity, `#14` and `#19` join the critical
   path; if it need not, the raster path can adopt first and RT follow. **The trigger is a consumer that
   needs RT at parity** — `helloVulkan` renders it today, so the honest answer is "probably yes, not
   yet decided". Recorded with its trigger so `#14`/`#19` can be scheduled rather than re-argued each
   time someone asks.
4. **The library is shaped by its first consumer.** The renderer's needs become the API by default and
   every later client inherits them. Mitigated on two fronts, and no longer hypothetically: A1.5's split
   keeps presentation out of the library, and **a second consumer that is not a renderer now exists** —
   `muyo-llm`, an LLM inference engine built on `muyo_rg`, owned by the same author as a permanent check
   rather than a one-off experiment. [`Consumers.md`](Consumers.md) is that mitigation made checkable:
   the guarantees each consumer relies on, `holds` or `gap`, with each gap naming its issue. The residual
   risk is no longer the absence of a second consumer but two things that file cannot fix itself: the
   table going stale (which its rules place on the change that breaks a guarantee), and the fact that
   **the boundary check now runs in another repository**. muyo-llm compiling against the public surface
   is the strongest evidence the surface is real, and nothing here runs it — the CI job (below) builds
   this repository, not the consumer, so that check is still manual and happens when someone remembers.
   Worth a periodic build of the consumer against `master`, and worth naming as a limitation rather than
   assuming the boundary is continuously verified.
5. **CI now gates the cheap classes, and hardware still gates the rest.** `.github/workflows/ci.yml` runs
   on github-hosted runners against mesa's lavapipe: both configurations build, the reference and the
   formatting are checked, and the suite passes in Debug, Release, both ray-tracing shapes and under the
   sanitizers. It **cannot** gate the two axes that need a driver — a second queue *family* (lavapipe
   reports one family with one queue, so no ownership transfer and no dedicated compute queue) and
   driver-specific behaviour — and the job records both on every run so a green check states its own
   scope. The changed lines are also clang-tidy clean (#207), a job of its own. **Verification is therefore still manual where it matters most, which is not the same as
   single-vendor:** the project has two machines on different vendors (RADV REMBRANDT and an RTX 3090,
   both with a dedicated compute family), and the runbook in AGENTS.md section 6 already treats a
   hardware re-run as part of review. **The two-machine matrix is the standard for anything touching
   present or images-in-flight** — P0's client half and all of P2 — because that is where a
   single-vendor, single-topology check is weakest and where the test suite is thinnest. For everything
   else one machine is enough.
6. **The B track is a hard platform break, gated by B0.** It requires Vulkan 1.4 plus
   `VK_EXT_descriptor_heap` and `VK_KHR_shader_untyped_pointers` at device creation, and it replaces the
   descriptor path rather than adding a fallback. B0 (`#213`) was the go/no-go: driver features, heap
   properties, descriptor writes, push data, AS sizing and RT null-layout acceptance. It passed on the
   recorded devices, with #223/#238 recording the RTX 3090 RT gaps rather than hiding them. llvmpipe
   support is Mesa-version dependent (Mesa 26.2.5 exposes both; an LLVM 20.1.2 build exposes neither), so
   CI records the device/Mesa/loader/features rather than treating a lavapipe run as a heap gate. B2
   landed (#254); the #111 UI pass remains accepted double conversion. If a target lacks the heap
   features, the path records the unsupported configuration rather than silently falling back to
   descriptor sets.

## How the GitHub half is set up

**Six milestones**, each carrying its gate in its description: the four phases
(`P0 Frame synchronization` … `P3 Deprecation`), the library track (`L Library extraction`), and
`Backlog (after P3)` for what this document deliberately defers. The two extra ones exist because the plan
has two halves — P0–P3 is the *first consumer*, L is the *library itself* — and because "after P3" is a
verdict rather than an absence of one. The B descriptor-heap track is cross-cutting inside P1 rather than
a seventh milestone; the B issues carry the P1 milestone, and the tracking issue #212 closed when B2
landed.

**Four custom fields:** `Area` (single-select), `Depends on` (text, sparse), `Critical path`
(single-select), and `Debt` (`Fix early` / `Subsumes debt` / `After P3`). The rest are GitHub's
built-ins.

A **visualized snapshot** of the phases, the critical path and the open issues, together with the
reconciliation record that produced it, is [`Roadmap-Visual.md`](Roadmap-Visual.md). It is a view, not a
source: the fields and milestones still own the state.

**Eight saved views:** `Roadmap (by phase)`, `Critical path`, one per phase (`P0 - Frame synchronization`
as a board, `P1 - Graph parity`, `Adoption (P2 + P3)`), one for the library track
(`L Library extraction`), and two for the debt verdicts (`Debt - fix early`, `After P3`).

The project README states the goal and points here for the reasoning.

Three API limits worth knowing before editing any of this by script:

- **Grouping and sorting are UI-only.** `ProjectV2View.groupByFields` and `sortByFields` are readable but
  not settable — `ProjectV2ViewConfigurationInput` accepts only `visibleFieldIds`. A script can create a
  view, name it, set its layout and filter, and still not finish it. **Group `Roadmap (by phase)` by
  `Milestone`** by hand.
- **Filters are not validated, and a field name must be the exact slug.** `updateProjectV2View` accepts
  `nosuchfield:xyz` without complaint, so acceptance is not evidence. `critical-path:Yes` matches the
  intended items and `"Critical path":Yes` matches **nothing**. Which is also a warning about this
  paragraph: it carried a hard-coded count, and the count went stale the moment the library chain joined
  the critical path. Run the command; a number in a document is not evidence either. A typo therefore shows an empty view and
  reports no error, so check a filter's count first:

  ```bash
  gh api graphql -f query='query($p:ID!,$q:String){ node(id:$p){ ... on ProjectV2 {
    items(first:100,query:$q){ totalCount } } } }' \
    -f p=PVT_kwHOACw01s4BmAfF -f q='critical-path:Yes' --jq '.data.node.items.totalCount'
  ```
- **Projects are not in git.** Field and milestone edits have no diff and no review, which is the
  reason a decision whose *rationale* matters belongs in this document instead. Moving an issue between
  milestones does not need a PR; changing a gate should be considered a decision worth recording here.

**`Area` classifies where a change is, not why.** A consumer-driven gap in pipeline compilation is
`Graph API`; the consumer is named in the issue body and in `Consumers.md`. `Adoption` is reserved for a
rendering pipeline adopting the graph — the P2 migration — rather than for library-boundary work that
happens to be consumer-driven, because the same change for a renderer reason would otherwise land in a
different area than for a consumer reason. Each option carries its own description on the board, so the
vocabulary is visible where it is used.

A **roadmap-layout** view is deliberately not created: that layout positions items by a date or
iteration field, and this plan is gate-based. Adding dates to populate a chart would mean inventing a
schedule. If a timeline is wanted, it needs two date fields and a stated target per phase — a commitment
to make on purpose, not as a side effect of choosing a layout.

## Maintaining this document

When a phase's gate changes, change it here in the same PR. When an issue moves between phases, move
its milestone **and** revisit its `Critical path` value — do not leave the three layers disagreeing,
which is the doc-versus-reality drift AGENTS.md section 5 already has a rule about.
