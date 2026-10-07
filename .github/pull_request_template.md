<!--
Mirrors AGENTS.md section 6 ("What a ready PR states"). The review session has none of your
context, so fill this in for someone reading the diff cold.
-->

## What and why

<!-- The problem, and what this changes. This text becomes the squashed commit body, so write it to
     be read as `git log`. -->

## What is explicitly not changed

<!-- Scope boundaries a reviewer should not have to guess at. For a refactor: state that behaviour
     is unchanged. -->

## Verification

<!-- Reproduced by the developer before merge. A reviewer with the hardware re-runs when available;
     a static-only reviewer is never the sole confirmation of a runtime claim. -->

| Check | Result |
| --- | --- |
| default build + tests | <!-- 89 assertions / 10 cases --> |
| `-DFEATURE_RAY_TRACING=ON` build + tests | <!-- 109 assertions / 12 cases --> |
| `scripts/sanity.sh` (docs + format) | |
| `scripts/sanity.sh sanitize` | <!-- required if memory/lifetimes/threading touched --> |
| GPU / driver / queue topology | |

## Known limitations and debt

<!-- Anything accepted rather than fixed, and why. "Marked, not fixed" is fine with a reason. -->

## Follow-ups

<!-- Issues filed from this work, with links. Anything deferred must be an issue, not a silent
     TODO. Call out anything you had to leave for a later phase. -->

## Checklist

- [ ] One concern per PR (mechanical refactors separate from behaviour changes).
- [ ] Branch based on an up-to-date `master`.
- [ ] New behaviour has a test with teeth, or the PR says why a test is impractical.
- [ ] `AGENTS.md` updated if the process itself changed.
