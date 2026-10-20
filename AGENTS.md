# AGENTS.md — working agreement

Shared context for anyone doing work in this repository, human or agent.

It exists because work happens across **separate sessions that do not share history**: a
*development session* writes code and a *review session* reviews the pull request cold, with none
of the developer's context. Both should be able to work from this file alone.

**This file is versioned like code.** If you find a better way to work, propose it in a pull request
with a short rationale — what improves, what it costs — and let it go through review. Do not
silently diverge from it.

---

## 1. Branch and merge model

- Trunk is `master`. It must always build and pass tests.
- Branch off an up-to-date `master`, one workstream per branch:
  - RenderGraph work: `rg-<topic>` — e.g. `rg-frame-sync`, `rg-descriptor-sets`.
  - Everything else: `<area>-<topic>` — e.g. `sync2-baseline`, `agent-workflow`.
- **Check the branch before you edit anything:** `git branch --show-current`. With `AGENTS.md`
  now committable straight to trunk, a checkout can legitimately be on `master` while code work
  is in progress, so "I am on master" no longer means "I am not about to commit code to master".
  Two sessions have already edited the wrong branch here; both were caught only by reading the
  diff, and one produced an edit against a file version that did not have the fix it was extending.
  `git status --short` before `git add` is the cheap habit. **And check it again after finishing a detour
  onto another branch**, because that is how it actually happens: switching to a PR branch to add one
  paragraph, then starting the next piece of work without switching back. That is how the #55 commit
  landed on #96's branch, with its upstream rebound to a new remote branch by the `-u` push - the second
  time this file records that outcome, which is the point: the rule was already written and was not the
  thing that failed. The check has to be *run*, at the start of work and again after any detour.
- **A branch created from a remote ref inherits that ref as its upstream.** `git checkout -B
  versioning-policy origin/master` sets the new branch to track `origin/master`, so a later bare `git
  push` is aimed at **trunk**. What stopped it here was `push.default=simple`, which refuses when the
  upstream branch name differs from the local one - and the push failed with no output at all through a
  `| tail -1`, so the only reason the wrong target was caught is that the state was checked instead of
  assumed (`origin/master` had not moved). Two habits follow: push with an explicit refspec
  (`git push origin HEAD:refs/heads/<name>`), and set the upstream deliberately with
  `git branch --set-upstream-to` rather than inheriting it from the base. **A push whose output you did
  not read is a push whose target you do not know** - which is the same rule as reading an exit status
  rather than grepping for a word, one layer out.
- **Do not chain state-changing git commands with `&&`.** A `git branch -f master origin/master` that
  refuses (it does while the worktree is *on* master) stops the chain, so the `git checkout <branch>` after
  it never runs - and the next command executes on the branch you thought you had left. That is how the
  `-u` push below bound `master`'s upstream to a feature branch's remote, one turn after this rule was
  written. Chain reads freely; run writes as separate steps, and check `git branch --show-current` and
  `@{upstream}` afterwards rather than assuming the step you meant was the step that ran.
- Never commit directly to `master`. **The `AGENTS.md` exception is gone as of 2026-10-12**, not by a
  change of mind but because the master ruleset (below) refuses a direct push from anyone, including an
  administrator: it has no bypass actors. A process lesson still has to be recorded promptly, so the
  route is now a small, self-describing PR rather than a straight commit — it just has one more step, and
  the step is the one that keeps the review record honest.
  Everything else keeps the branch-and-squash flow.
- **Merge with squash**: `gh pr merge <n> --squash` — **enforced by the ruleset since 2026-10-12**, which
  allows only that method. Trunk history is then one commit per
  workstream, and the PR body becomes the commit body — so write it to be read as `git log`. The merge
  happens once an approving **review** is on the PR (section 6), not once a comment reads like one.
- Keep one concern per pull request. A mechanical refactor and a behaviour change are two PRs even
  when the second would be a one-line edit of the first.
- For a large PR, post a **summary/index comment** on the PR (scope, features, fixes, review
  history, verification, deferred work) so the context survives the squash and can be linked to
  later.
- **Formatting-only commits are listed in [`.git-blame-ignore-revs`](.git-blame-ignore-revs)** so
  `git blame` skips them (`git config blame.ignoreRevsFile .git-blame-ignore-revs`). A pass that
  reformats the whole tree belongs in its own PR with no functional changes, and its commit must
  land on `master` verbatim for the entry to resolve - squash-merging replaces the SHA, so re-point
  the entry afterwards. `git blame` already passes whitespace-only changes through to the
  original author, so the list only covers what it cannot, such as blank lines the formatter
  inserted.

## 2. Project management

Issues are the durable backlog; the project board is a view over them.

- Board: **Muyo RenderGraph** — <https://github.com/users/v3c70r/projects/3> (project number `3`).
- `Status`: `Todo` / `In Progress` / `Done`. Move an item to `In Progress` when you start it.
- `Area` (single-select): `Synchronization`, `Descriptors`, `Lifetime`, `Ray tracing`,
  `Performance`, `Graph API`, `Adoption`, `Tooling`, `Device`.
- Labels: `rendergraph`, `synchronization`, `descriptors`, `ray-tracing`, `tech-debt`, plus the
  default `bug` / `enhancement`.

Rules:

- **Anything deferred becomes an issue, not a silent `TODO`.** If you leave a marker in code, it
  must name the phase or issue that will handle it, e.g. `// TODO(A2): ...` or `// TODO(#12): ...`.
- Issues must be self-contained: what, why, where (`path:line`), acceptance criteria, and
  references to the PR review finding / design doc they came from.
- Every issue created is added to the board.

