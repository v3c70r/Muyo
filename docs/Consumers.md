# Consumers

The RenderGraph is a library, and a library is only as good as the boundary its second consumer
finds. This file names the consumers, states the guarantees each one relies on, and points at the
issue that owns each gap — so "library boundary" is a checkable list rather than a slogan.

It is deliberately short. A guarantee with no consumer behind it is a guess; a consumer with no
guarantee named is a hidden dependency waiting to be broken.

## Consumers

| Consumer | Kind | Where | What it exercises |
| --- | --- | --- | --- |
| `helloVulkan` | renderer, first consumer | this repository | The full graphics pipeline. The migration to the graph is P2; until then it still renders through `RenderPassManager`. |
| in-tree tests | test suite | `src/tests/` | Every graph guarantee, in isolation, on one device at a time. |
| **muyo-llm** | **first-party architecture validation** | <https://github.com/v3c70r/muyo-llm> | A compute-only, windowless, long-running LLM decode workload. |

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
| Completion is expressible as a timeline value and chainable across executions | muyo-llm | holds | — |
| Long-lived *imported* resources (weights, a KV cache) survive across executions | muyo-llm | holds | — |
| Graph-managed transients have a defined lifetime and aliasing policy | muyo-llm | **gap** | #16 |
| Device, allocator and descriptor allocation are injected, not global | muyo-llm | **gap** | #53 |
| A stated public surface (what is API and what is detail) | all | **gap** | #53 |
| Specialization constants are applied at pipeline creation | muyo-llm | holds | — |
| Cooperative matrix can be enabled at device creation | muyo-llm | holds | — |
| Cooperative matrix property sets can be read, to pick a kernel shape | muyo-llm | **gap** | #72 |
| A client can supply SPIR-V bytes / a shader search path, not only a file name | muyo-llm | **gap** | #53 |
| Declaration errors fail at `Build()`, naming the graph entity | muyo-llm | **gap** | #55 |

## Rules for consumers and for changes

- **A change that would break a guarantee names the consumer and updates its row in the same PR.**
  The row is the contract; leaving it stale is the drift `AGENTS.md` §5 already has a rule about.
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
