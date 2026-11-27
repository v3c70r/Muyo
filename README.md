# 無用 — the Muyo libraries

This repository builds **`muyo_rg`** (the RenderGraph) and **`muyo_device`** (the Vulkan 1.4 device
layer), plus a test-only `muyo_debug`. It is a library, not a renderer: the render pipeline, the
window/present host, the apps, the shaders and the sample assets live in
**[muyo-renderer](https://github.com/v3c70r/muyo-renderer)**, which consumes these libraries as a
submodule. The split landed in #318 (2026-11-25).

`muyo_rg` is a render graph: declarative resources and nodes, derived synchronization (Vulkan
`synchronization2` + timeline semaphores), a descriptor-heap bindless path (`VK_EXT_descriptor_heap`,
no descriptor sets or pipeline layouts), per-frame `Build()` with resource/pipeline/descriptor caches,
and transient memory planning. `muyo_device` owns device creation, the memory allocator and the
concrete resources; validation is the host's opt-in, never the library's.

## Building and testing

```sh
cmake -S . -B build
cmake --build build --target tests rg_tests
./build/tests && ./build/rg_tests        # run on a GPU; rg_tests prints [test device]
```

The Vulkan SDK must be on the environment (headers, loader, validation layer). No shader compilers are
needed: this repository ships no shaders. Two test binaries: `tests` is device-free, `rg_tests` creates
a device. Both configurations (default and `-DFEATURE_RAY_TRACING=ON`) must pass; see
[`AGENTS.md`](AGENTS.md) for the full process and verification contract.

## Consuming the library

- **Submodule + `add_subdirectory`** — the way `muyo-renderer` and `muyo-llm` consume it.
- **CMake package** — `cmake --install` writes `Muyo::muyo_rg` and `Muyo::muyo_device`; a consumer can
  `find_package(muyo_rg 0.1 REQUIRED)` and link `Muyo::muyo_rg` without the source tree.

The public surface is stated and coverage-enforced: [`docs/RenderGraph.md`](docs/RenderGraph.md) lists
it, `scripts/render_graph_docs.py --check` verifies it, and the generated API reference is published at
<https://qgu.io/Muyo/> on every push to `master`.

## Key documents

| Document | Contents |
| --- | --- |
| [`AGENTS.md`](AGENTS.md) | Working agreement: branches, testing, review (including the second-machine rule). |
| [`docs/Roadmap.md`](docs/Roadmap.md) | The plan: goal, phases, gates, debt policy, risks. |
| [`docs/RenderGraph.md`](docs/RenderGraph.md) | RenderGraph concept, quick start, public surface. |
| [`docs/RenderGraph-Library-Design.md`](docs/RenderGraph-Library-Design.md) | Target architecture and recorded decisions. |
| [`docs/Consumers.md`](docs/Consumers.md) | Who consumes the library and the guarantees each relies on. |
| [`CHANGELOG.md`](CHANGELOG.md) | Consumer-visible changes, breaking entries marked. |

The renderer's own documents (capture recipes, coding conventions) live in `muyo-renderer`.
