# RenderGraph as a library: target architecture

Companion to [Roadmap.md](Roadmap.md). The roadmap says *when*; this says *what the boundary should be*.
No part of this is implemented — it is the shape to build toward, written down while there is one
consumer so the second one is not what discovers it.

## Decisions, with their triggers

Each of these names what would reopen it, so a session arriving without history can tell a settled
question from an open one instead of re-arguing it. The sections below carry the reasoning; this is the
index.

| Decision | Reopened by | Where |
| --- | --- | --- |
| The public surface is C++ headers; a C ABI is a later wrapper | a consumer that cannot use C++ headers | §1 below |
| `muyo_device` is a shared library, not an inverted dependency | a client that cannot take the shared library | §2 below |
| Error reporting is unresolved, deliberately | deciding it, or a consumer that cannot act on the errors we report | §3 below |
| No C++ modules: fix the paths and add an export set instead | the surface is split (#53) **and** a consumer asks for a module | the packaging section |

## The stack

The proposal was `RenderDevice → RenderGraph → RenderPipeline`. That is right, with three corrections:
a fourth layer above it, a boundary that is per-concern rather than a single line, and one piece of
app policy that has to come out of the device layer.

```
  ┌──────────────────────────────────────────────────────────────┐
  │ host / app        window, swapchain, frame pacing, present,  │
  │                   input, ImGui                           (a) │
  ├──────────────────────────────────────────────────────────────┤
  │ render pipeline   a specific arrangement of nodes:           │
  │                   GBuffer → lighting → transparent → UI       │
  │                   no device calls, no present             (b) │
  ├──────────────────────────────────────────────────────────────┤
  │ muyo_rg           resources and lifetimes, barriers, the     │
  │  (RenderGraph)    execution plan and executor, pipeline and  │
  │                   descriptor compilation                  (c) │
  ├──────────────────────────────────────────────────────────────┤
  │ muyo_device       instance, device, queues, memory, sync     │
  │  (RenderDevice)   primitives, command pools               (d) │
  └──────────────────────────────────────────────────────────────┘
```

**(a) The host is a layer and it owns presentation.** This is the same conclusion as the A1.5 split in
the roadmap: acquire, images-in-flight and present ids belong to whoever owns a window. A test harness,
an offline renderer and the future tool are all hosts too, which is why it is a layer rather than a
detail of the renderer.

**(b) The render pipeline is a consumer of the public API only.** If it cannot be written against the
public surface, the surface is wrong. This is the boundary test the roadmap asks for, and it is
checkable: `helloVulkan`'s passes must not include anything under `src/RenderGraph/` that is not public.

**(c) The graph owns orchestration, not the device.**

**(d) The device layer is Vulkan.** The graph is Vulkan-shaped on purpose — its public API carries
`VkImageLayout`, `VkCommandBuffer`, pipeline bind points — so an abstract RHI would be an abstraction
with one implementation. That is a cost with no benefit until a second backend exists.

## Where the line goes, per concern

The mistake to avoid is drawing one line and calling it "the boundary". Each concern has its own, and
several are currently on the wrong side of it.

| Concern | Owner | Received by injection | Note |
| --- | --- | --- | --- |
| Instance, device, queues, memory, sync primitives, command pools | **device** | — | Already exists; needs a target of its own rather than a header inside the engine. |
| Queue *use* — which segment runs where | **graph** | queue-family facts | The graph plans; it must not *require* a graphics family (compute-only profile). |
| Transient resources, aliasing, lifetimes | **graph** | allocator | The policy is the graph's; the memory is not. |
| Barriers and layouts | **graph** | — | The reason the graph exists. |
| Pipeline creation | **graph** | shader source provider, pipeline cache | It builds pipelines from node descriptions today. Keep that — but a client should be able to supply SPIR-V bytes and a cache, not only a file name on disk. |
| Descriptor sets | **graph** | descriptor allocator | Per #15's content-addressed design. |
| Swapchain, present, frame pacing | **host** | — | Not the graph's, not the pipeline's. |
| Validation-layer enablement | **host** | — | See below; this is currently the device's and should not be. |

## Decisions

### 1. C++ headers now; a C ABI is a later wrapper

Decided: the public surface is C++ headers, and the library is open source. If ABI stability or
cross-language use is needed later, a C ABI can be wrapped *around* it — that is a strictly additive move,
whereas designing for it now would constrain the API for a client that does not exist.

Recorded with its trigger so the decision can be revisited rather than drifted into: wrap a C ABI when
there is a consumer that cannot use C++ headers.

### 2. The device is a shared library, not an inverted dependency

Two ways to get a clean boundary:

- **shared library (chosen)** — `muyo_device` is its own target with a documented surface, and `muyo_rg`
  links it. Both ship. Simple, and honest about the graph being Vulkan-only.
- dependency inversion — the graph declares the device interface it needs and the device implements it.
  More flexible, and pays indirection for a second backend that does not exist.

Choose the shared library, and revisit on the same trigger as the C ABI: a client that cannot use
`muyo_device`. The current problem is not the *direction* of the dependency — it is that there is no
target boundary at all (13 cross-includes, 15 singleton call sites). Fix the boundary, keep the direction.

### 3. Error reporting is unresolved, deliberately

`FatalError` and `FatalAbort` abort the process. That is defensible for a renderer, where every failure
is unrecoverable, and hostile to a long-running inference service that links the library and would rather
lose one request than the process. Options are to return a result from the public entry points, to throw
on the graph-level failures (the graph already throws for declaration mistakes) and keep aborting only
for Vulkan call failures, or to let the host install a handler.

Not decided. Recorded as a design question rather than left implicit, because it is the kind of thing
that gets decided by whoever writes the next call site.

## What the device layer must shed

`VkDebugRenderDevice::Initialize` appends the validation layer and extension **unconditionally**, and
`DebugUtilsMessenger` is created with the instance. A library's client should choose whether it wants a
validation layer, a debug messenger, and their cost — an inference service does not.

So: instance/device/queues/memory stay in the device layer; **validation policy moves to the host**, with
the device offering a debug-messenger hook it can be given. Until then, every consumer of the library
gets validation whether it asked or not.

## Packaging an external consumer, and why not C++ modules yet

How an external application *takes* the library is a separate question from how the library is layered, and
it is the one #68 is about. Measured before deciding.

### What a consumer pays today

One translation unit that includes only `RenderGraph/RenderGraphBuilder.h`, compiled with the flags the
project itself uses. The provenance is part of the figure: these were reproduced on a second machine and
the magnitude held while the counts did not, so an absolute number quoted without its machine is wrong.

| | here — GCC 13.3, system Vulkan headers `VK_HEADER_VERSION 313` | reviewer's RTX 3090 box — SDK 1.4.363 |
| --- | --- | --- |
| transitive headers | **479** unique, **169** from `thirdparty/` | **541** raw / **502** unique, **147** from `thirdparty/` |
| preprocessed lines | **178,394** | **135,153** |
| compile time, trivial TU | **1.8 s** | **0.19 s** |

What both agree on is the claim the numbers exist to support: hundreds of headers, six figures of
preprocessed lines, paid per including translation unit, with `thirdparty/` a large fraction of it. The
variance is header revision, raw-versus-unique counting and machine speed — none of which is the point.

The public header reaches imgui, tinygltf, meshoptimizer, glm, SPIRV-Reflect and the engine's resource
managers. That is the "every header under `src/RenderGraph/` is public today" constraint in numbers: the
surface is not a surface, it is the engine. #53 is the fix, and nothing below replaces it.

### Modules: feasible, and the wrong first move

Feasibility was checked rather than assumed:

| | |
| --- | --- |
| clang 21 | C++20 named modules work end to end — `.cppm` → `.pcm` → link → run |
| g++ 13.3 | **no**; it treats `.cppm` as a linker input, and GCC 14 is the first release with real support |
| g++ 14.2 / 15 | available from apt, so the floor is a decision rather than a constraint |
| `import std` | unavailable — no libc++, and it needs CMake ≥ 3.30 against the 3.28 here |
| generator | Ninja is the supported path for CMake's module scanning; this project uses Unix Makefiles and Ninja is not installed |

None of that is disqualifying on its own. The reasons it is the wrong *first* move are:

1. **Modularising today's surface is modularising the engine.** 479 headers behind one `import muyo_rg;` is
   the same dependency graph with a better front door. Split first (#53), then the module is small enough to
   be worth having.
2. **Module artifacts are compiler-specific, and there is no cross-compiler ABI.** Demonstrated: GCC cannot
   read a `.pcm` clang produced — `failed to read compiled module`. A *prebuilt* module therefore pins every
   consumer to one compiler family and version, which is a harder ask than a header, and the opposite of
   what a library with a second consumer in another project wants. The way out is to ship module *source*
   and have each consumer compile it, which removes the pin but keeps the parse cost — amortised over their
   translation units rather than paid per include, which is still the real win.
3. **It does not address distribution.** #68 is `CMAKE_SOURCE_DIR` resolving to the consumer's root, 28
   paths built from it, and `muyo_rg` declaring no dependencies. Modules change none of that; a consumer
   still cannot configure the project.
4. **The upside is narrower than it looks.** A module would *enforce* the boundary — the compiler rejects
   reaching into internals, where a header split only reduces what is reachable — and that is a genuine
   advantage, the same one #53 wants from a stated surface. But it is a second-order benefit next to
   "the consumer cannot build the thing".

**Decision: fix #68 with paths and declared dependencies, add install/export rules for the packaging case,
and revisit modules with a trigger** — when the public surface has been split (#53) *and* a consumer asks for
one. Recorded the same way as the C-ABI decision, so it can be revisited rather than re-argued.

### What the embed drags across, stated rather than discovered

#68 made embedding work. These are the properties that came with it, because "embeddable" that hides
them is a different promise from the one the probe verified.

**Development targets are guarded; build dependencies are not.** The demo app, the PSO compiler, the test
suite and the docs check are `MUYO_TOP_LEVEL`-only, which is also what keeps the Catch2 fetch — a network
dependency on a framework the consumer will never build — out of an embedder's configure. Two things stay
deliberately: VMA's `FetchContent`, because it is a build dependency of `muyo` rather than a development
one, and the `Shaders` target, because it costs no network and a consumer may want this project's
shaders — which means an embed building `all` still compiles them.

**The embed still requires network, for VMA.** A consumer with no network access cannot configure at all,
not because of the tests but because `vk_mem_alloc.h` is fetched from GitHub and is on the public header
path. Vendoring it, or accepting a system copy, is a packaging decision and belongs with #73.

**Target names are global.** `glfw`, `imgui`, `imnodes`, `stb`, `tinyobj`, `tinygltf` and `meshoptimizer`
are declared in the consumer's namespace once this project is a subdirectory, so a consumer embedding
another copy of any of them gets a redefinition error naming neither project. Aliases or a name prefix are
the standard cures and they belong with #73, not with #68.

## Consumers include coding agents

Stated as a requirement, and it is a stronger one than "good errors" — it changes what an error and a
document have to contain. An agent reading a failure has no debugger, no replay, no ability to ask a
follow-up question, and reads the message in a log. Three consequences, all cheap while the graph is
small:

**Fail at `Build()`, not at the GPU.** A graph can check its own declarations before recording anything:
every read has a writer, no cycles, attachment extents agree, descriptor bindings match reflection, the
queue families a node asks for exist. Each of those is a *declaration* error, and reporting it at Build
points at the line that is wrong. Today several of them surface as a validation error during execution,
several frames later, naming a handle.

**Name the graph entity, not the handle.** The validation layer can only say
`VkBuffer 0x…`. The graph knows which node and which resource that is.

```
[RenderGraph] node 'ShadowPass' reads 'DepthTarget' (texture, 2048x2048, D32)
              which node 'GBuffer' writes as R16_UNORM (1024x1024)
              -> extents must match; see docs/RenderGraph-Errors.md
```

**Attribute GPU errors for free with debug labels and object names.** The engine already has
`ScopedMarker` and `setDebugUtilsObjectName`; if the graph brackets every node and names every resource
it creates, then a VUID arrives *already attributed*:

```
Validation Error: ... vkCmdDrawIndexed(): ... [node: ShadowPass, resource: ShadowMap0]
```

For an agent that is the difference between "read the message and act" and "reproduce under RenderDoc".
It is also what the vendor tools need, so it pays twice.

**And make the docs actionable.** Doxygen coverage is already enforced for every public entity; the bar
for a library is one step further — each entry states its **preconditions** and its **failure modes**
(what makes it throw, and what the caller must guarantee), not only what it does. Plus an index of
failures with their fixes, in the shape of AGENTS.md's gotchas section, because an agent resolves an
error by searching for it.

## Tooling: a trace first, vendor tools second

Not in scope now — the tool is a separate project, and it may be a graph node with ImGui or a headless
CLI. Two things in the library make either possible, and one of them is worth designing early because it
is also our own test story.

**An execution-plan trace.** The graph already computes the plan: segments, queue families, transfers,
timeline values, resource dependencies. Emitting that as machine-readable data (not a picture) is
backend-agnostic, costs one serializer, and powers the visual tool, the headless CLI, and the tests —
"the second execution waited on value 4 of timeline Q" is a fact a test can assert and an agent can read.

**A capture-tool abstraction.** RenderDoc covers general validation but replay shows neither ray tracing
nor queue scheduling; the vendor profilers cover those (Radeon GPU Profiler, Nsight) and disagree
platform to platform. So: one small interface — begin/end capture, markers, resource annotations — with
a backend per tool and a no-op default. The graph already emits markers through `ScopedMarker`; routing
them through that interface means every backend gets them, and the per-platform scoping lives in the
backend rather than in the graph.

The reason to note this now rather than when the tool is built: the trace and the markers are the
*interface*, and retrofitting an interface onto code that never emitted one is the expensive version.
