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
- Never commit directly to `master`.
- **Merge with squash**: `gh pr merge <n> --squash`. Trunk history is then one commit per
  workstream, and the PR body becomes the commit body — so write it to be read as `git log`.
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
  `Performance`, `Graph API`, `Adoption`.
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

- **There is no CI, by choice.** Testing happens on the development machine; the workflows in
  `.github/workflows/` are stale and have never fired — do not rely on them, and do not add
  anything that assumes a hosted runner. Revisit when a dedicated runner exists ([#22]); the
  build-only + docs job described there is the first thing to add.
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
- **Coverage must not depend on the machine.** A contract that only holds under one topology gets a
  device-independent test — a hand-built plan, a direct call — alongside any integration test. Where
  an integration test's *subject* is the topology, it must say so rather than pass quietly: a green
  run on a device without that topology is not evidence about the path.

Run `scripts/sanity.sh` before opening a pull request; it covers the documentation check and the
format check described below in one command.

## 5. Static analysis and sanity checks

`scripts/sanity.sh` is the local entry point. Nothing it reports is required to pass **yet** — the
tooling is adopted in passes, so the current state is visible rather than discovered late.

| Tool | Available | State |
| --- | --- | --- |
| `clang-format` 18 + `clang-format-diff` | yes | `.clang-format` exists (Google-based, Allman, 120 cols, 4-space) but the tree does not conform: 107 of 115 files under `src/` drift. |
| `clang-tidy` | **no** | `.clang-tidy` is a borrowed google-cloud-cpp config (`WarningsAsErrors: "*"`, C++14-era rationale) that has never been run here. See [#24](https://github.com/v3c70r/Muyo/issues/24). |
| Clang static analyzer (`scan-build`) | yes | Not yet baselined: `scripts/sanity.sh static`. |
| GCC `-fanalyzer` | yes (gcc 13.3) | Not yet baselined: `scripts/sanity.sh warnings`. |
| Sanitizers (ASan + UBSan) | yes | `-DCMAKE_BUILD_TYPE=Sanitize`; suppressions in `san.supp`. |
| Vulkan validation layers | always | Enabled in debug builds; the callback asserts on `ERROR`, so a validation error fails a test. |

Rules:

- **Format what you touch, not the world.** `scripts/sanity.sh format` checks only the lines a
  branch changed (via `clang-format-diff`), so a file that predates the branch never blocks work.
  The one-time whole-tree pass is [#23](https://github.com/v3c70r/Muyo/issues/23) and must be a
  standalone PR with no functional changes (use `.git-blame-ignore-revs` so it does not bury
  history).
- **Run the sanitizers before merge for anything touching memory, lifetimes, threading or resource
  ownership** — `scripts/sanity.sh sanitize`. It is the cheapest real-bug detector available today.
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

### Triage and merge

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
| [Project board](https://github.com/users/v3c70r/projects/3) | Backlog, area and status. |
