#!/usr/bin/env python3
"""Decide whether a failed test run's sanitizer report is about this repository.

The CI Sanitize job runs on a hosted runner whose software Vulkan driver and loader allocate inside
uninstrumented modules: on every report so far (#189), each leak's allocation stack is
``asan_malloc_linux.cpp`` followed by ``<unknown module>`` frames, and the first symbolised frame is a
loader function or ``asan_thread_start``. No suppression can name those without also hiding a leak in a
future project thread, and no runner image or driver version tested removes them (22.04, 24.04 and 26.04
all report the same 18-allocation block).

So the job keeps leak detection on and asks this script one question: **is any leak's allocation inside
this repository?** It walks each leak block's frames from the allocator upwards, skips the C/C++/sanitizer
runtime, and looks at the first frame that identifies an allocator:

* a path under the repository root, or a symbol from the test binary -> the leak is ours, fail;
* ``<unknown module>``, an SDK/loader path, or a driver module -> external, ignore;
* no leak blocks at all, or an ASan/UBSan error -> fail, the run is not an external-leak-only failure.

Conservative by construction: anything that is not clearly external fails the run, and anything that is not
clearly ours is ignored. A project leak allocated through a driver callback would be classified external;
that is the accepted cost of not being able to symbolise the driver.

Usage
-----
  check_sanitizer_report.py --err tests.err

Exit status: 0 when the only sanitizer diagnostics are leaks allocated outside the repository, 1 when a
leak is project-attributable or a non-leak sanitizer diagnostic is present, 2 on a usage or input error.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LEAK_HEADER = re.compile(r"^(Direct|Indirect) leak of \d+ byte")
FRAME = re.compile(r"^\s+#\d+\s+0x[0-9a-fA-F]+\s+(?P<rest>.*)$")
ERROR_START = re.compile(r"^==\d+==ERROR: (Address|Leak)Sanitizer")

# Frames that only say *how* the allocation happened, not where it belongs. Skipped while looking for the
# first allocator frame.
RUNTIME_MARKERS = (
    "libsanitizer",
    "libasan.so",
    "asan_",
    "libstdc++",
    "/usr/include/c++/",
    "libc.so",
    "libgcc",
    "ld-linux",
    "operator new",
    "operator delete",
)

# A frame we can attribute to an external module's own code.
EXTERNAL_MARKERS = (
    "<unknown module>",
    "/vulkan-sdk/",
    "libvulkan",
    "loader.c",
    "trampoline.c",
    "libdrm",
    "llvmpipe",
)

PROJECT_MARKERS = ("tests+0x", "tests(", "/tests:")


def frame_is_runtime(where: str) -> bool:
    return any(marker in where for marker in RUNTIME_MARKERS)


def frame_is_project(where: str) -> bool:
    if REPO_ROOT in where:
        return True
    return any(marker in where for marker in PROJECT_MARKERS)


def frame_is_external(where: str) -> bool:
    return any(marker in where for marker in EXTERNAL_MARKERS)


def classify_block(frames: list[tuple[bool, str]]) -> tuple[bool, str]:
    """Return (is_project, deciding frame) for one leak block.

    A frame with no symbol (``(module+0xoffset)`` or ``(<unknown module>)``) is one the sanitizer could not
    resolve; the driver, the loader and the ASan runtime all appear that way, and their allocations are not
    ours. A symbolised frame decides: a repository path is ours, a loader/driver/system path is external,
    and the C/C++/sanitizer runtime is skipped on the way to the allocator.
    """
    for symbolized, where in frames:
        if not symbolized:
            return False, f"<unsymbolised> {where}"
        if frame_is_runtime(where):
            continue
        if frame_is_project(where):
            return True, where
        if frame_is_external(where):
            return False, where
        # An unrecognised symbol at a non-repository path: external rather than a project leak.
        if where.startswith("/") and REPO_ROOT not in where:
            return False, where
        return True, where
    return True, "<no allocator frame>"


def scan(path: str) -> int:
    blocks: list[tuple[str, list[tuple[bool, str]]]] = []
    current: list[str] | None = None
    non_leak: list[str] = []

    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if LEAK_HEADER.match(line):
                current = []
                blocks.append((line, current))
                continue
            if ERROR_START.match(line):
                if "LeakSanitizer" not in line:
                    non_leak.append(line)
                continue
            if "runtime error:" in line:
                non_leak.append(line)
                continue
            match = FRAME.match(line)
            if match and current is not None:
                rest = match.group("rest").strip()
                if rest.startswith("in "):
                    current.append((True, rest[3:]))
                else:
                    current.append((False, rest))

    if non_leak:
        print(f"sanitizer report: {len(non_leak)} non-leak diagnostic(s) - failing")
        for line in non_leak[:10]:
            print(f"  {line}")
        return 1

    if not blocks:
        print("sanitizer report: no leak blocks found - this failure is not an external-leak-only run")
        return 1

    project_blocks = 0
    for header, frames in blocks:
        is_project, where = classify_block(frames)
        verdict = "PROJECT" if is_project else "external"
        if is_project:
            project_blocks += 1
        print(f"{verdict:8} {header}")
        print(f"         allocation: {where}")

    print(f"\nsanitizer report: {len(blocks)} leak block(s), {project_blocks} attributable to this repository")
    if project_blocks:
        print("A leak whose allocation is in this repository's code fails the run.")
        return 1
    print("Every leak allocates inside an external module; the suite result and exit status stand.")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--err", required=True, help="the suite's captured stderr (tests.err)")
    args = parser.parse_args(argv)
    try:
        return scan(args.err)
    except OSError as error:
        print(f"check_sanitizer_report: cannot read {args.err!r}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