```bash
# file an issue and put it on the board
gh issue create --title "A2: Derive precise barrier stage/access masks" \
  --label "enhancement,rendergraph,synchronization" --body "..."
gh project item-add 3 --owner v3c70r --url <issue-url>

# discover field / option ids, then set them
gh project field-list 3 --owner v3c70r --format json
gh project item-list 3 --owner v3c70r --format json
gh project item-edit --project-id <project-id> --id <item-id> \
  --field-id <field-id> --single-select-option-id <option-id>
```

## 3. Development process

- **Language/API floor: Vulkan 1.3.** The engine uses `synchronization2` and timeline semaphores
  only; the pre-1.0 synchronization API is not to be reintroduced. Required features are enabled
  *and verified* at device creation (`VkRenderDevice::CreateDevice`) — add new requirements to that
  list, never enable a feature blindly.
- **Never initialize Vulkan structs positionally.** Use field-by-field assignment or C++20
  designated initializers. `{}` and `{VK_STRUCTURE_TYPE_...}` are fine. See
  [`docs/CodingConventions.md`](docs/CodingConventions.md) — the sync2 barrier layouts make a
  positional initializer a silent field shuffle.
- **Separate refactors from semantics.** State in the PR which one it is. A refactor PR may leave a
  bug marked but unfixed, as long as it says so and files the issue.
- **Asserts are for programmer error; an error path must survive `NDEBUG`.** `assert` compiles out in
  release builds, so it may not guard a Vulkan result, an allocation or a lookup the next line
  depends on - the build would proceed with a null handle and fail somewhere unrelated. Use
  `VK_ASSERT` for Vulkan results: it always runs, reports the failing `VkResult` and the call site,
  and aborts. Use a runtime check for any other invariant a release build must still enforce. Keep
  `assert` for conditions only reachable by editing the code in front of you.
- **Evidence over assertion.** If a change depends on a non-obvious platform fact (struct layout,
  driver behaviour, extension support), verify it on the actual toolchain and put the evidence in
  the PR — an `offsetof` dump, a validation message, a test that fails on the old revision. The
  review session will challenge unverified claims, and "the docs say so" is not evidence about
  *these* headers.
- **`VK_ASSERT` is unconditional; plain `assert` is for invariants only.** `VK_ASSERT` reports the
  `VkResult`, file, line and function and aborts — it is the error path, so `NDEBUG` must not remove
  it. A plain `assert` expresses something true by construction *in this code* (an index bounded by a
  preceding loop, a size two callers ago), and disappearing under `NDEBUG` is fine. Anything that can
  be falsified by *input* — a file, a driver, a global, a `vkCreateX` result — needs a runtime check
  that survives `NDEBUG`: a throw or a warning, never an `assert`. Two lessons already paid for:
  a device-layer `assert` compiled out and let an invalid `VkDeviceCreateInfo` through, and an
  `assert(false)` on an unsupported resource type left a descriptor unwritten, surfacing at draw time
  as an unrelated VUID.
- **Build and run `Release` when you touch initialisation, descriptor or resource code.** `NDEBUG`
  also removes `assert`-gated *side effects* that no longer exist, and `-O3` exposes lifetime bugs a
  Debug build cannot see: the descriptor updates bound to null buffers, and three Release tests that
  "rendered nothing", were one `VkDescriptorBufferInfo` declared inside the branch whose address was
  used after the branch closed. Both configurations are cheap; the rule in section 4 already requires
  both, so this is the *reason*, not a new obligation.
- **Documentation is coverage-enforced.** Every public entity under `src/RenderGraph/` needs a
  Doxygen description or the docs build fails.

```bash
# regenerate HTML/XML + docs/RenderGraph-api.md (fails on undocumented public API)
cmake --build build --target render_graph_docs
# verify only: coverage + checked-in markdown is current
python3 scripts/render_graph_docs.py --check
```

- Doxygen only parses the files listed in `docs/Doxyfile.render_graph` (currently
  `src/RenderGraph/*.h`, `docs/RenderGraph.md`, `docs/DescriptorSet-Lifecycle-Design.md`). Markdown
  inside that set may **only** link to other files in the set — add the file to the `INPUT` list in
  the Doxyfile *and* to `scripts/render_graph_docs.py` if you need a new page in it, otherwise the
  docs build fails on an unresolvable reference.
- Designs that are not implemented yet live in `docs/*-Design.md` and are explicitly kept out of
  the API reference.

## 4. Testing

Two configurations, both must build and pass:

```bash
cmake -S . -B build        && cmake --build build    --target tests -j"$(nproc)" && ./build/tests
cmake -S . -B build-rt -DFEATURE_RAY_TRACING=ON \
                           && cmake --build build-rt --target tests -j"$(nproc)" && ./build-rt/tests
```

Operational notes, each of which has cost someone a red herring:

- **`tests` does not depend on the `Shaders` target.** Build both — `cmake --build build --target
  tests Shaders` — or the tests fail at runtime on missing `.spv` files (they are loaded from
  `shaders/` relative to the *working directory*, not the source tree).
- **Shader lists are CMake `GLOB`s, evaluated at configure time.** An existing build directory does not
  see a newly added `.slang` file until it is reconfigured, so that one case fails with `Failed to open
  file` while everything around it passes — and the fix is `cmake -S . -B build`, not the shader. Both
  sessions on the specialization-constants PR hit it inside an hour, one of them after *adding* the shader
  and building successfully in a different directory.
- **`--clean-first` deletes the generated `.spv` files too**, so this trap also fires in an *existing*
  build directory, not just a fresh one: `cmake --build build --target tests --clean-first` (used to
  count warnings from a known state) empties `build/shaders/`, and the next run fails ten shader-loading
  cases that look exactly like a regression in whatever was just merged. It was reported as one. Rebuild
  `Shaders` after any `--clean-first`, or count warnings without it.
- **`assets/` is also CWD-relative.** Run from the build directory and link the assets in once:
  `ln -s "$PWD/assets" build/assets`. A missing assets dir fails as `SetData(nullptr)` deep in the
  Mazda fixtures, which looks like a code bug and is not.
