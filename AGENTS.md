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

- **Tests are GPU-dependent and there is no CI signal.** The workflows in `.github/workflows/` are
  stale (they have never fired on this repository) — do not rely on them. Run the tests locally on
  a GPU. Validation is enabled in debug builds and the debug callback asserts on `ERROR` severity,
  so a validation error fails a test.
- **A regression test must have teeth.** Before claiming a test covers a bug, confirm it *fails*
  without the fix — revert the fix, or check out the revision that had the bug. For example, the
  "read-write attachment keeps earlier content" test was confirmed to fail on `46307ba`.
- **Report the counts** in the PR, e.g. `89 assertions / 10 cases` (default) and
  `109 assertions / 12 cases` (RT). Counts drift as tests are added; the point is that they are
  reported, and that a changed count is explained rather than silently absorbed.

## 5. Review process

The review session has **no access to the developer's context**. Write every PR to be reviewed
cold.

A ready PR states: what changed and why; what is explicitly **not** changed; known limitations and
debt; verification (build configs, test counts, docs check, GPU/driver used); files touched; and
the follow-up issues filed.

Findings are numbered so they can be referenced later:

| Prefix | Meaning |
| --- | --- |
| `C<n>` | Correctness — wrong results, races, leaks, UB. |
| `A<n>` | Architecture or documentation — structure, missing docs, deferred design. |
| `N<n>` | New issue raised while reviewing a fix (often found in round two). |
| `P<n>` | Polish — naming, comments, small cleanups. |

Triage **every** finding, and say which you did:

- **Fix it in-branch** when it is in scope and cheap.
- **Otherwise file an issue** on the board and reply with the link. Deferring is fine; ignoring is
  not.
- **"Marked, not fixed"** is a legitimate outcome — for example when fixing would change
  synchronization semantics that a refactor PR promised not to change. Leave a `TODO(<phase>)`
  marker and reference the issue.

Reviewers verify claims rather than trusting them, so pre-empt it with evidence. When reviewer and
developer disagree, resolve it with a reproducible result on the actual toolchain — not with
authority, and not by repeating the claim.

Merge only when the review is approve-worthy, the fixups are pushed, and the verification table has
been reproduced on the merged content.

## 6. Definition of done

- [ ] Both configurations compile with no new warnings.
- [ ] Tests pass in both configurations, and the counts are reported.
- [ ] `python3 scripts/render_graph_docs.py --check` passes when `src/RenderGraph/` or its docs changed.
- [ ] New behaviour has a test with teeth, or the PR explains why a test is impractical.
- [ ] Deferred work is filed as issues and referenced from the code and the PR.
- [ ] `AGENTS.md` is updated if the process itself changed.

## 7. Environment notes

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
  A machine without one will silently take the single-queue path.
- `thirdparty/*` submodules frequently show as dirty. Do not commit submodule pointer churn unless
  the pointer change is intentional.

## 8. Key documents

| Document | Contents |
| --- | --- |
| `AGENTS.md` | This file — process, verification, review. |
| [`docs/CodingConventions.md`](docs/CodingConventions.md) | Repository-wide code rules. |
| [`docs/RenderGraph.md`](docs/RenderGraph.md) | RenderGraph concept, quick start, status/limitations. |
| [`docs/RenderGraph-api.md`](docs/RenderGraph-api.md) | Generated API reference (do not hand-edit). |
| [`docs/DescriptorSet-Lifecycle-Design.md`](docs/DescriptorSet-Lifecycle-Design.md) | Descriptor-set redesign (not yet implemented). |
| [`docs/README.md`](docs/README.md) | How to generate and check the docs. |
| [Project board](https://github.com/users/v3c70r/projects/3) | Backlog, area and status. |
