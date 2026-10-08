# Roadmap: the RenderGraph replaces RenderPassManager

## Goal

`RenderGraph` is the only renderer. `src/RenderPasses/` and `RenderPassManager` are deleted, and every
frame in the application — including `helloVulkan` — is built from graph nodes.

**Definition of done:**

```bash
grep -rn "RenderPassManager" src/          # expect: no matches
ls src/RenderPasses/ 2>/dev/null           # expect: does not exist
```

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

## Phases

Each phase has a **gate**: the observable that must hold before the next phase starts. The gate is the
point; the issue list is how it gets there.

### P0 — The graph owns frame synchronization

Issue: **#11 (A1)**. Done: graph core (#9), sync2 migration (#10), executor and in-flight slots
(A1.1–A1.3b). Remaining: **A1.4** per-queue timeline semaphores — which also supplies the
caller-facing `signalFence` deliberately deferred in A1.3b — and **A1.5** `FrameSync`
(`NUM_BUFFERS` slots, per-frame acquire/render-finished binaries, images-in-flight map, present ids).

Rider: **#39** (reuse slot command buffers; the pools already have `RESET_COMMAND_BUFFER_BIT`).

**Gate:** a windowed frame loop presents through the graph with several frames in flight, and the
application holds no semaphore, fence or image index.

### P1 — The graph can express the legacy frame

The legacy path clears, resolves, binds materials at scene scale, caches pipelines and renders a
shadow atlas. The graph must do all of it, at comparable cost, before the application can move.

| Issue | Why it gates |
| --- | --- |
| **#18 A6** transfer and clear usages | The legacy passes clear attachments and resolve; the graph's node API must express that rather than the pass doing it by hand. |
| **#16 A3** transient lifetime and aliasing | Memory, and the prerequisite for more than one execution in flight. |
| **#15 A4** content-addressed descriptor sets | The MATERIAL pool cliff: per-node sets are correct but do not survive a real scene. |
| **#17 A5** pipeline and shader caching | Per-frame cost parity. |
| **#13 C8** `SetData` realloc invalidates descriptors | Correctness at the boundary the graph leans on. |
| **#12 A2** precise barrier masks | Today's masks are conservative *and correct*; this is the performance half of parity. |
| **#14 C9**, **#19 A7** RT barriers and hardening | Gate the ray-tracing path specifically; the rest of P1 does not depend on them. |
| **#40** in-flight count above 1 | Depends on #15 and #16; real overlap otherwise aliases descriptor state and transients. |

**Gate:** a scene renders through the graph with validated correctness and cost in the same range as
the legacy path. Ray tracing is a separate gate on the same phase.

### P2 — The application runs on the graph

Issue: **#20** — port the render loop (`src/app/helloVulkan.cpp`) to graph nodes. Two dependencies that
are not part of #20 today and must be, or the port stalls halfway:

- **DebugUI is coupled to `RenderPassManager`** — viewport size, camera, `ReloadEnvironmentMap`,
  and `GetRenderPasses()` for the debug pass list. It needs a frame/renderer interface, not a
  `RenderPassManager*`.
- **`ShadowPassManager` is a second frame manager** — it owns N `RenderPassRSM` passes and its own
  command buffers, and must become graph nodes before `src/RenderPasses/` can be deleted.

**Gate:** `helloVulkan` renders through the graph, and `RenderPassManager` is referenced only by its
own files and the not-yet-deleted passes.

### P3 — Deprecation

Delete `RenderPassManager`, `src/RenderPasses/`, the `DebugUI` coupling and the `ShadowPassManager`.
This is a deletion of ~4,400 lines and should be a PR whose diff is almost entirely negative — if it
needs behaviour changes, P2 was not finished.

**Gate:** the definition of done at the top of this document.

## Technical debt policy

The rule this roadmap is asked to enforce: **fix debt as early as possible, unless a planned feature
already replaces it.** The second half matters as much as the first — the migration deletes a large
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

```
A1.4 ──▶ A1.5 ─────────────────────┐
                                   ├──▶ #20 (P2) ──▶ P3 deletion
#18 ─┬─▶ #15 ─┬─▶ #40              │
     │        │                    │
     ├─▶ #16 ─┘                    │
     ├─▶ #17 ──────────────────────┘
     └─▶ #12, #13
#14, #19 ─────────▶ (RT parity gate, parallel)
```

The path that matters is **A1.4 → A1.5 → #20**. Everything in P1 can proceed in parallel with P0
except `#40`, which needs `#15` and `#16`.

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
4. **No CI (#22), so the migration is verified on one machine.** The migration touches the frame loop
   and swapchain, which is where a single-vendor, single-topology check is weakest — and where the
   test suite is thinnest. Anything touching present or images-in-flight needs the reviewer's hardware
   as well as the developer's.

## How this is reflected in GitHub

The plan lives in three places, and they are meant to say the same thing:

| Layer | Holds |
| --- | --- |
| This document | The phases, their gates, the debt policy, the risks. The reasoning. |
| **Milestones** | The phases, as a grouping every issue belongs to. `P0 Frame synchronization`, `P1 Graph parity`, `P2 App adoption`, `P3 Deprecation`. |
| **Project "Muyo RenderGraph"** | A view over the above, plus the one thing a milestone cannot carry: the `Critical path` field. |

Project fields and views, all created through the API except where noted:

- **`Critical path`** (single-select `Yes`/`No`) — the issues on the path that decides when P2 can
  start: `#11` (A1.4, A1.5), `#18`, `#15`, `#16`, `#20`, `#51`. Everything else is `No`.
- **`Depends on`** (text) — sparse, used where an issue is genuinely blocked (`#40`).
- Views: **Roadmap (by phase)** (all items), **Critical path**, **P0 - Frame synchronization**
  (board), **Debt - fix early**, **P1 - Graph parity**, **Adoption (P2 + P3)**.
- The project **README** carries the summary above, so the project page answers "what is the plan"
  without a link-click.
- The project **short description** states the goal in one line.

Two API limits worth knowing before editing views by script:

- **Grouping and sorting are UI-only.** `ProjectV2View.groupByFields` and `sortByFields` are readable
  but not settable — `ProjectV2ViewConfigurationInput` accepts only `visibleFieldIds`. So "group by
  Milestone" is one click per view, and a script cannot finish the job.
- **Filters are not validated, and the field name must be the slug.** `updateProjectV2View` accepts
  any string, including `nosuchfield:xyz`. A filter referring to a field that is not an exact slug
  (lower-case, spaces to hyphens — `critical-path:Yes`, not `"Critical path":Yes`) matches **nothing**
  rather than everything, so a typo shows an empty view and reports no error. Check a filter's count
  before trusting it:

  ```bash
  gh api graphql -f query='query($p:ID!,$q:String){ node(id:$p){ ... on ProjectV2 {
    items(first:100,query:$q){ totalCount } } } }' \
    -f p=PVT_kwHOACw01s4BmAfF -f q='critical-path:Yes' --jq '.data.node.items.totalCount'
  ```

A **roadmap-layout** view is deliberately not created. The layout positions items by a date or
iteration field, and this plan is gate-based rather than dated — adding dates would mean inventing a
schedule to make a chart look populated. If a timeline is wanted, it needs two date fields and a
stated target per phase, which is a commitment to make on purpose rather than as a side effect of
choosing a layout.

## Maintaining this document

When a phase's gate changes, change it here in the same PR. When an issue moves between phases, move
its milestone **and** revisit its `Critical path` value — do not leave the three layers disagreeing,
which is the doc-versus-reality drift AGENTS.md section 5 already has a rule about.
