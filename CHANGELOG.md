# Changelog

All notable changes to the RenderGraph library (`muyo_rg`) are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). The library uses semantic versioning with the
pre-1.0 caveat recorded in [`docs/RenderGraph-Library-Design.md`](docs/RenderGraph-Library-Design.md)
("Versioning the library"): until 1.0 the public surface is still being stated (#53), so a minor release
may break source compatibility — but each break is listed here, and a spelling that survives is marked
`[[deprecated("use X")]]` for one release before removal. A breaking entry is prefixed **Breaking:** in
the `Changed` / `Removed` list, because Keep a Changelog has no separate `Breaking` heading and the
policy's `Breaking` label is the marker a consumer greps for.

Since there are no tags yet, `0.1.0` is **unreleased**: the entries below seed the record with the
landed changes a consumer of this version can observe. From here on, a PR that changes behaviour adds
its entry as it lands. An entity's deprecation clock starts at the first tagged release, not at the
commit that deprecated it (#87).

## [0.1.0] - unreleased

### Added

- **Compile-time version.** `project(Muyo VERSION 0.1.0)` generates `<muyo_rg/version.h>`, which exposes
  `MUYO_RG_VERSION_MAJOR` / `_MINOR` / `_PATCH`, a Vulkan-style combined `MUYO_RG_VERSION` that can be
  compared in `#if`, `MUYO_RG_MAKE_VERSION`, and `MUYO_RG_VERSION_STRING`. `RenderGraph/MuyoVersion.h`
  re-exports the same values as documented constants for the API reference (#87).
- **This changelog and the in-tree deprecation gate.** The project's own targets compile with
  `-Werror=deprecated-declarations`, so the library cannot keep using a spelling it has deprecated
  (#87).
- **In-tree coverage of the no-renderer path.** A `COMPUTE`-only hand-built plan — no attachment, no
  graphics-family assumption — chained across two executions through a caller-owned timeline, in
  `rg_tests` (#54, #288).
- **Test coverage for a client-owned device.** Device debug markers and `CreateShaderModule` are
  exercised on a `VkRenderDevice` the test constructs itself, with the process-wide singleton left
  uninitialised (#274, #276, #291).

### Changed

- **Breaking: validation is the host's choice, not the device's.** `muyo_device` appends neither the
  validation layer nor a debug messenger; the in-tree hosts opt in through
  `InitializeDeviceWithValidation` in every configuration. A consumer that relied on linking the library
  to get validation must now ask for it (#58, #277).
- **Breaking: the graph depends on the device, not the renderer.** `muyo_rg` links `muyo_device` and
  declares no engine dependency; resources arrive through the graph-owned
  `RenderGraph::IGraphResourceAllocator` interface, and graph transients are no longer engine
  `BufferResource` / `ImageResource` subclasses. The transitional `RenderGraphServices::resourceManager`
  and the `GetMemoryAllocator()` lookup are gone (#272, #285).
- **Breaking: `CreateShaderModule` and the debug-marker helpers take the owning `VkDevice`.** Call sites
  pass the device they already use. The convenience `setDebugUtilsObjectName(objectHandle, ...)` overload
  remains, documented as singleton-only (#274, #276, #291).
- **The legacy renderer path is gone.** `helloVulkan`'s frame is graph nodes, and `RenderPassManager`, the
  recorded-only legacy passes and `src/RenderPasses/` are deleted (#20, #51, #257–#264).
- **The public surface and the error-reporting contract are stated.** Sixteen headers under
  `src/RenderGraph/` are API; `RenderGraphExecuteInfo.h` and `TransientMemoryPlan.h` are public so the
  value passed to `Execute()` and the plan returned by `GetTransientMemoryPlan()` stay documented, while
  the executor, planner, trace recorder, barrier derivation and shader manager are internal detail and
  leave the reference. `render_graph_docs.py --check` fails on an unclassified header or a stale list.
  Declaration and configuration errors throw a standard exception; failed Vulkan calls abort with their
  result and call site; no host error handler is installed (#53).

### Deprecated

- Nothing is deprecated in `0.1.0`. The mechanism is enforced by the build, so the first
  `[[deprecated("use X")]]` lands together with proof that the library itself no longer uses the old
  spelling.

### Removed

- **Breaking: the engine resource model from the graph's public surface.** `muyo_rg` no longer subclasses
  `BufferResource` / `ImageResource`, `dynamic_cast`s to them, or reaches `GetMemoryAllocator()` and
  `GetRenderResourceManager()`. See the `Changed` entry (#272, #285).
- **Breaking: the legacy render-pass path.** `RenderPassManager` and `src/RenderPasses/` are deleted; the
  `helloVulkan` frame runs through the graph (#51, #264).

### Fixed

- **Debug markers resolved the process-wide device.** A client-constructed `VkRenderDevice` had its
  `SCOPED_MARKER` labels recorded against the singleton (or silently dropped when the singleton was
  uninitialised). The marker helpers now use the `VkDevice` passed in (#274, #291).
- **`CreateShaderModule` created the module on the process-wide device.** With the singleton
  uninitialised, the loader rejected the null handle, so a client could not create a module at all. The
  helper now takes the owning device (#276, #291).
