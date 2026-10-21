#!/usr/bin/env python3
"""Check per-file warning counts against the recorded baseline.

The build is expected to be clean apart from warnings this project already owns, and those are recorded
per file in ``scripts/warning-baseline.txt``. This check compares the *changed* files against that
baseline: a file may keep its recorded count, and one more warning in it fails. Files the branch did not
touch are reported but never fail, so an unrelated file's pre-existing warning cannot block work.

Only diagnostics located under ``src/`` are counted. Vendored and generated headers are `SYSTEM`
includes (see CMakeLists.txt), so they do not appear in the log as warnings against our translation
units; anything under ``thirdparty/`` or a build directory is excluded anyway.

The baseline is per configuration because warning sets differ: Release adds ``-Wnonnull`` and
``-Warray-bounds`` evidence that ``-O3`` inlining produces. ``scripts/sanity.sh warnings`` measures the
Debug configuration, which is what the recorded file describes.

Usage
-----
  warning_baseline.py --log BUILD_LOG --baseline BASELINE [--base REF]
      Compare changed files (``git diff --name-only REF``) against the baseline.
  warning_baseline.py --log BUILD_LOG --write
      Print a baseline for the log's counts, newest-state first (redirect into the baseline file).

Exit status: 0 when no changed file regressed, 1 on a regression, 2 on a usage or input error (missing or
unreadable log, malformed baseline, undiffable base) - so a caller can tell "the tree regressed" from "this
check could not run".
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE_HEADER = "# <path> <count>  -  -Wall -Wextra (Debug) per-file warning counts."


class InputError(Exception):
    """A bad argument, an unreadable file or an undiffable base - not a warning regression."""

# GCC prints one primary ``file:line:col: warning: ...`` line per diagnostic; notes and "In file
# included from" continuation lines do not match. A diagnostic's option is the trailing ``[-W...]`` when
# present; warnings without one still count, as "unclassified".
WARNING_RE = re.compile(r"^(?P<path>[^:\n]+?):\d+:\d+: warning: (?P<message>.*)$")
KIND_RE = re.compile(r"\[(-W[^\]]+)\]\s*$")


def project_relative(path: str) -> str | None:
    """Return ``path`` relative to the repository when it is one of the project's own sources."""
    absolute = os.path.abspath(path)
    try:
        relative = os.path.relpath(absolute, REPO_ROOT)
    except ValueError:  # different drive on Windows
        return None
    if relative.startswith(".."):
        return None
    parts = relative.split(os.sep)
    if not relative.startswith("src" + os.sep):
        return None
    if any(part in ("thirdparty", "_deps") for part in parts):
        return None
    return relative


def counts_from_log(log_path: str) -> tuple[dict[str, int], dict[str, list[str]]]:
    """Return per-file counts and the warning lines themselves, for the report."""
    counts: dict[str, int] = {}
    lines: dict[str, list[str]] = {}
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = WARNING_RE.match(line.rstrip("\n"))
            if not match:
                continue
            relative = project_relative(match.group("path"))
            if relative is None:
                continue
            counts[relative] = counts.get(relative, 0) + 1
            lines.setdefault(relative, []).append(line.rstrip("\n"))
    return counts, lines


def read_baseline(path: str) -> dict[str, int]:
    baseline: dict[str, int] = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 2:
                raise InputError(f"malformed baseline line: {line!r}")
            try:
                baseline[parts[0]] = int(parts[1])
            except ValueError as error:
                raise InputError(f"malformed count in {line!r}") from error
    return baseline


def changed_files(base: str) -> set[str]:
    try:
        output = subprocess.run(
            ["git", "diff", "--name-only", base, "--", "src"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        raise InputError(f"cannot diff against {base!r}: {error}") from error
    return {line.strip() for line in output.splitlines() if line.strip()}


def write_baseline(counts: dict[str, int]) -> None:
    print(BASELINE_HEADER)
    for path in sorted(counts):
        print(f"{path} {counts[path]}")


def check(counts: dict[str, int], lines: dict[str, list[str]], baseline: dict[str, int], changed: set[str]) -> int:
    print(f"project warnings: {sum(counts.values())} in {len(counts)} file(s); baseline: {sum(baseline.values())}")
    print(f"changed files compared: {len(changed)}")

    regressions: list[str] = []
    for path in sorted(changed):
        allowed = baseline.get(path, 0)
        actual = counts.get(path, 0)
        if actual > allowed:
            regressions.append(path)
            print(f"\nREGRESSION {path}: {actual} warning(s), baseline {allowed}")
            for line in lines.get(path, []):
                print(f"  {line}")

    # A recorded file whose count fell is not a failure, but it means the baseline should be refreshed;
    # saying so keeps a shrinking baseline from silently becoming fiction.
    for path in sorted(set(counts) | set(baseline)):
        if path in changed:
            continue
        if counts.get(path, 0) != baseline.get(path, 0):
            print(f"note: {path} is {counts.get(path, 0)} (baseline {baseline.get(path, 0)}), not changed here")

    if regressions:
        print(
            "\nA changed file gained warnings, or a new file has some. Fix them, or - if the warning is "
            "real and owned by an issue - record the new count in scripts/warning-baseline.txt with the "
            "issue number in the commit that raises it."
        )
        return 1
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, help="build log captured with 2>&1")
    parser.add_argument("--baseline", help="recorded per-file counts")
    parser.add_argument("--base", default="origin/master", help="reference for the changed-file set")
    parser.add_argument("--write", action="store_true", help="print a baseline instead of checking")
    args = parser.parse_args(argv)
    if not args.write and not args.baseline:
        parser.error("--baseline is required unless --write is given")

    try:
        counts, lines = counts_from_log(args.log)
        if args.write:
            write_baseline(counts)
            return 0
        return check(counts, lines, read_baseline(args.baseline), changed_files(args.base))
    except InputError as error:
        print(f"warning_baseline: {error}", file=sys.stderr)
        return 2
    except OSError as error:
        # `error` already names the file that failed; naming `args.log` here was wrong when the baseline
        # was the unreadable one (P1 on #201).
        print(f"warning_baseline: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
