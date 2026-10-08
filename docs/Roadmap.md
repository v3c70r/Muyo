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

The debt tables below are the exception that proves the rule: they cite issue numbers because they
record a **verdict that no field holds** — including for work that was never filed as an issue at all
(the subsumed `TODO`s, which live in code). If a fact can be a field, make it a field.

## Where we are

| | Lines | State |
| --- | --- | --- |
| `src/RenderGraph/` | 3,154 | Drives the **tests only**. Foundation, sync2, executor in flight. |
| `src/RenderPasses/` | 4,403 | What actually renders: 11 passes plus `ShadowPassManager`. |
| `src/app/helloVulkan.cpp` | 374 | Owns the frame loop and hand-rolled semaphores. |

The single fact that matters: **the graph has zero production adoption.** Every guarantee it makes is
exercised by the test suite and by nothing else, and `RenderPassManager` keeps its own frame-sync bugs
in the meantime (`m_imageAvailable` reused every frame, per-swapchain-image fences). That asymmetry is
the risk this roadmap exists to close — see Risks.

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

| Constraint | Why it is expensive to retrofit |
| --- | --- |
| **No frame boundary in the library** | An inference client submits work that outlives many display frames. `Execute()` draining at frame end is a convenience to keep, not the model: completion has to be expressible as a timeline value the client can chain across frames. |
| **Device, allocator and descriptor allocation injected, not global** | There are 15 call sites inside `src/RenderGraph/` that reach for `GetRenderDevice()` and friends. A library cannot own the engine's singletons. |
| **Compute-only is a first-class profile** | No surface, no present, possibly no colour attachments. Nothing on the execution path may require a graphics family or a swapchain. |
| **Long-lived resources are first-class** | Weights and a KV cache live for the process, not a frame. The transient/persistent split (`#15`, `#16`) has to serve that, not only per-frame render targets. |
| **A stated public surface** | Every header in `src/RenderGraph/` is public today, reflection and descriptor internals included. Deciding what is API and what is detail is a prerequisite for shipping, and cheapest while there is one consumer. |
| **Diagnostics an agent can act on** | The library's consumers include coding agents, which read a log with no debugger and no follow-up question. That means failing at `Build()` on declaration errors, naming the graph node and resource rather than the handle, attributing GPU errors through debug labels, and documenting preconditions and failure modes. Cheap while the graph is small; a rewrite once errors are shaped like `VkBuffer 0x…`. |

### Distance to a shippable library

Measured, not estimated:

- **`muyo_rg` is already a separate target, but a library in name only.** It declares no dependencies of
  its own — no `target_link_libraries`, VMA as a private include directory — and compiles inside the
  engine's include scope.
- **13 of its files include engine headers** from outside `src/RenderGraph/`, concentrated in
  `RenderGraphBuilder.h` (12 of them: `MeshResourceManager`, `PerObjResourceManager`, `ShaderAsset`,
  `PSODesc`, …).
- **15 call sites reach for engine singletons** rather than receiving them.

None of that blocks the first consumer; all of it blocks a second one.

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
clears and resolves (`#18`), material binding at scene scale (`#15`), transient memory (`#16`), pipeline
caching (`#17`). These are capabilities, not polish — the frame loop cannot be ported around their
absence.

**P2 before P3** because a deletion that requires behaviour changes is not a deletion. Everything that
still references `RenderPassManager` from outside `src/RenderPasses/` has to go first — which is why the
`DebugUI` coupling and `ShadowPassManager` are P2 work rather than P3 cleanup.

**Where this can go wrong** is P1 growing without P2 starting, because each missing capability is a
plausible reason to wait. The mitigation is in P2's gate itself: it is capability, not completeness.

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
| **TODO hygiene** | 24 markers, and exactly one names an issue. AGENTS.md requires a marker to name what will handle it; without that, deferred work is invisible, which is the failure this whole document is trying to avoid. |
| **#30**, **#35** | In flight (PR #45): the startup and error-path checks that `NDEBUG` removed. |

### Do not fix — a planned feature replaces it

| Debt | Replaced by | Note |
| --- | --- | --- |
| Every `TODO` under `src/RenderPasses/` (`RenderPassManager.h:24`, `.cpp:158,173`, `RenderPassParameters.cpp:321`, `RenderLayerIBL.h:82`) | **P3 deletion** | Do not file them, do not fix them. |
| `helloVulkan.cpp:347` "resizing doesn't work" | **A1.5 FrameSync** | Swapchain recreation with images in flight *is* the fix for resize. |
| Legacy frame-sync bugs (`m_imageAvailable` reused every frame, per-image fences) | **A1.5 FrameSync** | Fixing them in place is work on code P2 removes. |
| `Swapchain.h` `m_swapchainImageViews  // todo: remove this` | **A1.5** | Image bookkeeping is FrameSync's subject. |
| `Texture.h:50` "move this barrier out of the function" | **#16 A3 / #18 A6** | Barrier placement is exactly what A6 and A3 own. |
| `ResourceBarrier.cpp:64` `TODO(A2)` | **#12** | Already names its issue — the one marker that does. |
| `RenderPassParameters` `assert(false)` on an unhandled descriptor type | **P3 deletion** | Decide in #46 as "leave it, deleted by P3" unless it is reachable before then. |

### Not on the parity path — schedule after P3

