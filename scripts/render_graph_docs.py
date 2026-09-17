#!/usr/bin/env python3
"""Generate and verify the RenderGraph API documentation.

Locates Doxygen, renders the Doxyfile template, enforces documentation coverage
and regenerates the Markdown API reference.

Modes
-----
  generate (default)  Render HTML + XML + docs/RenderGraph-api.md in place.
  --check             Fail if any public entity is undocumented or if the
                      checked-in docs/RenderGraph-api.md is out of date.

Doxygen is looked up in $DOXYGEN, then on PATH, then in <repo>/.tools.
Run docs/README.md for how to obtain it without root.
"""

from __future__ import annotations

import argparse
import filecmp
import os
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(REPO_ROOT, "docs")
HEADER_DIR = os.path.join(REPO_ROOT, "src", "RenderGraph")
OVERVIEW = os.path.join(DOCS_DIR, "RenderGraph.md")
DOXYFILE_TEMPLATE = os.path.join(DOCS_DIR, "Doxyfile.render_graph")
GENERATED_MD = os.path.join(DOCS_DIR, "RenderGraph-api.md")
COVERAGE_CHECKER = os.path.join(DOCS_DIR, "check_api_coverage.py")
MD_GENERATOR = os.path.join(DOCS_DIR, "gen_api_md.py")


def find_doxygen() -> str | None:
    candidate = os.environ.get("DOXYGEN")
    if candidate and os.path.isfile(candidate):
        return candidate
    found = shutil.which("doxygen")
    if found:
        return found
    for suffix in ("bin/doxygen", "usr/bin/doxygen"):
        candidate = os.path.join(REPO_ROOT, ".tools", "doxygen", suffix)
        if os.path.isfile(candidate):
            return candidate
    return None


def render_doxyfile(output_dir: str, warn_log: str) -> str:
    with open(DOXYFILE_TEMPLATE, encoding="utf-8") as handle:
        config = handle.read()
    config = config.replace("@DOXYGEN_OUTPUT_DIR@", output_dir)
    config = config.replace("@RENDERGRAPH_HEADER_DIR@", HEADER_DIR)
    config = config.replace("@RENDERGRAPH_OVERVIEW@", OVERVIEW)
    config = config.replace("@DOXYGEN_WARNLOG@", warn_log)
    # Graphviz is optional: enable graphs when `dot` is available.
    config = config.replace("HAVE_DOT               = NO", f"HAVE_DOT               = {'YES' if shutil.which('dot') else 'NO'}")
    return config


def run(cmd: list[str]) -> int:
    return subprocess.call(cmd, cwd=REPO_ROOT)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="verify coverage and that the Markdown is current")
    parser.add_argument("--output-dir", default=os.environ.get("RENDER_GRAPH_DOCS_DIR", os.path.join(REPO_ROOT, "build", "docs", "render_graph")))
    args = parser.parse_args()

    doxygen = find_doxygen()
    if doxygen is None:
        print("render_graph_docs: doxygen not found (set $DOXYGEN or see docs/README.md).")
        return 3

    output_dir = args.output_dir
    if args.check:
        tmp = tempfile.mkdtemp(prefix="rgdocs-")
        output_dir = os.path.join(tmp, "out")

    os.makedirs(output_dir, exist_ok=True)
    warn_log = os.path.join(output_dir, "doxygen-warnings.log")
    doxyfile_path = os.path.join(output_dir, "Doxyfile")
    with open(doxyfile_path, "w", encoding="utf-8") as handle:
        handle.write(render_doxyfile(output_dir, warn_log))

    rc = run([doxygen, doxyfile_path])
    if rc != 0:
        print("render_graph_docs: doxygen failed (see the warnings above).")
        return rc

    xml_dir = os.path.join(output_dir, "xml")

    # 1. Coverage enforcement.
    rc = run([sys.executable, COVERAGE_CHECKER, xml_dir])
    if rc != 0:
        return rc

    # 2. Markdown API reference.
    target_md = os.path.join(output_dir, "RenderGraph-api.md") if args.check else GENERATED_MD
    rc = run([sys.executable, MD_GENERATOR, xml_dir, target_md])
    if rc != 0:
        return rc

    if args.check:
        if not os.path.isfile(GENERATED_MD):
            print(f"render_graph_docs: {GENERATED_MD} is missing; run without --check to create it.")
            return 1
        if not filecmp.cmp(GENERATED_MD, target_md, shallow=False):
            print("render_graph_docs: docs/RenderGraph-api.md is out of date; regenerate it.")
            return 1
        print("RenderGraph docs: coverage OK and RenderGraph-api.md is up to date.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