- **The Vulkan SDK must be on the environment** (`source <sdk>/setup-env.sh` sets `VULKAN_SDK`,
  `PATH`, `VK_LAYER_PATH`, `LD_LIBRARY_PATH`): without it there is no `slangc`/`glslangValidator`
  and no validation layer. On a machine without Wayland dev packages configure with
  `-DGLFW_BUILD_WAYLAND=OFF`; the X11 path needs `libxrandr-dev`, `libxinerama-dev`,
  `libxcursor-dev`, `libxi-dev`.
- **An unknown `CMAKE_BUILD_TYPE` configures successfully with no per-config flags.** `Release`
  misspelled (`rel`, `RELEASE` is fine but `Rel` is not, and so on) yields a build with **no `-O3`
  and no `-DNDEBUG`** that looks ordinary in every other way - asserts stay live and the optimiser
  is off, which silently invalidates both halves of a Release verification. Pass the full
  `Debug`/`Release` strings (beware tag-slicing like `${tag##*-}` when looping over build dirs) and
  confirm `NDEBUG` in `compile_commands.json` before trusting a Release-only claim.
- **ThreadSanitizer is a third configuration, not another sanitizer flag** (`-DCMAKE_BUILD_TYPE=Thread`;
  TSan and ASan cannot share a binary). Its flags are generator expressions in `CMakeLists.txt`, so the
  missing per-config defaults above do not apply - `-O1` and `-g` are stated there on purpose. Under
  this configuration the `tests` target carries `CROSSCOMPILING_EMULATOR "setarch;<arch>;-R"`, which
  is what lets `catch_discover_tests` execute the binary at build time; `ctest` applies the same prefix
  when it runs a case. Invoking `build-tsan/tests` by hand needs `setarch $(uname -m) -R` in front, or
  it dies with `FATAL: ThreadSanitizer: unexpected memory mapping` before running anything. See §5 for
  the scope of `scripts/sanity.sh thread`.
- **Selecting cases:** Catch2 specs are exact-match unless they contain a wildcard - a plain
  `"GPU frustum culling"` matches nothing (the case is `RenderGraphBuilder: GPU frustum culling
  (async compute queue)`); use `"*GPU frustum culling*"`. Multiple specs take comma-separated
  wildcards in one argument: `./tests "DIAG*,*read-write*"`. A comma **inside** one test's name splits the spec there too -
  `"*named, not dereferenced*"` ran two unrelated patterns and silently selected a different test,
  making a teeth mutation look like a pass during the #85 review; reword the wildcard to avoid a
  comma that is part of the name.
- **The debug callback asserts on `ERROR`**, so a validation error aborts the *whole run* at that
  point, not just the case. To see the remaining failures, judge from the stderr VUIDs and re-run
  selected cases by name.

- **CI exists, and it gates what a software device can prove.** `.github/workflows/ci.yml` runs on
github-hosted runners with mesa's lavapipe as the Vulkan device: both configurations build, the
checked-in reference matches the headers (`render_graph_docs.py --check`), the changed lines are
`clang-format` clean, and the whole suite passes in Debug, Release, both ray-tracing shapes and under
the sanitizers - with the validation layer on. The workflow's header states the two axes it does **not**
cover, and the job writes them into the run summary on every build:

  - **Queue topology.** lavapipe reports one queue family with one queue, so no cross-family ownership
transfer and no dedicated compute queue is exercised; the suite takes the single-queue path. The
`[test device]` line says so on every run.
  - **Driver-specific behaviour.** A software device passing is not evidence about a driver.