| Item | Why it can wait |
| --- | --- |
| **Copy queue** (`QueueType::COPY` resolves to the graphics queue; `VkRenderDevice.h:59` `// TODO: Handle copy queue`) | The legacy path uses the graphics queue for every transfer, so a real transfer queue is a new capability, not parity. Unfiled today; file it when it is scheduled rather than carrying it. |
| `ShaderReflectionFetcher` TODOs (input attachments, decorations, specialization constants) | They concern subpass features the graph does not use — it renders with dynamic rendering. Revisit only if subpasses return. |
| **#24** clang-tidy, **#25** `-Wextra`, **#22** CI | Incremental adoption, none of it blocking. Adopt each scoped to changed lines as AGENTS.md section 5 describes. |

## Critical path

There are two chains, because there are two things to reach: the library shipping, and the first
consumer adopting it. The project's `Critical path` field marks the union and the **Critical path** view
filters on it — the issues are not listed here, for the reason above.

- **Library** — frame sync (`#11`) → the graph boundary (`#53`) → the device boundary (`#58`) → the
  boundary test (`#54`). Nothing on this chain can slip without moving the goal.
- **First consumer** — transfer and clear usages (`#18`) → descriptors (`#15`) and transients (`#16`) →
  the client-side frame helper (`#57`) → the port (`#20`) → the deletion (`#51`).

The union is deliberately broad: it is every issue whose slippage moves an end date, not a shortlist. If
it stops being useful, narrow the field to *what blocks the current phase* rather than widening the
definition of "critical".

## Risks

1. **The migration is deferred by one more graph feature.** This is the real risk, and the phase gates
   are its mitigation: **P2's gate is capability, not completeness.** Once A1.5 and #18 land, the loop
   can move even while #12, #17 and #19 continue — the graph and the legacy renderer can coexist
   during P2, so "the graph is not finished" is not a reason to keep `RenderPassManager` in the loop.
   If P2 has not started after P0 and #18 are done, that is a signal to start it anyway.
2. **Two renderers means every fix lands twice.** Mitigated by the do-not-fix table above: the way to
   keep the cost bounded is to stop polishing the code being deleted, and to put new behaviour in the
   graph only.
3. **RT parity pulls P2 later.** If ray tracing must be at parity, #14 and #19 join the critical path;
   if it need not, the graph can adopt the raster path first and let RT follow.
4. **The library is shaped by its first consumer, and never extracted.** The renderer's needs would become
   the API by default, and every later client would inherit them. The mitigation is the split above —
   A1.5 is the first concrete step, and the constraints table is the checklist — rather than a promise to
   extract later. A second consumer that is *not* a renderer is the only real test of the boundary, so a
   small compute-only example is worth more than any amount of interface design.
5. **No CI (#22), so the migration is verified on one machine.** The migration touches the frame loop
   and swapchain, which is where a single-vendor, single-topology check is weakest — and where the
   test suite is thinnest. Anything touching present or images-in-flight needs the reviewer's hardware
   as well as the developer's.

## How the GitHub half is set up

Four **milestones** (`P0 Frame synchronization` … `P3 Deprecation`), each carrying its gate in its
description. Two custom fields: **`Critical path`** (single-select) and **`Depends on`** (text, sparse).
Six saved views: `Roadmap (by phase)`, `Critical path`, `P0 - Frame synchronization` (board),
`Debt - fix early`, `P1 - Graph parity`, `Adoption (P2 + P3)`. The project README states the goal and
points here for the reasoning.

Three API limits worth knowing before editing any of this by script:

- **Grouping and sorting are UI-only.** `ProjectV2View.groupByFields` and `sortByFields` are readable but
  not settable — `ProjectV2ViewConfigurationInput` accepts only `visibleFieldIds`. A script can create a
  view, name it, set its layout and filter, and still not finish it. **Group `Roadmap (by phase)` by
  `Milestone`** by hand.
- **Filters are not validated, and a field name must be the exact slug.** `updateProjectV2View` accepts
  `nosuchfield:xyz` without complaint, so acceptance is not evidence. `critical-path:Yes` matches the six
  intended items; `"Critical path":Yes` matches **nothing**. A typo therefore shows an empty view and
  reports no error, so check a filter's count first:

  ```bash
  gh api graphql -f query='query($p:ID!,$q:String){ node(id:$p){ ... on ProjectV2 {
    items(first:100,query:$q){ totalCount } } } }' \
    -f p=PVT_kwHOACw01s4BmAfF -f q='critical-path:Yes' --jq '.data.node.items.totalCount'
  ```
- **Projects are not in git.** Field and milestone edits have no diff and no review, which is the
  reason a decision whose *rationale* matters belongs in this document instead. Moving an issue between
  milestones does not need a PR; changing a gate should be considered a decision worth recording here.

A **roadmap-layout** view is deliberately not created: that layout positions items by a date or
iteration field, and this plan is gate-based. Adding dates to populate a chart would mean inventing a
schedule. If a timeline is wanted, it needs two date fields and a stated target per phase — a commitment
to make on purpose, not as a side effect of choosing a layout.

## Maintaining this document

When a phase's gate changes, change it here in the same PR. When an issue moves between phases, move
its milestone **and** revisit its `Critical path` value — do not leave the three layers disagreeing,
which is the doc-versus-reality drift AGENTS.md section 5 already has a rule about.
