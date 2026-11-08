# Consumers

The RenderGraph is a library, and a library is only as good as the boundary its second consumer
finds. This file names the consumers, states the guarantees each one relies on, and points at the
issue that owns each gap — so "library boundary" is a checkable list rather than a slogan.

It is deliberately short. A guarantee with no consumer behind it is a guess; a consumer with no
guarantee named is a hidden dependency waiting to be broken.

This file describes **version `0.1.0`** of the library (`MUYO_RG_VERSION`; the macros are in the
generated `<muyo_rg/version.h>`, the documented constants in `RenderGraph/MuyoVersion.h`). A row is a
claim about that version and no other.

## Consumers

| Consumer | Kind | Where | What it exercises |
| --- | --- | --- | --- |
| `helloVulkan` | renderer, first consumer | this repository | The full graphics pipeline. `FrameSync` owns acquire/present and the `GBUFFER`, direct `LIGHTING`, `UI` and `FINAL` passes are graph nodes over graph-owned transients (#20); the legacy renderer and `src/RenderPasses/` are deleted (#51), with IBL/RSM/RT dropped rather than ported. |
| in-tree tests | test suite | `src/tests/` | Every graph guarantee, in isolation, on one device at a time. |
| **muyo-llm** | **first-party architecture validation** | <https://github.com/v3c70r/muyo-llm> | A compute-only, windowless, long-running LLM decode workload. |
| **muyo-renderer** | **host + render pipeline, future consumer** | separate repository (planned) | The window/swapchain/present host and the GBuffer/lighting/UI/FINAL pipeline, built on the graph the way `muyo-llm` is — the split `#272` unblocks. |

### muyo-llm — first-party architecture validation

muyo-llm is a lightweight LLM inference engine built directly on `muyo_rg`. It is the
second consumer the roadmap asks for, and the first that does not look like a renderer: no window,
no swapchain, no colour attachment, no frame — just a deep chain of compute dispatches per token,
repeated for the life of the process over resources (weights, KV cache) that outlive any one
execution.

It is called *first-party* because it is owned by the same author and is intended to be a
permanent check on the library, not a one-off experiment. Its plan and findings live in its own
repository; gaps it finds are filed here as issues and linked below. The roadmap's own reasoning
for wanting this (`docs/Roadmap.md`, Risk #4) is that "a second consumer that is *not* a renderer
is the only real test of the boundary".

## Guarantees

Each row is a promise a consumer depends on. `Holds` means a test or an existing consumer proves it
today, and carries **no** tracking issue — citing an open one would say the opposite. A gap has a
tracking issue and, where it blocks a consumer, is marked.

| Guarantee | Consumer | State | Tracking |
| --- | --- | --- | --- |
| Compute-only profile: no surface, present or colour attachment required on the execution path | muyo-llm | holds | — |
| Linking `muyo_device` enables no validation layer or debug messenger and needs no engine link; diagnostics are the host's opt-in | muyo-llm, muyo-renderer | holds | — |
| Completion is expressible as a timeline value and chainable across executions | muyo-llm | holds | — |
| Long-lived *imported* resources (weights, a KV cache) survive across executions | muyo-llm | holds | — |
| An imported image's caller-stated layout is the first barrier's source, so its content is not discarded | helloVulkan | holds | — |
| A node can bind a byte range of a buffer, so one arena serves many tensors | muyo-llm | holds | — |
| Graph-managed transients have a defined lifetime and aliasing policy | muyo-llm | holds | — |
| Device, allocator and descriptor allocation are injected, not global | muyo-llm | holds | — |
| The graph's resource dependency is a narrow interface it owns — no engine resource types or global lookups in its public headers | muyo-renderer, muyo-llm | holds | — (landed in #272) |
| A stated public surface (what is API and what is detail) | all | holds | — (listed in `docs/RenderGraph.md`, checked by `render_graph_docs.py`) |
| Specialization constants are applied at pipeline creation | muyo-llm | holds | — |
| Cooperative matrix can be enabled at device creation | muyo-llm | holds | — |
| Cooperative matrix property sets can be read, to pick a kernel shape | muyo-llm | **gap** | #72 |
| A client can supply SPIR-V bytes / a shader search path, not only a file name | muyo-llm | **gap** | #292 |
| Declaration errors fail at `Build()`, naming the graph entity | muyo-llm | holds | — |
| Declaration and configuration errors throw a standard exception a caller can catch, and never abort; a failed Vulkan call aborts with its result and call site | muyo-llm, muyo-renderer | holds | — |

## Rules for consumers and for changes

- **A change that would break a guarantee names the guarantee and updates its row in the same PR, and is
  listed under `Breaking` in the changelog.** Naming the *guarantee* rather than every consumer is what
  keeps this file an inventory instead of a registry: it scales by promise, and a third party finds the
  change through the version and the changelog (`docs/RenderGraph-Library-Design.md`, "Versioning the
  library"). The row is the contract; leaving it stale is the drift `AGENTS.md` §5 has a rule about.
- **First-party consumers are named as well.** `muyo-llm`, and `helloVulkan` as it adopts the graph, are
  how the library's coverage is measured rather than only who consumes it, so a change that breaks one
  names it and does not merge with that upgrade unplanned.
- **Rows describe the current version, `0.1.0`.** Only the current version is supported, so a first-party
  consumer that has not upgraded leaves its rows stale - and a stale row is not evidence about the library.
  The version a row is written against is read from `MUYO_RG_VERSION` (`<muyo_rg/version.h>`, or the
  constants in `RenderGraph/MuyoVersion.h`); a change that moves it updates the referent above in the same
  PR.
- **A new consumer adds its row here before it adds code.** The row states which guarantees it
  relies on, so a gap is visible while it is cheap to fix.
- **A consumer-driven change follows the normal process.** File an issue first if the roadmap does not
  already cover the gap, then a branch, then a squash PR with a test that has teeth. The issue must name
  the consumer and the use case, not "a library user". **The area is where the change is, not why it
  exists:** a gap in pipeline compilation is `Graph API` and one in the device layer is `Device`.
  `Adoption` is reserved for a rendering pipeline adopting the graph — the P2 migration — so the same
  change does not land in a different area depending on who asked for it (`docs/Roadmap.md`, "How the
  GitHub half is set up").
- **The renderer is not regressed for a consumer.** Both test configurations stay green; a change
  that only makes sense for a non-renderer belongs behind a capability or injected service, not in
  the renderer's path.