Those two stay with the development machine and the review machine. **A green check is not a substitute
for the hardware run in section 6**, and it never was: a clean run is evidence about a *code and
validation-layer* pair on a *software device*, which is why the job records the device line and the layer
version rather than just a tick. On a pull request, treat CI as the thing that rules out the cheap
classes (a broken build, a stale reference, an unformatted line, a validation error) before a human or a
GPU is spent on it. [#22](https://github.com/v3c70r/Muyo/issues/22) tracks what a hardware runner would
add; the workflows that used to sit here unrun (2021, `actions/checkout@v2`, SDK 1.3.211) are deleted
rather than left as configuration nothing executes.
- **Tests are GPU-dependent.** Run them locally on a GPU. Validation is enabled in debug builds and
  the debug callback asserts on `ERROR` severity, so a validation error fails a test.
- **A regression test must have teeth.** Before claiming a test covers a bug, confirm it *fails*
  without the fix — revert the fix, or check out the revision that had the bug. For example, the
  "read-write attachment keeps earlier content" test was confirmed to fail on `46307ba`.
- **Report the counts** in the PR, e.g. `89 assertions / 10 cases` (default) and
  `109 assertions / 12 cases` (RT). Counts drift as tests are added; the point is that they are
  reported, and that a changed count is explained rather than silently absorbed.
- **Record what the tests ran on.** A verification table names the *device*, not the vendor:
  `llvmpipe`, `RADV REMBRANDT`, `RTX 3090`. The same commit has run green on a software rasteriser
  and on a real driver, and naming the vendor described neither correctly. `GraphicsTestEnv` logs the
  selected device and whether a dedicated compute family is present, so a run states its own scope
  instead of leaving it to be assumed.
- **Plan tests across the configuration axes, and say which ones each covers.** "It passes" has
  repeatedly meant less here than it sounded, because the axes differ silently:

  | Axis | Why it matters |
  | --- | --- |
  | Device | a software device (llvmpipe) passes everything and exercises no driver behaviour. |
  | Queue topology | one family takes the single-queue path and cannot validate handover machinery. |
  | In-flight count | 1 serialises; overlap, aliasing and chaining only appear above 1. |
  | Build configuration | `NDEBUG` removes asserts, and a Release build currently fails tests Debug passes. |
  | Resource sharing | per-execution vs shared resources decide whether a change can alias anything. |

  A test whose subject is one of these must state the axis it needs and fail or report loudly when the
  machine does not provide it — a green run on a configuration that cannot exercise the subject is not
  evidence about it. Where a contract matters, prefer a device-independent test of the mechanism (a
  hand-built plan, a direct call) over relying on the topology, the in-flight count or the build
  configuration being present.
- **Coverage must not depend on the machine.** A contract that only holds under one topology gets a
  device-independent test — a hand-built plan, a direct call — alongside any integration test. Where
  an integration test's *subject* is the topology, it must say so rather than pass quietly: a green
  run on a device without that topology is not evidence about the path.

Run `scripts/sanity.sh` before opening a pull request; it covers the documentation check and the
format check described below in one command. `scripts/sanity.sh warnings` is the enforcing form of
"no new warnings" and belongs in the same gate.

## 5. Static analysis and sanity checks

`scripts/sanity.sh` is the local entry point. Nothing it reports is required to pass **yet** — the
tooling is adopted in passes, so the current state is visible rather than discovered late.

| Tool | Available | State |
| --- | --- | --- |
| `clang-format` 18 + `clang-format-diff` | yes | The one-time pass ([#23](https://github.com/v3c70r/Muyo/pull/29)) landed: all files under `src/` conform (verified with 18.1.3). Keep new and edited lines clean via `scripts/sanity.sh format`. |
| `clang-tidy` | **no** | `.clang-tidy` is a borrowed google-cloud-cpp config (`WarningsAsErrors: "*"`, C++14-era rationale) that has never been run here. See [#24](https://github.com/v3c70r/Muyo/issues/24). |
| `-Wextra` | yes | On for the project's targets, with `-Wno-missing-field-initializers`. The exclusion is required by section 3's initialisation rule, not noise tolerance: the check fires on `{VK_STRUCTURE_TYPE_...}` and on partial designated initialisers, which is the style this repository mandates (3824 of 3942 `-Wextra` warnings were that one check when it was measured). Vendored and SDK include directories are `SYSTEM`, so their warnings are not attributed to our translation units. Remaining warnings are owned: [#169](https://github.com/v3c70r/Muyo/issues/169) and [#186](https://github.com/v3c70r/Muyo/issues/186), plus one diagnostic inside the vendored tinygltf target. `scripts/sanity.sh warnings` compares the changed files against `scripts/warning-baseline.txt` and fails if one gains a warning; files the branch did not touch only produce a note. |
| Clang static analyzer (`scan-build`) | yes | Not yet baselined: `scripts/sanity.sh static`. |
| GCC `-fanalyzer` | yes (gcc 13.3) | Not yet baselined: `scripts/sanity.sh analyzer` (report only; separate from the `warnings` gate). |
| Sanitizers (ASan + UBSan) | yes | `-DCMAKE_BUILD_TYPE=Sanitize`; suppressions in `san.supp`. **UB aborts**: `-fno-sanitize-recover=undefined` is set, without which a `runtime error:` prints and the run still exits 0. Runtime options live in `sanitizer_env` in `scripts/sanity.sh` and are duplicated in `.github/workflows/ci.yml` on purpose. |
| ThreadSanitizer | yes | `-DCMAKE_BUILD_TYPE=Thread`. `catch_discover_tests` executes the binary at *build* time and TSan cannot start on a kernel that randomises mappings beyond its shadow layout (`FATAL: ThreadSanitizer: unexpected memory mapping`, kernel `vm.mmap_rnd_bits`), so `CMakeLists.txt` sets `CROSSCOMPILING_EMULATOR "setarch;<arch>;-R"` on `tests` for that configuration: without it the build stops at discovery with `Result: 66` and deletes the target, before any test runs. `scripts/sanity.sh thread` runs the **device-free** subset only (the cases tagged `[CaptureTool]`, `[TransientMemoryPlanner]`, `[RenderGraphTrace]`, `[PSODesc]`, `[LaneLayout]`) and states the case count with the report count, because a driver is uninstrumented and a clean device-case run is not a claim about the device path. Report-only until the baseline is enforced ([#183](https://github.com/v3c70r/Muyo/issues/183)); adoption is [#173](https://github.com/v3c70r/Muyo/issues/173). |
| MemorySanitizer | **no, and not planned** | It needs every dependency instrumented, including libc and the Vulkan driver. Uninstrumented code produces false positives, and a GPU driver cannot be rebuilt. |
| Vulkan validation layers | always | Enabled in debug builds; the callback asserts on `ERROR`, so a validation error fails a test. |

Rules:

- **Format what you touch, not the world.** `scripts/sanity.sh format` checks only the lines a
  branch changed (via `clang-format-diff`), so a file that predates the branch never blocks work.
  The one-time whole-tree pass is [#23](https://github.com/v3c70r/Muyo/issues/23) and must be a
  standalone PR with no functional changes (use `.git-blame-ignore-revs` so it does not bury
  history). **A file the branch *adds* is invisible until it reaches the index:** the check diffs
  `origin/master` against tracked state, so a brand-new `.h` or `.cpp` sits outside it and the local
  verdict is clean while CI's is not. That is how an entire new header arrived at CI unformatted, with a
  local `sanity: OK` quoted beside it. Two habits follow, and one sitting needed both: stage new files
  before believing a local format pass, and after applying the formatter **re-run the check with a clean
  working tree**, so the verdict is about the committed state rather than the working tree that produced
  it. Staging only part of what the formatter touched is a second way to pass locally and fail in CI -
  it happened in the same fix, because the formatter had reflowed a second file.
- **Warnings are compared, not eyeballed.** `scripts/sanity.sh warnings` rebuilds the whole Debug
  configuration with the project's flags and compares the *changed* files against
  `scripts/warning-baseline.txt` (per-file counts). A changed file that gains a warning fails; a
  pre-existing warning in a file the branch did not touch is printed as a note and cannot block it. The
  recorded counts are owned ([#169](https://github.com/v3c70r/Muyo/issues/169),
  [#186](https://github.com/v3c70r/Muyo/issues/186)): raising one needs the owning issue in the commit
  that raises it, and a count that falls should be re-recorded rather than left stale. The counts are
  whole-build, so the target uses `--clean-first` - an incremental build reports warnings only for the
  translation units it recompiles, which made the first version of this check vacuous. As with the
  format check, a new file is invisible to it until it is staged.
- **Run the sanitizers before merge for anything touching memory, lifetimes, threading or resource
  ownership** — `scripts/sanity.sh sanitize`. It is the cheapest real-bug detector available today.
- **A sanitizer diagnostic must fail the run, not decorate it.** Until 2026-10-19 the Sanitize build passed
  `-fsanitize=undefined` without `-fno-sanitize-recover=undefined`, whose default is to *recover*: a signed
  overflow printed `runtime error:` and the process exited **0**. Measured on this tree with a planted
  overflow: `exit=0` and `All tests passed (1 assertion in 1 test case)` with the old flags, `exit=1` with the
  new one. So every earlier claim of "0 UBSan errors" was a claim about reading stderr, not about an exit
  status — and a test that asserts the absence of a *line* is weaker than a process that cannot succeed.
  `scripts/sanity.sh sanitize` now checks both the exit status **and** the diagnostic count, and prints the
  options it used, because a local run and a CI run have to be the same experiment.
- **Sanitizers are scoped to this repository's targets.** `add_compile_options(${SANITIZE_FLAGS})` sits after
  the vendored targets (`imgui`, `SPIRV-Reflect`, `tinygltf`, `stb`, ...), so UB inside a dependency cannot
  abort our run. It also means a sanitizer report is always about our code — which is what makes "fix the
  finding" the right response to one.
- **Some checks are rejected for a measured reason, and the reason is recorded where the flags are.** Pointer
  pair checking (`-fsanitize=pointer-compare,pointer-subtract` +
  `ASAN_OPTIONS=detect_invalid_pointer_pairs=2`) aborts inside `vk_mem_alloc.h`: VMA's intrusive list compares
  pointers that are not in the same object. VMA is a header compiled into our translation units, so the check
  cannot be scoped away from it. See the comment in `CMakeLists.txt`. A rejected check with a measurement
  beats an adopted one that forces `detect_leaks=0`.
- **Read `scripts/sanity.sh`'s exit status, not a grep of its output.** The script ends with a single
  `sanity: OK` / `sanity: FAILED` line, but the rule is the exit status: each check prints its own red
  message, they share no token, and a grep for `OK` will happily match a passing check while the
  failing one scrolls past. Two sessions recorded "format clean" from `./scripts/sanity.sh 2>&1 |
  grep -E "OK|FAILED"` over a run that exited 1 — worse than not checking, because a claim got made
  and the reviewer then spent a round disproving it (on the same clang-format the developer had).
  Distrust a filtered verdict generally: piping a check into `grep` throws away the exit status unless
  `PIPESTATUS` is read, so the pipe can only ever confirm, never deny.
- **Never enable a new check globally on the first run.** Add it, measure the fallout, file the
  cleanup as an issue, and only then enforce — scoped to changed lines. [#24](https://github.com/v3c70r/Muyo/issues/24)
  (clang-tidy) and [#25](https://github.com/v3c70r/Muyo/issues/25) (`-Wextra`) are written up that way.
- A new warning in code you touched is a review finding, not noise. "No new warnings" is part of
  the definition of done.

## 6. Review process

The review session has **no access to the developer's context**. Write every PR to be reviewed
cold.

### What a ready PR states

What changed and why; what is explicitly **not** changed; known limitations and debt; verification
(build configs, test counts, docs check, GPU and driver used); files touched; the follow-up issues
filed. [`.github/pull_request_template.md`](.github/pull_request_template.md) mirrors this list, so
it does not have to be remembered.

### Findings: kind, confidence, acceptance criterion

Findings are numbered so they can be referenced later:

| Prefix | Kind |
| --- | --- |
| `C<n>` | Correctness — wrong results, races, leaks, UB. |
| `A<n>` | Architecture or documentation — structure, missing docs, deferred design. |
| `N<n>` | New issue raised while reviewing a fix (usually round two). |
| `P<n>` | Polish — naming, comments, small cleanups. |

The prefix says *what kind* of finding it is. Every finding also states **how sure** the reviewer is
and **how it is closed**:

| Confidence | Meaning |
| --- | --- |
| `confirmed` | Deterministic from reading the code; no run needed to know it is real. |
| `needs runtime validation` | Derived by reasoning or arithmetic; a run must settle it. |
| `design preference` | Argue or drop. Never blocks a merge. |

The **acceptance criterion** is the test, command or observable that closes it — "two writers, one
target; prove the new test fails on the old revision". A finding without one is an opinion.

The confidence label tells the fixer what *kind* of work settles it. Arithmetic findings (like the
descriptor-pool exhaustion in the PR #9 review) are `needs runtime validation` even when they look
certain, and a fix may legitimately resolve them structurally rather than by measurement — but say
so.

### Multi-round reviews

A second round starts from the first round's finding list and produces an **item-by-item verdict**
(`fixed` / `partial` / `open` / `new`). Two checks earn their keep:

- **Over-correction.** Verify the fix did not cost a property the code already had. A WAW hazard fix
  that also barriers read-after-read pairs trades a correctness bug for a performance cliff.
- **New-issue hunt.** Read the fix diff as if it were a fresh PR. Several findings in this
  repository were *introduced by the fix commit*, not present before it.

### Mechanical claims get mechanical verification

Any claim of the form "all X are Y" — "all synchronization migrated", "every enabled feature is
verified" — is closed with a copy-pasteable command, not by re-reading:

```bash
grep -rn "vkCmdPipelineBarrier(" src/          # expect: no matches
python3 scripts/render_graph_docs.py --check   # expect: coverage OK
```

Check symmetric claims in **both** directions: nothing enabled-but-unchecked, nothing
checked-but-unenabled. The `offsetof` dump in section 8 follows the same principle — *the command is
the evidence*.

### Reviewer conduct

"Evidence over assertion" binds reviewers too.

- **Check the artifact, never the memory.** A reviewer's claim about a struct layout, driver
  behaviour or spec clause is settled against the headers and toolchain in this repository, not
  recalled. There is a worked example: a reviewer asserted a `VkMemoryBarrier2` field order from
  memory, measurements contradicted it, and the measurements were right.
- **Post the correction in the thread.** PR comments are the durable record. A wrong reviewer claim
  left standing outranks a right one merely by being first, and future readers will cite the error.

### Reviews without the hardware

Static review — no GPU — is a legitimate mode, but its conclusions are conditional. Close every such
review with a **Questions / Unknowns** list: what static inspection cannot determine, and *which
tool settles it* — validation layers, GPU-assisted validation, RenderDoc, a second vendor, a
different queue-family topology. The fixer works that list item by item and reports back per item.

### The reviewer's hardware runbook

The counterpart to the section above: how a reviewer with a GPU verifies a PR end to end. This is
the sequence used on the render-graph PRs; a fresh session can repeat it without rediscovery.

**Workspace.** One worktree per PR under `/tmp`, built and run there; the developer's checkout is
never touched, and nothing is ever pushed from a review worktree. Every local patch —
instrumentation, mutations, workarounds — is reverted before finishing (`git status --porcelain`
reports clean) and disclosed in the review comment.

**The standard matrix**, in a Debug build of both configurations:

```bash
source /path/to/vulkansdk/setup-env.sh                    # SDK: slangc, layers, loader
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGLFW_BUILD_WAYLAND=OFF
cmake --build build --target tests Shaders -j"$(nproc)"
ln -sfn "$PWD/assets" build/assets
( cd build && ./tests >run.out 2>run.err )
grep -E "All tests passed|test cases:" run.out            # reproduce the claimed counts
grep -oE "#(VUID-[A-Za-z0-9-]+)" run.err \
    | grep -v "VUID-vkWaitForFences-pFences-parameter" | wc -l
                                                          # validation errors: expect 0. The
                                                          # exclusion is standing, not noise: the
                                                          # DebugCallback fork test (added with the
                                                          # #52 roadmap merge) has a child that
                                                          # deliberately trips that one VUID and
                                                          # inherits the parent's stderr, so a clean
                                                          # run reports 2 before the exclusion.
grep -c "\[WARNING\]" run.err                            # >0 proves the layer is delivering
grep "\[test device\]" run.err                           # what this run actually proved
```

A zero warning count is suspicious, not clean — it can mean validation is not delivering messages
at all (the `assert()`-gated messenger bug did exactly that, silently, in Release). Read the
`[test device]` line before treating green as evidence about the cross-queue path.

**Record the validation layer's version beside the counts, because a clean run is evidence about a
*code + layer* pair and not about the code alone.** The specialization-constants PR is the worked
example: `VUID-VkBufferMemoryBarrier2-dstAccessMask-03917` is enforced over the `ALL_COMMANDS` expansion
by layer 1.4.363 and not by 1.4.313, so the same commit aborted every configuration on one machine and
was clean on the other. Neither run was wrong; only one was seeing the barrier the code emits. The spec
version is already in the VUID text the run prints (`.../view/1.4.363.0/...`), so it costs nothing to
quote — and "no validation errors" without it is a claim about an unnamed pair.

**Tooling without root.** Ubuntu packages extract locally:
`apt-get download <pkg> && dpkg-deb -x <pkg>.deb <dir>`, run with
`LD_LIBRARY_PATH=<dir>/usr/lib/x86_64-linux-gnu`. Needed for: `clang-format-18` (the version
matters — include-block handling differs across versions and the format verdict can flip), and
`doxygen` plus `libfmt9` (the reference is generated with 1.9.8; see `docs/README.md`).

**Teeth, attribution, flakes.**

- *Absence claims:* a grep that finds no guard, no pattern or no count proves nothing about the
  forms it did not search for. Platform guards come in at least two idioms (`#if defined(_WIN32)`
  ... `#else`, and `#if defined(__unix__)` ... `#else`) - calling a file "unguarded" after grepping
  only one manufactured a false blocking finding on #61, and a count whose pattern required a
  leading `//` was off by one on #52. An absence claim names every pattern it ruled out.

- *Teeth:* revert the fix — or mutate the guard it added — in the local worktree, rebuild, and
  watch the test fail with the expected symptom. Revert the mutation afterwards.
- *Attribution:* when a defect is found, build the PR's base commit the same way and run the same
  case. Pre-existing versus introduced is a fact, not an impression, and it changes the triage
  from "fix in-branch" to "file an issue".
- *Flakes:* a failure seen once gets repeated runs to bound the rate before it is reported:
  "observed once in N runs, VUID …" — with the unreproducible failure still recorded, because it
  is data (the push-constant flake in the #37 review pointed at the silent shader-load skip).

**Instrumentation.** Temporary `fprintf` tracing in the code under review — plan segments,
destroyed-semaphore counts, executed node names — settles "which path did this take" in one run.
Instrument, run, revert, and quote the trace in the review: the trace is the evidence, and it
doubles as coverage proof ("this test exercises X" is checked by counting X in the trace).

**Assert-related claims need a Release build.** Anything justified by `NDEBUG` — "fails loudly in
every configuration", "the guard fires" — is verified in a build where `-DNDEBUG` is actually on
the command line, confirmed via `compile_commands.json`.

**Pure-refactor equivalence.** For a "no behaviour change" claim over many files, compare
whitespace-stripped, comment-stripped, macro-continuation-folded content with `#include` lines
removed. For include reordering, additionally check that the include *multiset* is unchanged and
that no include crossed a `#define`/`#if`/code boundary — the one way `SortIncludes` can change
what is defined when. The format-pass review verified 107/107 files this way.

### Triage and merge

**The merge gate is a repository ruleset, and it is worth knowing exactly what it enforces** — as of
2026-10-12, `master: PR + review, squash only`, active, with **no bypass actors**:

| Enforced | Not enforced, and the manual rule that covers it |
| --- | --- |
| A pull request, from anyone including an administrator | **CI is not a required check**, so a PR can merge with a red run. Read the checks before merging; the ruleset will not stop you. |
| **One approving review**; **squash-only merge**, enforced since 2026-10-12 (`allowed_merge_methods = [squash]`, which is what the ruleset's name already claimed) | No linear-history requirement, so the squash is a squash by the merge method and not by the shape of the history. |
| No deletion, no force-push to `master` | **Stale approvals are not dismissed on push**, and the last push does not need its own approval — so a fixup pushed after review keeps the approval it invalidated. The section below already requires a re-check; the gate does not. |

Two identities, and now they are real ones: work is authored by `v3c70r` and reviewed by
**`qgu-reviewer`**, which is a separate account so that an approval is a GitHub *state* rather than a
comment saying "approve". That is what makes `gh pr view <n> --json reviewDecision` worth reading, and it
is why the reviews before 2026-10-12 read as comments: a single account cannot approve its own pull
request.

**The author and the approving reviewer must not be the same actor, and the approval must be submitted as a
review.** Both halves were paid for, and both are checkable.

- **A comment is not an approval.** Writing "approve" in the thread leaves `reviewDecision` at
  `REVIEW_REQUIRED`, because the gate reads the review *state* and not the text. [#94](https://github.com/v3c70r/Muyo/pull/94)
  was refused for exactly this — `HTTP 405 … At least 1 approving review is required by reviewers with
  write access` — with a complete review already posted as a comment. GitHub cannot convert a comment into
  a review, so the remedy is a re-submission rather than an edit:

  ```bash
  gh pr review <n> --approve --body "<the review text>"   # from the reviewing account
  gh pr view <n> --json reviewDecision                    # expect APPROVED; a comment leaves REVIEW_REQUIRED
  ```

- **The reviewer must be a different account from the author.** GitHub will not let an author approve their
  own pull request, so this is enforced rather than trusted — but the enforcement is not the point. A review
  written by the author in the PR thread, however honest, is a self-review, and the reason the second
  account exists is to make one impossible rather than to make it impolite. A session that both wrote the
  branch and reviewed it has produced no review, whatever the thread says.

- **The two identities are also two git emails.** GitHub attributes authorship by commit *email*, so a
  commit authored with the reviewer account's address puts the reviewer inside the author set - and the
  gate then refuses their approval with the same message it gives for a missing review. [#99](https://github.com/v3c70r/Muyo/pull/99)
  hit it from both ends at once: approving as the author was refused because the author opened the pull
  request, and approving as the reviewer was refused because the branch's commits carried `qgu@qgu.io`,
  which is registered to the reviewer account. **Both identities were disqualified by one config field,
  and the message named neither.** The author's commits are authored with the author account's
  `users.noreply` address - `git commit --amend --reset-author` fixes an existing branch, and feature
  branches are not gated - and no branch the reviewer is expected to approve should carry the reviewer's
  address. Which account may produce the approving *state* is decided here rather than by the ruleset:
  the ruleset decides only that a state is required.

- **Which account a `gh` call uses is session state, not an argument.** The active account is persistent
  config (`gh auth switch`), every subsequent `gh` call uses it, and the output of a *successful* call does
  not name who posted it. So a review can be complete, honest, and posted as the wrong actor — and the gate
  then refuses it as a *missing* review rather than as one from the wrong identity, which reads like a bug in
  the gate. Read the identity back the same way the branch is read back, and do it before the call:

  ```bash
  gh auth switch --user qgu-reviewer   # once per session; the setting persists
  gh api user --jq .login              # names who the next state-changing call will post as
  ```

  #94 is the worked example, and it was three layers deep in one evening. The review was first posted as the
  **author's** account, because that account was still active from earlier work in the same session; the
  delete then failed twice, having passed a GraphQL `IC_…` node ID to the REST delete endpoint, which wants
  the numeric one; and only the re-submission produced a review. Every one of those calls succeeded at being
  *a* call. The later slip — an approving *comment* instead of an approving *review* — is the same shape one
  layer up, and it happened again on the pull request that documents it.

The check is the evidence, so run it rather than recalling it:

```bash
# No pull request may be approved by its own author. Expect no output.
gh api graphql -f query='{repository(owner:"v3c70r",name:"Muyo"){pullRequests(first:100,states:[OPEN,CLOSED,MERGED]){nodes{number author{login} reviews(first:30){nodes{state author{login}}}}}}}' \
  --jq '.data.repository.pullRequests.nodes[] | . as $pr | select([$pr.reviews.nodes[] | select(.state=="APPROVED" or .state=="CHANGES_REQUESTED") | .author.login] | index($pr.author.login)) | "self-reviewed #\($pr.number)"'
```

Run on 2026-10-13: 47 pull requests, **no self-reviewed one**, and 42 merged before the gate whose approval
survives only as a comment — which is why those old merges cannot be re-audited as approvals, only read as
the review that happened.

Triage **every** finding and say which you did:

- **Fix it in-branch** when it is in scope and cheap.
- **Otherwise file an issue** on the board and reply with the link. Deferring is fine; ignoring is
  not.
- **"Marked, not fixed"** is a legitimate outcome — for example when fixing would change
  synchronization semantics that a refactor PR promised not to change. Leave a `TODO(<phase>)`
  marker and reference the issue.

**Who reproduces the verification:** the developer reproduces before merge; a reviewer with the
hardware re-runs when available. A static-only reviewer is never the sole confirmation of a runtime
claim — test counts, validation cleanliness, dedicated-compute-queue behaviour.

Merge only when the review is approve-worthy, the fixups are pushed, and the verification table has
been reproduced on the merged content.

## 7. Definition of done

- [ ] Both configurations compile with no new warnings.
- [ ] Tests pass in both configurations, and the counts are reported.
- [ ] `scripts/sanity.sh` passes (docs check + formatting of changed lines).
- [ ] `scripts/sanity.sh sanitize` is run for changes touching memory, lifetimes, threading or
      resource ownership.
- [ ] New behaviour has a test with teeth, or the PR explains why a test is impractical.
- [ ] Deferred work is filed as issues and referenced from the code and the PR.
- [ ] `AGENTS.md` is updated if the process itself changed.

## 8. Environment notes

- **Struct layout is not what you assume.** On the Vulkan headers used here, the sync2 barriers do
  not follow the order a positional initializer implies. Verified with `offsetof`:

  ```
  VkMemoryBarrier2:    size=48 sType=0 pNext=8 srcStage=16 srcAccess=24 dstStage=32 dstAccess=40
  VkImageMemoryBarrier2:        srcStage=16 srcAccess=24 dstStage=32 dstAccess=40
  ```

  This is precisely why positional initialization is banned, and why the rule is "verify the
  layout, never assume it" rather than "trust the specification order".
- The development GPU exposes a **dedicated compute queue family** (graphics family 0, compute
  family 1), so the async-compute path and queue-family ownership transfers are exercised for real.
  This depends on the driver initialising: when it does not, Vulkan silently falls back to a software
  device (llvmpipe) with a single family and **nothing about the transfer path is validated** - a
  green run looks identical either way. `GraphicsTestEnv` prints a `[test device] ...` line each run
  saying which case it was, so read that line before treating a green run as evidence about
  transfers. A machine with one queue family takes the single-queue path, which is a legitimate
  configuration rather than a failure, but it cannot validate handover machinery.
- **AMDVLK ships an *implicit* layer that replaces device enumeration, not just adds a driver.** With
  `VK_LAYER_AMD_switchable_graphics_64` active (its manifest is in `/etc/vulkan/implicit_layer.d/`, so the
  loader enables it for every application), `vulkaninfo` and every app see **only AMDVLK's device** — RADV
  and llvmpipe disappear from the list, and `devices[0]` stops being RADV. Verified here: without it three
  devices enumerate (RADV, AMDVLK, llvmpipe) and RADV is first; with it, one.

  The session sets `DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1=1` in `~/.config/environment.d/`, so RADV stays
  the default for the desktop and for shells after a re-login. **Select AMDVLK explicitly when it is wanted**
  — it is the only device here with `VK_KHR_cooperative_matrix`:

  ```bash
  VK_DRIVER_FILES=/etc/vulkan/icd.d/amd_icd64.json <program>     # VK_ICD_FILENAMES is the deprecated alias
  ```

  Two consequences for verification. A re-run of a PR whose table says `RADV REMBRANDT` will report AMDVLK
  until the environment applies, and that is the environment rather than a regression — read the
  `[test device]` line. And the cooperative-matrix property sets **differ by device**: AMDVLK offers
  `F16/F16->F32 M=16 N=8 K=16` shapes (16x16x16 among them), llvmpipe offers `M=8 N=8 K=8`. A test that
  assumes a shape is testing the device, not the code.

  The cleaner fix is to remove the layer registration itself, which needs root and covers processes that do
  not inherit the user environment: `sudo mv /etc/vulkan/implicit_layer.d/amd_icd64.json{,.disabled}`.
- `thirdparty/*` submodules frequently show as dirty. Do not commit submodule pointer churn unless
  the pointer change is intentional.

## 9. Key documents

| Document | Contents |
| --- | --- |
| `AGENTS.md` | This file — process, verification, review. |
| [`scripts/sanity.sh`](scripts/sanity.sh) | Local sanity runner: docs, format, clang-tidy, static analysis, sanitizers. |
| [`.github/pull_request_template.md`](.github/pull_request_template.md) | Mirrors section 6 — what a ready PR states. |
| [`.clang-format`](.clang-format) / [`.clang-tidy`](.clang-tidy) | Formatting and static-analysis configuration (see section 5). |
| [`docs/CodingConventions.md`](docs/CodingConventions.md) | Repository-wide code rules. |
| [`docs/RenderGraph.md`](docs/RenderGraph.md) | RenderGraph concept, quick start, status/limitations. |
| [`docs/RenderGraph-api.md`](docs/RenderGraph-api.md) | Generated API reference (do not hand-edit). |
| [`docs/DescriptorSet-Lifecycle-Design.md`](docs/DescriptorSet-Lifecycle-Design.md) | Descriptor-set redesign (not yet implemented). |
| [`docs/README.md`](docs/README.md) | How to generate and check the docs. |
| [`docs/Roadmap.md`](docs/Roadmap.md) | The plan: goal, phases, gates, debt policy, risks. Reasoning lives here; state lives in the GitHub milestones and project fields — the doc's "Where the plan lives" section is the contract between them. |
| [`docs/RenderGraph-Library-Design.md`](docs/RenderGraph-Library-Design.md) | The library target architecture: the layer stack, per-concern boundaries, and decisions recorded with their triggers. |
| [`docs/Consumers.md`](docs/Consumers.md) | Who consumes the library and the guarantees each relies on — the checkable form of the boundary. |
| [Project board](https://github.com/users/v3c70r/projects/3) | Backlog, area, status, critical path, debt verdicts. |
