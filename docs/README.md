# RenderGraph documentation

Two documents cover the RenderGraph:

| File | Kind | Purpose |
| --- | --- | --- |
| [`RenderGraph.md`](RenderGraph.md) | hand-written | Conceptual overview, quick start, async/RT/GPU-driven notes. |
| [`RenderGraph-api.md`](RenderGraph-api.md) | generated | Full API reference extracted from the Doxygen comments in `src/RenderGraph/*.h`. |

Both are plain Markdown so they render on GitHub and can be read directly by tools/agents.

## Generating and checking

```bash
# Regenerate HTML + XML + docs/RenderGraph-api.md (fails on undocumented API).
cmake --build build --target render_graph_docs

# Verify only: coverage + the checked-in Markdown is up to date (used by CI).
python3 scripts/render_graph_docs.py --check
```

`render_graph_docs` runs three steps:

1. **Doxygen** parses `src/RenderGraph/*.h` into XML + HTML.
2. **`docs/check_api_coverage.py`** fails if any public class/struct/enum, its public members or
   enum values, or any free function/alias lacks a brief or detailed description.
3. **`docs/gen_api_md.py`** regenerates `docs/RenderGraph-api.md`, grouped by header.

The task is also registered as the CTest test `RenderGraphApiDocs` when Doxygen is found.

## Writing the comments

Coverage is enforced, so every public entity needs a Doxygen description. A one-line `///` brief is
enough for most:

```cpp
/// Declare a graph-owned resource. It is allocated at Build() from `desc`.
/// @return `*this` for chaining.
RenderGraphBuilder& AddResource(const ResourceHandle& handle, ResourceDesc desc);
```

Struct members can use a trailing comment:

```cpp
uint32_t mips = 1;   ///< Mip level count.
```

Entities that should stay out of the reference can be wrapped in Doxygen's conditional blocks:

```cpp
/// \cond INTERNAL
... // hidden from the docs and from the coverage check
/// \endcond
```

## Getting Doxygen

`render_graph_docs` looks for Doxygen in `$DOXYGEN`, then on `PATH`, then in `<repo>/.tools`.
Without root you can fetch the Ubuntu package and extract it locally:

```bash
apt-get download doxygen
mkdir -p .tools/doxygen && dpkg-deb -x doxygen_*.deb .tools/doxygen
# then either rely on the .tools lookup, or:
export DOXYGEN="$PWD/.tools/doxygen/usr/bin/doxygen"
```

Graphviz (`dot`) is optional: class/include diagrams are enabled only when it is on `PATH`.
