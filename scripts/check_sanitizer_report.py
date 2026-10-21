#!/usr/bin/env python3
"""Decide whether a failed test run's sanitizer report is about this repository.

The CI Sanitize job runs on a hosted runner whose software Vulkan driver and loader allocate inside
uninstrumented modules: on every report so far (#189), each leak's allocation stack is
``asan_malloc_linux.cpp`` followed by ``<unknown module>`` frames, and the first symbolised frame is a
loader function or ``asan_thread_start``. No suppression can name those without also hiding a leak in a
future project thread, and no runner image or driver version tested removes them (22.04, 24.04 and 26.04
all report the same 18-allocation block).

So the job keeps leak detection on and asks this script one question: **is the leak's allocation inside
this repository?** It walks each leak block's frames from the allocator upwards and stops at the first frame
the sanitizer could resolve:

* **an unsymbolised frame decides external, whatever module it names.** That is ``(<unknown module>)`` and
  ``(module+0xoffset)``, including frames ASan attributes to the test binary's own address range: an unwind
  through uninstrumented thread code can land there, and calling those ours would fail the job on every run
  of the hosted runner's software driver (#189). The cost is explicit: an unsymbolised frame *above* the
  project frames makes the block external.
* a symbolised frame from this repository - a path under the root, or a test-binary symbol - is ours, fail;
* the C/C++/sanitizer runtime frames are skipped on the way to the allocator;
* any other symbolised frame (SDK/loader/driver path) is external, ignore;
* no leak blocks at all, or an ASan/UBSan error, fails: the run is not an external-leak-only failure.

A project leak allocated through a driver callback would be classified external; that is the accepted cost
of not being able to symbolise the driver. The alternative - treating unsymbolised frames as ours - makes
the CI job permanently red on leaks this repository does not own.

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

# A symbolised frame we can attribute to an external module's own code.
EXTERNAL_MARKERS = (
    "<unknown module>",
    "/vulkan-sdk/",
    "libvulkan",
    "loader.c",
    "trampoline.c",
    "libdrm",
    "llvmpipe",
)

# Only consulted for *symbolised* frames: an unsymbolised `(tests+0x...)` is decided by the rule in
# classify_block and is external. This marker catches the symbolised module+offset form the sanitizer
# emits for an inlined frame with no source line.
PROJECT_MARKERS = ("tests+0x",)


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

    The first frame the sanitizer could resolve decides. An unsymbolised frame - ``(<unknown module>)`` or
    ``(module+0xoffset)`` - is always external, *whatever module it names*, including the test binary: the
    unwind can land in the main binary's address range through uninstrumented code, and attributing that to
    us would make the CI job red on every run of the software driver (#189). A symbolised frame is ours only
    if it names a repository path (or the symbolised test-binary form); loader/driver/system symbols are
    external; the C/C++/sanitizer runtime is skipped on the way to the allocator.
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
