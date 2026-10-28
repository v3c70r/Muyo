#!/usr/bin/env python3
"""B1b gate: the RenderGraph path has no descriptor sets, layouts, pools or pipeline layouts.

The graph is descriptor-heap-only since B1b. The legacy `RenderPassManager` keeps its own descriptor
path outside `src/RenderGraph/`, so the scan is scoped to that directory. This is the mechanical form of
the acceptance criterion in docs/DescriptorHeap-Bindless-Design.md §12: a grep for the four calls the
descriptor-set path needed is empty.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GRAPH_DIR = ROOT / "src" / "RenderGraph"

# The calls the descriptor-set path used. Each one requires a layout or a set that the heap path does
# not have; a hit means a legacy path came back or a new one was added.
FORBIDDEN = (
    "vkCmdBindDescriptorSets",
    "vkCreateDescriptorSetLayout",
    "vkCreatePipelineLayout",
    "vkCmdPushConstants",
)


def main() -> int:
    files = sorted(p for p in GRAPH_DIR.rglob("*") if p.suffix in {".h", ".cpp"})
    violations: list[str] = []
    for path in files:
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for token in FORBIDDEN:
                if token in line:
                    violations.append(f"{path.relative_to(ROOT)}:{lineno}: {token}")

    if violations:
        print("descriptor-path gate FAILED: src/RenderGraph still contains the descriptor-set path:")
        for violation in violations:
            print(f"  {violation}")
        return 1

    print(f"descriptor-path gate OK: {len(files)} files under src/RenderGraph, none of {', '.join(FORBIDDEN)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
