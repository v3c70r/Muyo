# RenderGraph as a library: target architecture

Companion to [Roadmap.md](Roadmap.md). The roadmap says *when*; this says *what the boundary should be*.
Most of this is not implemented — it is the shape to build toward, written down while there is one
consumer so the second one is not what discovers it. Where a piece has landed, the section says so.

## Decisions, with their triggers

Each of these names what would reopen it, so a session arriving without history can tell a settled
question from an open one instead of re-arguing it. The sections below carry the reasoning; this is the
index.

| Decision | Reopened by | Where |
| --- | --- | --- |
| The public surface is C++ headers; a C ABI is a later wrapper | a consumer that cannot use C++ headers | §1 below |
| `muyo_device` is a shared library, not an inverted dependency | a client that cannot take the shared library | §2 below |
| The public surface is a named list of public headers; every other header under `src/RenderGraph/` is internal | a consumer needs a type the list calls internal, or #73's install set divides differently | §1a below |
| Error reporting: declaration/configuration errors throw a standard exception; a failed Vulkan call aborts | a consumer that must survive a device failure, or public entry points returning a result | §3 below |
| No C++ modules: fix the paths and add an export set instead | the surface is split (#53) **and** a consumer asks for a module | the packaging section |
| Concrete resource types live in `muyo_device`; the graph owns only a narrow resource/allocator interface; scene-level resources stay in the consumer | a consumer whose allocator is not the device's | §4 below (landed, #272) |
| Versioning: the library publishes, first-party consumers are named and must not lag | a consumer that cannot move, or a second first-party consumer | the versioning section |

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
| Instance, device, queues, memory, sync primitives, command pools | **device** | — | Landed as the `muyo_device` target (#58). |
| Queue *use* — which segment runs where | **graph** | queue-family facts | The graph plans; it must not *require* a graphics family (compute-only profile). |
| Transient resources, aliasing, lifetimes | **graph** | allocator | The policy is the graph's; the memory is not. The interface is graph-owned and the concrete types live in `muyo_device` (#272). |
| Barriers and layouts | **graph** | — | The reason the graph exists. |
| Pipeline creation | **graph** | shader source provider, pipeline cache | It builds pipelines from node descriptions. Keep that — and a client supplies the SPIR-V bytes (or a search path) and a cache, not only a file name on disk; the provider landed in #292. |
| Descriptor heap and bindless binding | **graph** | descriptor-heap service | B1a/B1b replaced descriptor sets: the graph receives a `DescriptorHeapService`, writes no set layouts, pools or pipeline layouts, and exposes a use's heap index. |
| Swapchain, present, frame pacing | **host** | — | Not the graph's, not the pipeline's. |
| Validation-layer enablement | **host** | — | Landed in #58: the device exposes the messenger hook, the host chooses. See below. |

## Decisions

### 1. C++ headers now; a C ABI is a later wrapper

Decided: the public surface is C++ headers, and the library is open source. If ABI stability or
cross-language use is needed later, a C ABI can be wrapped *around* it — that is a strictly additive move,
whereas designing for it now would constrain the API for a client that does not exist.

Recorded with its trigger so the decision can be revisited rather than drifted into: wrap a C ABI when
there is a consumer that cannot use C++ headers.

### 1a. The public surface is a named list of headers (landed, #53)

The public surface is **seventeen headers** under `src/RenderGraph/`, and everything else in that directory is
implementation detail. The canonical list lives in `scripts/render_graph_docs.py` (`PUBLIC_HEADERS` /
`INTERNAL_HEADERS`) and is reproduced, and checked, in `docs/RenderGraph.md` under "Public surface":

> `CompiledRenderGraph.h`, `MuyoVersion.h`, `PipelineCache.h`, `PSODesc.h`, `RenderGraphBuilder.h`,
> `RenderGraphExecuteInfo.h`, `RenderGraphExecutionPlan.h`, `RenderGraphNodeContext.h`,
> `RenderGraphNodeResource.h`, `RenderGraphResourceDesc.h`, `RenderGraphResourceHandle.h`,
> `RenderGraphResources.h`, `RenderGraphShader.h`, `RenderGraphTrace.h`, `ShaderCache.h`,
> `ShaderSourceProvider.h`, `TransientMemoryPlan.h`.

Four of those exist because the old split was a file label rather than a type boundary. `RenderGraphExecuteInfo`
moved out of the executor, `TransientMemoryPlan` out of the planner, `ShaderAssetManager` out of the public shader
header, and the trace recorder out of the public trace header. A consumer names the data it passes and receives; the
mechanism that produces it stays internal. The allocator interface is public on purpose - a consumer with its own
memory manager implements `IGraphResourceAllocator` - and so is the shader source provider, which a host implements
to supply SPIR-V bytes instead of files (#292). The concrete resources live in `muyo_device` (§4).

The boundary is **checkable**, not prose:

- The Doxygen `INPUT` set is generated from `PUBLIC_HEADERS`, so `docs/RenderGraph-api.md` and the coverage check
  cover exactly the stated list and no internal header.
- `render_graph_docs.py --check` fails when a header under `src/RenderGraph/` is in neither list (a new header
  must be classified), when a listed header is gone, or when the checked block in `docs/RenderGraph.md` and the
  lists disagree.
- Internal headers carry a one-line note saying so, and are not installed. Since #297 the public headers are
  self-contained: `RenderGraphBuilder`'s private state lives in an out-of-line `Impl` defined in its `.cpp`, so
  the header includes only public and device headers. `render_graph_docs.py --check` fails when a public header
  includes an internal one. The public include directory is exactly `PUBLIC_HEADERS`, and the package has no
  `include/Muyo/detail/RenderGraph/` directory.

**What is not promised.** The internal headers have no source compatibility guarantee, and the surface has no ABI
stability yet (the C ABI is the later wrapper in §1). A consumer that needs an internal type reopens this decision
rather than including it.

**Third-party vocabulary in the surface.** The public headers need Vulkan, VMA and SPIRV-Reflect:
`BufferResourceDesc::memoryProperties` and `IGraphResourceAllocator` speak `VmaMemoryUsage`, and `ShaderAsset`
carries the reflection object. That is part of the stated interface until the VMA vocabulary is replaced by a
graph-owned enum (#293); packaging those dependencies is #73.

### 2. The device is a shared library, not an inverted dependency

Two ways to get a clean boundary:

- **shared library (chosen)** — `muyo_device` is its own target with a documented surface, and `muyo_rg`
  links it. Both ship. Simple, and honest about the graph being Vulkan-only.
- dependency inversion — the graph declares the device interface it needs and the device implements it.
  More flexible, and pays indirection for a second backend that does not exist.

Choose the shared library, and revisit on the same trigger as the C ABI: a client that cannot use
`muyo_device`. The direction was right and the boundary has landed: `muyo_rg` links `muyo_device` and
nothing else, the concrete resources live in `muyo_device` (§4, #272), and the remaining engine-shaped
lookups and includes are gone. The pre-#272 scan that motivated the split (9 files including a header from
outside the directory, one engine-singleton call site, `muyo_rg` linking `muyo`) is kept in
`Roadmap-Visual.md` section 7 as the before picture, not restated as current.

### 3. Error reporting: declaration errors throw, failed Vulkan calls abort (decided, #53)

The contract has three tiers, and a consumer can rely on which tier it is in:

| Failure | Mechanism | Can a caller act on it? |
| --- | --- | --- |
| A malformed declaration, a missing injected service, an out-of-range value | throws a standard exception (`std::runtime_error` for the graph's preconditions, `std::invalid_argument` from a range-checked conversion), in every build configuration, naming the node/resource/binding | **Yes** - catch it, fix the declaration, build a graph again |
| A failed Vulkan call (`vkCreate*`, `vkQueueSubmit2`, `vkWaitSemaphores`, memory allocation) | `VK_ASSERT` prints `[FATAL]: Vulkan call failed with <VkResult>` and the call site, then aborts | No - the process ends |
| A validation-layer `ERROR` when the host installed the callback | `FatalAbort`, because the callback runs on the driver's C stack | No - the process ends |

The first tier is the library contract. `FatalError` *throws* a `std::runtime_error` - the earlier version of
this section said it aborted, which was wrong - and the graph's own declaration paths throw directly
(`AddResource`, `ImportResource`, `AddNode`, `AddDependency`, `Build`, `Execute`). A failed `Build()` leaves no
GPU objects behind and the device usable: the `a failed Build() keeps its message and leaves the device clean`
case builds a fresh graph on the same device afterwards, which is the property a long-running service needs.

The second tier is deliberate rather than unfinished. `VK_ASSERT` is unconditional (`NDEBUG` cannot remove it),
reports the failing result and the call site, and aborts; the alternative - throwing from ~70 call sites whose
failure is a device or driver fault - would impose exception-safety requirements on every caller and still leave
the caller unable to do anything about a lost device. `FatalError` exists for the cases that are *configuration*
rather than device failure (a missing layer, an unsupported Vulkan version, no enumerated device); it throws.

**Rejected for now: a host-installed handler and a result type on the public entry points.** A handler is a
second error channel to keep consistent with the throw path, and a result type changes every signature before
there is a consumer that needs it. Not doing either is the decision, recorded with its trigger: reopen it when a
first-party consumer must keep the process alive across a device failure, or when the public entry points return
a result for another reason. At that point the second tier is what changes; the first tier stays.

**Checkable.** `src/tests/ErrorReportingTests.cpp` (`[ErrorReporting]`) pins the throw type/message/location and
that `VK_ASSERT` aborts with its diagnostic; the declaration paths are the `CHECK_THROWS_*` cases beside the
failure index in `docs/RenderGraph.md`, and the validation-callback abort is the `[RenderDevice]` fork case.
The mechanisms are greppable too: the first tier is the `throw` statements in `src/RenderGraph/`, and
`grep -rn "VK_ASSERT" src/RenderGraph/` lists the second-tier sites.

### 4. The graph owns the resource interface; `muyo_device` owns the concrete resources

Decided as the direction, with #272 the work. The graph declares a narrow resource/allocator interface
and receives it by injection, rather than subclassing the engine's concrete resource types. The
VMA-backed buffer/image implementation lives in `muyo_device` — `VkMemoryAllocator` already sits there —
and scene-level resources (`Texture`, `Material`, `Geometry`, `MeshVertex`, `RenderResourceManager`) live
in the consumer (`muyo-renderer`, `muyo-llm`).

This closes the last "the surface is the engine" dependency in one move. Today `RenderGraphServices` is
typed `RenderResourceManager*`, the graph subclasses `BufferResource`/`ImageResource`
(`TransientResources.h`) and identifies them with `dynamic_cast`, and it reaches two globals —
`GetRenderResourceManager()` and `GetMemoryAllocator()`. The trigger that would reopen this decision is a
consumer whose allocator is not the device's — a host that allocates through its own memory manager — at
which point the interface stays and only the implementation moves.

**Landed in #272.** `src/RenderGraph/RenderGraphResources.h` declares the interface the graph owns:
`IGraphResource` with `IGraphBuffer`/`IGraphImage`/`IGraphAccelerationStructure`, and
`IGraphResourceAllocator` (owned resources, unbound resources plus aliasing blocks, root memory and
views). `RenderGraphServices::resourceAllocator` is that pointer; the two global lookups are gone, as are
the `dynamic_cast`s to engine resource classes and the engine headers in the graph's public surface.
The concrete VMA-backed implementation is `src/RenderDevice/VkGraphResourceAllocator.{h,cpp}` in
`muyo_device`: `VkGraphBuffer`/`VkGraphImage` own the Vulkan handles and memory, and the allocator
implements the graph interface. The engine's `BufferResource`, `ImageResource` and
`AccelerationStructure` implement the same interfaces, so an imported engine resource is still usable by
a node callback without the graph naming its class. `muyo_rg` links `muyo_device` and nothing else; the
graph-only test binary `rg_tests` builds and passes against those two targets.

This is also what turns the stack's two bottom layers into a repo boundary: `muyo_device` + `muyo_rg`
stay in this repository, while the host and the render pipeline move to a consumer repository
(`muyo-renderer`) built on the graph the way `muyo-llm` is. The render pipeline (layer (b)) remains a
consumer of the public surface only; the resource boundary is the prerequisite that lets it stop linking
the engine.

## The device layer and validation policy (landed, #58)

`muyo_device` is now a target built from `src/RenderDevice/`: instance, device, queues, memory, sync
primitives, command pools, the VMA allocator, the descriptor-heap service, the extension loader,
barriers, the pipeline-state builder and the debug utilities. The engine and the graph link it
(`muyo` publicly, and `muyo_rg` directly), so the bottom of the stack no longer arrives through the
engine's target. `ScopedMarker` - the composition of a Vulkan label with a capture-tool marker - moved
to `src/Debug/ScopedMarker.h`: the capture backend is host policy, and keeping the composition out of
the device header is what lets a client link `muyo_device` without the engine (#275).

The validation policy is the host's, which is the behaviour change that was the reason to file #58
separately from a mechanical target split:

- `VkRenderDevice::Initialize` appends neither `VK_LAYER_KHRONOS_validation` nor
  `VK_EXT_debug_utils`, and creates no messenger. The minimal call is a headless, unvalidated device.
- `VkRenderDevice::InitializeDebugMessenger` is the hook a host can give itself. The device never
  installs the callback on its own, and `Unintialize` destroys it if it was installed.
- This repository's hosts keep their previous behaviour by opting in through
  `Muyo::InitializeDeviceWithValidation` (`src/Debug/Validation.h`). That is policy in the engine, not
  in the library: an inference service linking `muyo_device` may pass an empty layer list and pay
  nothing for diagnostics it did not ask for.

The device still selects a physical device, but a null surface is a supported argument to
`CreateDevice`: no present-capable queue family is selected and `GetPresentQueue()` stays null, so a
headless client does not require a surface. Both configurations exercise the unvalidated path through
`VkRenderDevice: validation is opt-in and a headless device works without it` (`[RenderDevice]`).

## Packaging an external consumer, and why not C++ modules yet

How an external application *takes* the library is a separate question from how the library is layered, and
it is the question #68 opened and #73 finishes. Measured before deciding.

### What a consumer pays today

One translation unit that includes only `RenderGraph/RenderGraphBuilder.h`, compiled with the flags the
project itself uses. The provenance is part of the figure: these were reproduced on a second machine and
the magnitude held while the counts did not, so an absolute number quoted without its machine is wrong.
The numbers below were measured **before #272 removed the engine resource types** from the graph's path;
they are the before picture for the header split, not a claim about the stated surface in §1a.

| | here — GCC 13.3, system Vulkan headers `VK_HEADER_VERSION 313` | reviewer's RTX 3090 box — SDK 1.4.363 |
| --- | --- | --- |
| transitive headers | **479** unique, **169** from `thirdparty/` | **541** raw / **502** unique, **147** from `thirdparty/` |
| preprocessed lines | **178,394** | **135,153** |
| compile time, trivial TU | **1.8 s** | **0.19 s** |

What both agree on is the claim the numbers exist to support: hundreds of headers, six figures of
preprocessed lines, paid per including translation unit, with `thirdparty/` a large fraction of it. The
variance is header revision, raw-versus-unique counting and machine speed — none of which is the point.

At that point the public header reached imgui, tinygltf, meshoptimizer, glm, SPIRV-Reflect and the
engine's resource managers: "every header under `src/RenderGraph/` is public" in numbers. The stated
surface in §1a is the answer. It still needs Vulkan, VMA and SPIRV-Reflect; since #297 the builder header no
longer includes the internal headers its private state used to name, so what a consumer compiles is the named
public list and the device headers it names, not the implementation.

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
3. **It does not address distribution.** #68 was `CMAKE_SOURCE_DIR` resolving to the consumer's root, 28
   paths built from it, and `muyo_rg` linking the engine rather than standing alone; it is closed, with
   the standalone target in #53 and the package in #73. Modules change none of that: they are a
   source-level mechanism and do not produce an installable package.
4. **The upside is narrower than it looks.** A module would *enforce* the boundary — the compiler rejects
   reaching into internals, where a header split only reduces what is reachable — and that is a genuine
   advantage, the same one #53 wants from a stated surface. But it is a second-order benefit next to
   "the consumer cannot build the thing".

**Decision: fix #68 with paths and declared dependencies (landed), add install/export rules for the
packaging case (#73), and revisit modules with a trigger** — when the public surface has been split (#53)
*and* a consumer asks for one. Recorded the same way as the C-ABI decision, so it can be revisited rather
than re-argued.

### What the package ships (landed, #73)

The install rules produce a relocatable CMake package under `<prefix>/lib/cmake/muyo_rg/`:

```cmake
find_package(muyo_rg 0.1 REQUIRED)          # version file from project(Muyo VERSION ...)
target_link_libraries(app PRIVATE Muyo::muyo_rg)
```

`Muyo::muyo_rg` links `Muyo::muyo_device` and nothing from the engine, and both targets are exported with
their `PUBLIC` include directories. The installed headers are a stated set rather than `src/RenderGraph`:

| Installed path | Contents |
| --- | --- |
| `include/RenderGraph/` | exactly `PUBLIC_HEADERS` (17 headers) |
| `include/RenderDevice/` | the device target's headers, which the graph's public interface needs |
| `include/muyo_rg/version.h` | the generated version macros (the checked-in `MuyoVersion.h` is in the public set) |
| `include/Muyo/detail/thirdparty/` | VMA and SPIRV-Reflect, which the public headers name |

VMA and SPIRV-Reflect are exported targets too, not dependencies a consumer has to find. **Vulkan is the one
external dependency**, and `muyo_rgConfig.cmake` looks it up with `find_dependency(Vulkan)`, so a
missing SDK fails at the consumer's configure step with a message naming Vulkan instead of at a compile or
link later. `render_graph_docs.py --check` compares the CMake header list against the stated surface and
rejects a public header that includes an internal one, so the install set cannot drift from #53's list or from
the self-containment #297 established.

**What is not in it.** There is no shared library, no `SOVERSION`/SONAME and no ABI tooling yet — the targets
are static, and the C ABI is the later wrapper in §1. There is no `include/Muyo/detail/RenderGraph/` either:
the public headers are self-contained because `RenderGraphBuilder`'s private state is an out-of-line `Impl`
(#297), so the internal graph headers are not installed.

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

## Versioning the library, and how a consumer learns about a change

The package carries a version. This says what the version means, how a breaking change reaches a
consumer, and which consumers are not allowed to lag behind it. **The library version itself exists
(#87):** `project(Muyo VERSION 0.1.0)` generates `<muyo_rg/version.h>` and the documented constants in
`RenderGraph/MuyoVersion.h`, and the package's `muyo_rgConfigVersion.cmake` is generated from the same
`project()` version (#73), so `find_package(muyo_rg 0.1)` is checked against the header a consumer compiles
against rather than against a second, hand-maintained number.

### The library publishes; the consumer pulls

A library cannot keep a registry of its consumers, and a per-consumer notification requirement does not
survive the second or third one. So the notification lives **in the artifact**, and the consumer finds it
on their own terms:

| Mechanism | Who it reaches | Cost to the library |
| --- | --- | --- |
| `project(... VERSION)` plus a version header (`MUYO_RG_VERSION` and its components) | everyone who compiles or finds the package | none |
| `CHANGELOG.md` with `Breaking` / `Deprecated` sections | anyone who reads before upgrading | one entry per change |
| `[[deprecated]]` on the old spelling | **every consumer, in their own build, without the library knowing they exist** | one attribute |
| `SameMajorVersion` in the package version file (#73); `SameMinorVersion` while the major is 0 | `find_package(muyo_rg 1.2)`, `find_package(muyo_rg 0.1)` | none |

### The rule that makes a version sufficient: a breaking change has to be detectable

A changelog only reaches a consumer who reads it, so a breaking change may not depend on being read. Each
kind has a mechanism, and the mechanism *is* the notification:

- **Removed or renamed entity** → a compile error.
- **Replaced entity** → `[[deprecated("use X")]]`, so the warning lands in the consumer's own build and
  names the replacement.
- **A new requirement** → a **named runtime failure**. Raising the device floor (#79) is the worked
  example: a device without the feature now fails at device creation naming it, rather than producing a
  wrong result.
- **Changed semantics** → the honest hard case, because nothing reaches the consumer automatically. Make
  it loud where a loud form exists, deprecate where it does not, and list it under `Breaking`. What must
  not happen is a silent change of meaning.

**The guarantee rows are the floor of what counts as breaking, not the ceiling.** A consumer will depend on
observable behaviour the rows never mention - timing, allocation, ordering, the number of dispatches a node
records - and Hyrum's law says that becomes a dependency whether or not it was promised. Read this rule
generously: if a change can be observed by a consumer who only used documented behaviour, it belongs under
`Breaking` even when no row names the thing that changed. The alternative - "our rows do not mention frame
pacing, so the change is not breaking" - is a technically correct argument that loses a consumer.

This is what keeps versioning as cheap as it is: the library does not chase anyone, and a consumer that
compiles against a new version is told by their own compiler.

### First-party consumers are named, and are expected not to lag

Third-party consumers pull. **First-party** ones - `muyo-llm` today, `helloVulkan` as it adopts the graph -
are a small known set and are named, because they are not only consumers: they are how the library's
coverage is measured. A first-party consumer on an older version leaves the library's claims about the
current version unverified, which is a worse problem than a notification one.

So two things differ for them:

- **The change names them.** A breaking change says which first-party consumer has to move, and a PR that
  breaks one does not merge with that upgrade unplanned.
- **Their rows in `Consumers.md` are claims about the current version.** A first-party consumer that has
  not upgraded leaves its rows stale, and the row is the contract.

### Only the current version is supported

Recorded with its consequence rather than left implicit: there is no backport policy and no branch to
maintain, and a first-party consumer is expected to be on the current version. That is affordable only
because of the detection rule above - a consumer compiling against the new version is warned before
something is removed.

**The deprecation window is notice, not support**, which is how the two fit together: an entity is
deprecated in one release and removed in a later one, never the same one. The consumer sees the warning
in their own build and migrates before the removal lands; nothing has to keep the old version alive.

**When releases begin**: with the first package (#73). Until then a master commit is not a release, and
the window above has nothing to count against - so `Breaking` and `Deprecated` entries still apply to
master as they are written, and an entity's removal clock starts at the first tagged version, not at the
commit that deprecated it. Saying so matters because the alternative reading - that a deprecation on
master starts a window immediately - would make the rule unsatisfiable before there is anything to
release.

Revisited when a consumer cannot move for reasons of its own - that is the point at which an LTS branch
has to be maintained rather than merely promised.

### 1.0 means the boundary exists, not that the code is finished

**1.0 when the public surface is stated (#53's second half) and `Consumers.md` has no `gap` row.** Before
that it is 0.x, where breaking changes are allowed and still follow everything above. Releasing 1.0 while
the surface is a gap would make the number claim something it does not.

**And the compatibility mode is a question 0.x has to answer before #73 writes the version file.** Semver
permits anything to break inside a 0.x series, so `find_package(muyo_rg 0.9 SameMajorVersion)` would
silently accept a breaking 0.10 - the exact case the compatibility field exists to prevent, defeated by
the pre-1.0 convention. So either the package is exact-match (or `SameMinorVersion`) while the major is 0,
or the version file states that 0.x minors do break.

**Decided (#73): `SameMinorVersion`.** `write_basic_package_version_file(... COMPATIBILITY SameMinorVersion)`
is the 0.x answer: `find_package(muyo_rg 0.1)` accepts 0.1.x and rejects 0.2, so a minor release that the
pre-1.0 caveat allows to break cannot satisfy a request for the older minor. The same table's
`SameMajorVersion` row is the 1.0 answer, and the CMake call changes to it when the version reaches 1.0
(the policy above, and the 1.0 condition below, are what move the version). `SameMinorVersion` needs CMake
3.11, which is why this file's `cmake_minimum_required` moved there.

## Consumers include coding agents

Stated as a requirement, and it is a stronger one than "good errors" — it changes what an error and a
document have to contain. An agent reading a failure has no debugger, no replay, no ability to ask a
follow-up question, and reads the message in a log. Three consequences, all cheap while the graph is
small:

**Fail at `Build()`, not at the GPU.** A graph can check its own declarations before recording anything:
every read has a writer, no cycles, attachment extents agree, a use's kind and usage agree with its
resource, the queue families a node asks for exist. Each of those is a *declaration* error, and reporting
it at `Build()` points at the line that is wrong instead of at a handle several frames later. The
declaration checks landed in #55: the failure index in `docs/RenderGraph.md` lists each message with its
cause and fix.

**Name the graph entity, not the handle.** The validation layer can only say
`VkBuffer 0x…`. The graph knows which node and which resource that is.

```
[RenderGraph] node 'ShadowPass' reads 'DepthTarget' (texture, 2048x2048, D32)
              which node 'GBuffer' writes as R16_UNORM (1024x1024)
              -> extents must match; see the failure index in docs/RenderGraph.md
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
(what makes it throw, and what the caller must guarantee), not only what it does. The failure index in
`docs/RenderGraph.md` is the existing shape for the graph's own errors, in the spirit of AGENTS.md's
gotchas section, and it has to cover the stated surface as the surface is split out, because an agent
resolves an error by searching for it.

## Tooling: a trace first, vendor tools second

The tool is a separate project, and it may be a graph node with ImGui or a headless CLI. The two
interfaces that make either possible have landed in their structural form; the timing data and the vendor
backends are what remain.

**An execution-plan trace — structural half landed (#165, #166).** The graph computes the plan: segments,
queue families, transfers, timeline values, resource dependencies. `RenderGraphTrace.h` records that as
data: `SetTraceSink` receives one trace per execution, `MUYO_RG_TRACE` writes it as well, and
`ToJson`/`ToText` serialise it (`ToGraph` feeds `ToDot`/`ToMermaid` for diagrams). The
`[RenderGraphTrace]` cases assert on the plan rather than on pixels, and the debug page in `helloVulkanRG`
consumes it. **Pending (#197, the phase-2 half of #56):** the timings. `TraceInterval` is reserved and left
invalid, and `TraceScope` reports `bKnown = false` for the timestamp capability, so the fields exist before
the values do. Timings are also what #59's cost measurement consumes.

**A capture-tool abstraction — landed (#167).** RenderDoc covers general validation but replay shows
neither ray tracing nor queue scheduling; the vendor profilers cover those (Radeon GPU Profiler, Nsight)
and disagree platform to platform. `src/Debug/CaptureTool.h` is the one small interface — begin/end
capture, markers, resource annotations — with a no-op default and a RenderDoc backend, and `ScopedMarker`
composes the marker routing, so the graph's node markers reach whichever backend is installed. The RGP and
Nsight backends are still to write; the per-platform scoping lives in the backend rather than in the
graph.

Landing the interfaces early was the point: the trace and the markers are the *interface*, and
retrofitting an interface onto code that never emitted one is the expensive version.
