# Roadmap: a visualized snapshot

**Snapshot:** 2026-10-22, master `88bcd6c`. The execution-bindings design note is open in PR #208 at the
time of writing; it is referenced here, not assumed merged.

**Authority:** phase membership is the six GitHub milestones; an issue's state is its `Status`,
`Critical path`, `Depends on` and `Debt` fields on the [project board](https://github.com/users/v3c70r/projects/3);
the reasoning is [`Roadmap.md`](Roadmap.md), and the consumer contract is [`Consumers.md`](Consumers.md).
This file holds none of that. It is a **view** plus the record of the reconciliation that produced it.
When the board moves, regenerate it - do not treat a diagram here as a commitment.

It is a separate file because `Roadmap.md` deliberately does not list phase members (its "Where the plan
lives" section); a diagram that listed them inside that document would recreate the duplication it warns
about. The view lives here so the contract there stays true.

---

## 1. Phases and gates

```mermaid
flowchart LR
    P0["P0 Frame synchronization<br/>10 closed · 0 open<br/>executor + timeline values"]
    P1["P1 Graph parity<br/>5 closed · 7 open<br/>capabilities + cost (#59)"]
    P2["P2 App adoption<br/>0 closed · 6 open<br/>helloVulkanRG proves the loop;<br/>helloVulkan not ported"]
    P3["P3 Deprecation<br/>0 closed · 1 open<br/>delete RenderPassManager"]
    L["L Library extraction<br/>7 closed · 7 open<br/>overlaps P1-P3"]
    P0 --> P1 --> P2 --> P3
    L -. constrains .-> P1
    L -. constrains .-> P2
```

| Phase | Gate (from the milestone description) | State |
| --- | --- | --- |
| **P0** | Executor runs N in-flight executions; completion is per-queue timeline values; no frame boundary on the execution path. Builder stays `inFlightCount == 1` (#40). | **Met** - 0 open. The milestone is still open as a grouping device; closing it is an admin decision. |
| **P1** | A scene renders through the graph, validation-clean, every capability (transfer/clear, transients, descriptors, pipeline caching) matching the legacy path; RT is a separate gate. The cost half is #59. | **In progress.** #18 ✅, #16 ✅, #15 cache ✅ but frame retention open, #17 open, #19 open (separate RT gate), #59 open. P1 cannot close without **#17 + #59**. |
| **P2** | `helloVulkan` renders through the graph; `RenderPassManager` is referenced only by its own files. Capability, not completeness. | **Started** (helloVulkanRG, increment 1: `FrameSync` + a UI node through the graph, #112/#113). `helloVulkan` itself is unchanged. #48/#49 must land before #51. |
| **P3** | `grep -rn "RenderPassManager" src/` is empty and `src/RenderPasses/` is gone; the diff is almost entirely deletions. | **Blocked on P2 + #48 + #49.** |
| **L** | A non-renderer consumer runs against the public surface only, with no `gap` row blocking it. | #53 (public surface, SPIR-V bytes) and #72 (cooperative matrix fetch) are the remaining blockers. #55 and #16 landed, so their `gap` rows are now `holds` (see R4/R5). |

The two "start it anyway" facts worth keeping in view: P0 is complete, and the Roadmap's own P2 risk text
says the condition for starting the migration is met; part of it has started, and that is not a reason to
delay the rest.

## 2. Critical path

```mermaid
flowchart TB
    subgraph consumer["First consumer chain"]
        C18["#18 transfer/clear ✅"]
        C16["#16 transients ✅"]
        C15["#15 descriptor cache<br/>cache ✅ · frame retention open"]
        C57["#57 FrameSync<br/>helper ✅ · windowed acceptance open"]
        C20["#20 helloVulkan port<br/>helloVulkanRG = increment 1"]
        C48["#48 DebugUI decoupling"]
        C49["#49 ShadowPassManager fold-in"]
        C51["#51 delete src/RenderPasses/"]
        C18 --> C16 --> C15 --> C57 --> C20
        C20 --> C48 --> C51
        C20 --> C49 --> C51
    end
    subgraph library["Library chain"]
        L53["#53 graph boundary<br/>remaining: header split, standalone, packaging"]
        L58["#58 device layer"]
        L54["#54 compute-only in-tree test"]
        L53 --> L58 --> L54
    end
    subgraph cost["Cost gate"]
        K197["#197 trace timings"] --> K59["#59 graph vs legacy<br/>method + tolerance"] --> G1["P1 closes"]
    end
    C20 -. both paths live .-> K59
```

`#59` is drawn across the consumer chain rather than in it: nothing waits for it, but P1 cannot close
without its number. Its measurement needs **both** renderers alive, so the window is after #20 and before
#51.

## 3. Where the next work is

```mermaid
flowchart LR
    subgraph now["Now"]
        N209["#209 graph-owned resource reuse<br/>(rebuild after resize)"]
        N17["#17 build caches<br/>shader+reflection → layouts → pipelines"]
        N209 --> N17
    end
    subgraph next["Next"]
        N20["#20 helloVulkan port<br/>increments after helloVulkanRG"]
        N48["#48 + #49<br/>unblock #51"]
        N20 --> N48
    end
    subgraph then["Then"]
        N59["#197 → #59<br/>cost measurement"]
        N83["#83 execution bindings<br/>(design in PR #208)"]
        N40["#40 in-flight > 1<br/>per-execution transients + descriptors"]
        N83 --> N40
    end
    subgraph parallel["Parallel (L track)"]
        L53["#53 → #58 → #54"]
        L73["#73 packaging · #87 versioning"]
    end
    N17 --> N20
    N20 --> N59
    N20 --> N83
    N17 --> G1["P1 gate: pipeline caching"]
```

Why this order:

1. **#209 + #17 first.** #17 is a named P1 capability, and #209 is the correctness it assumes: a rebuild
   that reuses a graph-owned resource by name keeps the old extent (`RenderGraphResourceDesc.h:54`).
   #209 is small; #17 is the cache that makes a rebuild cheap. Together they are the "solid step" that
   does not depend on the adoption.
2. **Then #20.** The Roadmap's P2 risk says the migration must not be deferred by one more graph feature;
   helloVulkanRG proves the loop, and the remaining work is porting `helloVulkan`'s passes and the two
   decouplings. #17 may still be running; the P2 gate is capability, not completeness.
3. **Then #197 → #59, and #83 → #40.** The cost criterion needs both paths, so it follows the port and
   precedes the deletion. Execution bindings and in-flight overlap are deliberately after adoption
   (PR #208), and #15's frame retention is the shared prerequisite.

## 4. Open issues, by area

Twenty-six issues are open. `CP` is the `Critical path` field; `Deps` is the `Depends on` field.

| # | Area | Milestone | Status | CP | Deps | Note |
| --- | --- | --- | --- | --- | --- | --- |
| 15 | Descriptors | P1 | In Progress | Yes | #53, #18 ✅ | Cache landed (#108/#109); frame retention, import path and `framesInFlight` remain. |
| 17 | Performance | P1 | Todo | **Yes** (R7) | - | Shader/reflection, layout and pipeline caches; P1 gate capability. |
| 19 | Ray tracing | P1 | Todo | No | - | RT hardening; separate gate. |
| 40 | Synchronization | P1 | Todo | No | #15, #83 (R6) | In-flight > 1; blocked on per-execution state, not slots. |
| 59 | Performance | P1 | Todo | Yes | #197 (R6) | Cost criterion; needs #197 and both paths. |
| 20 | Adoption | P2 | In Progress | Yes | #57 | helloVulkanRG is increment 1; `helloVulkan` still legacy. |
| 48 | Adoption | P2 | Todo | No | - | DebugUI decoupling; blocks #51. |
| 49 | Adoption | P2 | Todo | No | - | ShadowPassManager into graph; blocks #51. |
| 57 | Synchronization | P2 | In Progress | Yes | #15, #16 ✅ | `FrameSync` + helloVulkanRG resize landed (#110/#113). |
| 51 | Adoption | P3 | Todo | Yes | #20 | Deletion only. |
| 53 | Graph API | L | In Progress | Yes | - (R8) | Services injected; remaining: header split, standalone build, packaging. |
| 54 | Graph API | L | Todo | Yes | #58 | In-tree compute-only plan test. |
| 58 | Device | L | Todo | Yes | #53 | Device target + validation policy to the host. |
| 72 | Device | L | Todo | No | - | VVL/cooperative-matrix crash; consumer-triggered. |
| 73 | Tooling | L | Todo | No | #53 | Install rules/export set. |
| 80 | Device | L | Todo | No | - | 8-bit storage; trigger is a failing module. |
| 87 | Tooling | L | Todo | No | - | Version, changelog, deprecation mechanism. |
| 25 | Tooling | Backlog | Todo | No | - | **Retitled (R9)**: remaining `-Wconversion` + scan-build baseline. |
| 83 | Graph API | P2 (R3) | Todo | No | - | Execution bindings; design in PR #208; after #20. |
| 144 | Lifetime | Backlog (R10) | Todo | No | - | Multisampling in `ImageResourceDesc`. |
| 157 | Tooling | Backlog (R10) | Todo | No | - | Mid-suite device fallback. |
| 183 | Tooling | Backlog (R10) | Todo | No | - | TSan report-count enforcement. |
| 186 | Adoption | P2 (R10) | Todo | No | - | `Camera::Resize` drops the new projection. |
| 188 | Graph API | Backlog (R10) | Todo | No | - | Plan-diagram granularity for consumers. |
| 197 | Performance | P1 (R10) | Todo | Yes | - | Trace timings; feeds #59. |
| 209 | Graph API | P1 (R10) | Todo | No | - | Graph-owned resource reuse after a resize; precedes #17. |

## 5. Dependencies

```mermaid
flowchart LR
    I209["#209 stale resource"] --> I17["#17 caches"]
    I17 --> P1G["P1: pipeline caching"]
    I15["#15 frame retention"] --> I40["#40 in-flight > 1"]
    I83["#83 execution bindings"] --> I40
    I16["#16 ✅"] -.-> I40
    I11["#11 ✅"] -.-> I40
    I57["#57 FrameSync"] --> I20["#20 port"]
    I20 --> I48["#48 DebugUI"]
    I20 --> I49["#49 shadows"]
    I48 --> I51["#51 delete"]
    I49 --> I51
    I197["#197 timings"] --> I59["#59 cost"] --> P1G
    I20 -. both paths .-> I59
    I53["#53 graph boundary"] --> I58["#58 device layer"] --> I54["#54 compute-only test"]
    I73["#73 packaging"] -.-> I53
    I87["#87 versioning"] -.-> I73
    I72["#72 cooperative matrix"] -.-> LG["L gate"]
    I53 -.-> LG
```

## 6. Reconciliation findings

Each finding was checked against the repository or the board on 2026-10-22; the action is what this
branch and the corresponding issue/field edits do.

| # | Finding | Evidence | Action |
| --- | --- | --- | --- |
| R1 | `Roadmap.md` "Where we are" line counts are stale. | `src/RenderGraph` is **7,466** lines (doc says 3,777); `src/RenderPasses` 4,412 (4,403); `helloVulkan.cpp` 376 (374). | Updated the table. |
| R2 | "The graph has zero production adoption" overstates it now. | `helloVulkan` is still legacy, but `src/app/helloVulkanRG.cpp` (330 lines) owns its frame loop through the graph, and `muyo-llm` is an external non-renderer consumer. | Reworded: zero **in-repo renderer** adoption; helloVulkanRG + muyo-llm exercise it. |
| R3 | `#83` is in `Backlog (after P3)`, but its design trigger is **after #20** (P2), which is before P3. | Design note (PR #208) status section; #83 body update. | Moved #83 to P2 (not part of the P2 gate). |
| R4 | `Consumers.md` lists "Declaration errors fail at `Build()`, naming the graph entity" as a **gap** owned by #55 - but #55 is closed. | #55 closed; the failure-index work closed as completed in the batches #127/#128/#131/#132/#135/#142 (issues, not PRs). | Row becomes `holds`. |
| R5 | The L milestone description says #55 and #16 block the gate; both are closed. | #16 and #55 are closed; the remaining gap rows are #53 (two) and #72. | Updated the milestone description. |
| R6 | `#40`'s body says descriptor sets are written at record time and depends on #15/#16/#11. Since #15's cache, sets are written at `Build()`; #16 and #11 are closed. | `docs/RenderGraph.md` status bullet says the record-time hazard is gone; the remaining alias is transients. | Rewrote #40's blocker section: per-execution transients + frame-scoped sets + execution bindings (#83); updated `Depends on`. |
| R7 | `#17` has `Critical path: No`, but the P1 gate names pipeline caching as a capability P1 cannot close without. | P1 milestone description. | Set `Critical path: Yes` (slippage moves P1's close). |
| R8 | `#53`'s measurements are stale (13 files / 15 call sites / "no `target_link_libraries`"), and it still depends on #11 (closed). | Now **8** files under `src/RenderGraph/` include at least one header from outside the directory (scan in section 7), **2** singleton code call sites in the transitional constructor (+3 doc lines), and `muyo_rg` has `target_link_libraries(PUBLIC muyo)`. | Updated #53's body; cleared the closed dependency. |
| R9 | `#25` is titled "Enable -Wextra ...", but `-Wextra` and the warning baseline landed (#193/#194). Only `-Wconversion` measurement and the scan-build baseline remain. | #25's own status section. | Retitled to the remaining scope; kept `Debt: After P3`. |
| R10 | Seven open issues have **no milestone**, so the Roadmap's "milestone owns the phase" contract does not cover them: #144, #157, #183, #186, #188, #197, #209. | `gh issue list` vs milestones. | Assigned: #197/#209 to P1, #186 to P2, #144/#157/#183/#188 to Backlog. |
| R11 | The `Depends on` field for #59 is empty although #59 consumes #197's timing data; #57 still lists closed #16. | #59 body and #197 body. | Set #59 → #197; #57 keeps #15 (open) and drops #16. |
| R12 | `docs/RenderGraph-Library-Design.md` still says `muyo_rg` declares no dependencies, while it now links the engine. | `CMakeLists.txt:366 target_link_libraries(muyo_rg PUBLIC muyo)`. | Corrected the sentence: the remaining gap is standing alone, not declaring nothing. |
| R13 | The Roadmap's "Not on the parity path" table still lists #22/#24 (closed) and #25 (landed except two items). | #22 and #24 closed; #25 status section. | Table updated: #25's remaining scope only. |
| R14 | P0 has 0 open issues but the milestone is still open. | Milestone counts. | Left open as a grouping device; noted here so it is a decision rather than an oversight. |
| R15 | The Roadmap's Risk 5 says CI gates "formatting"; CI now also runs the changed-line clang-tidy gate. | `.github/workflows/ci.yml` has a `clang-tidy (changed lines)` job and 7 jobs total. | Added to Risk 5. |

## 7. Refreshing this view

```bash
# phase counts
gh api 'repos/v3c70r/Muyo/milestones?state=all&per_page=100' \
  --jq '.[]|"\(.title): open=\(.open_issues) closed=\(.closed_issues)"'

# issue state, including the custom fields (the key has a space: "critical path"). Board reads need
# the read:project scope (gh auth refresh -s read:project); without it the command fails, which is not
# the same as an empty board.
gh project item-list 3 --owner v3c70r --limit 200 --format json \
  | python3 -c 'import json,sys; [print(i["content"]["number"], i.get("status"), i.get("critical path"), i.get("depends on")) for i in json.load(sys.stdin)["items"] if i.get("content",{}).get("number")]'

# the measurement claims in section 6
cat $(find src/RenderGraph -name '*.cpp' -o -name '*.h') | wc -l
grep -rnE 'GetRenderDevice\(\)|GetRenderResourceManager\(\)|GetDescriptorManager\(\)' --include=*.cpp src/RenderGraph/ | wc -l
# -> 2. Without --include=*.cpp it prints 5: the two code sites plus three doc-comment lines in the header.
grep -n 'target_link_libraries(muyo_rg' CMakeLists.txt

# The include scan behind R8/R12: quoted includes that are not files in src/RenderGraph/.
python3 - <<'PY'
import os, glob, re
d = 'src/RenderGraph'
local = set(os.listdir(d))
for f in sorted(glob.glob(d + '/*.h') + glob.glob(d + '/*.cpp')):
    inc = [m.split('/')[-1] for m in re.findall(r'^\s*#include\s+"([^"]+)"', open(f, encoding='utf-8').read(), re.M)]
    ext = [h for h in inc if h not in local and not h.startswith('vulkan')]
    if ext:
        print(f, '->', ' '.join(sorted(set(ext))))
PY
# 8 files at this head; the largest sets are RenderGraphBuilder.cpp (4) and RenderGraphBuilder.h (3).
```

A number in this file is a measurement with a date. If it is wrong, the fix is the command, not the
sentence.
