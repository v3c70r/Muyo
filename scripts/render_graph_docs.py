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
import xml.etree.ElementTree as ET
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(REPO_ROOT, "docs")
HEADER_DIR = os.path.join(REPO_ROOT, "src", "RenderGraph")
OVERVIEW = os.path.join(DOCS_DIR, "RenderGraph.md")
DESIGN_NOTE = os.path.join(DOCS_DIR, "DescriptorSet-Lifecycle-Design.md")
DOXYFILE_TEMPLATE = os.path.join(DOCS_DIR, "Doxyfile.render_graph")
GENERATED_MD = os.path.join(DOCS_DIR, "RenderGraph-api.md")
COVERAGE_CHECKER = os.path.join(DOCS_DIR, "check_api_coverage.py")
MD_GENERATOR = os.path.join(DOCS_DIR, "gen_api_md.py")


def doxygen_version(doxygen: str) -> str:
    """Return the version string reported by the doxygen binary, or "unknown"."""
    try:
        result = subprocess.run([doxygen, "--version"], capture_output=True, text=True, check=False)
    except OSError:
        return "unknown"
    first_line = (result.stdout or "").strip().splitlines()
    return first_line[0] if first_line else "unknown"


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
    config = config.replace("@RENDERGRAPH_DESIGN_NOTE@", DESIGN_NOTE)
    config = config.replace("@DOXYGEN_WARNLOG@", warn_log)
    # Graphviz is optional: enable graphs when `dot` is available.
    config = config.replace("HAVE_DOT               = NO", f"HAVE_DOT               = {'YES' if shutil.which('dot') else 'NO'}")
    return config


def run(cmd: list[str]) -> int:
    return subprocess.call(cmd, cwd=REPO_ROOT)


# Where each section is rendered. Entries get their own block; a class member's sections go into its table cell,
# so the two are counted with different markers.
_ENTRY_MARKERS = {
    "param": "| Parameter | Description |",
    "return": "**Returns**",
    "exception": "**Throws**",
}
_CELL_MARKERS = {
    "param": "Parameters: ",
    "return": "Returns: ",
    "exception": "Throws: ",
}


def _count_sections(detailed_el) -> dict[str, int]:
    """Return 1 for each section kind this description carries.

    One, not the number of items: a marker is emitted **once per entity** (`| Parameter | Description |` per
    entity with parameters, `**Throws**` per entity with exceptions), so counting items would over-expect by the
    number of parameters - which is how the first raw-XML attempt asked for 55 parameter tables where 17 are
    rendered.
    """
    counts = {"param": 0, "return": 0, "exception": 0}
    if detailed_el is None:
        return counts
    for listing in detailed_el.iter("parameterlist"):
        kind = listing.get("kind")
        if kind in ("param", "templateparam") and listing.findall("parameteritem"):
            counts["param"] = 1
        elif kind == "exception" and listing.findall("parameteritem"):
            counts["exception"] = 1
    if any(sect.get("kind") == "return" for sect in detailed_el.iter("simplesect")):
        counts["return"] = 1
    return counts


def check_rendered_sections(xml_dir: str, md_path: str) -> int:
    """Fail when the reference omits a section the headers document.

    The expectations come from the **raw Doxygen XML**, not from `gen_api_md.collect`. That is deliberate: the
    first version of this check counted the generator's own parse result, so a parser that dropped a kind dropped
    it from both sides and the check went green - the same self-cancelling failure the staleness comparison has,
    one layer up (#161 review A1).

    Two wrinkles, both load-bearing:

    - Doxygen repeats every member in its `file` compound as well as its namespace/class compound, so the walk is
      restricted to the four API compound kinds and deduplicated by memberdef id.
    - A namespace member renders as its own entry block; a class member renders inside its table cell. The
      expected counts are split the same way, or a correct reference would look short.
    """
    entry_counts = {"param": 0, "return": 0, "exception": 0}
    cell_counts = {"param": 0, "return": 0, "exception": 0}
    seen: set[str] = set()

    index_path = os.path.join(xml_dir, "index.xml")
    if not os.path.isfile(index_path):
        print(f"render_graph_docs: no Doxygen index.xml in {xml_dir!r}.")
        return 1
    index = ET.parse(index_path).getroot()

    # Iterate the compounds the index lists rather than every file in the directory: the index is what the
    # generator reads, and a leftover XML file is not an entity of this build.
    for compound in index.findall("compound"):
        refid = compound.get("refid") or ""
        path = os.path.join(xml_dir, f"{refid}.xml")
        if not os.path.isfile(path):
            continue
        root = ET.parse(path).getroot()
        compounddef = root.find("compounddef")
        if compounddef is None:
            continue
        compound_kind = compounddef.get("kind")
        if compound_kind not in ("namespace", "class", "struct", "enum"):
            continue
        # A non-public compound never becomes an entry, so counting its sections would over-expect and red a
        # consistent tree. `RenderGraphBuilder::RenderGraphNode` and friends are exactly this case today.
        if compounddef.get("prot") not in (None, "public"):
            continue

        # The compound's own description renders as an entry block - but only for a class/struct/enum. The
        # generator emits no entry for a namespace itself, so counting a namespace's own description would
        # over-expect the same way.
        if compound_kind in ("class", "struct", "enum"):
            for key, value in _count_sections(compounddef.find("detaileddescription")).items():
                entry_counts[key] += value

        # The API surface is the public members of public sections - the same selection the generator makes, so
        # the two agree on *which* entities exist. What this check does not share is the section *parsing*.
        target = entry_counts if compound_kind == "namespace" else cell_counts
        for section in compounddef.findall("sectiondef"):
            section_kind = section.get("kind") or ""
            if compound_kind == "namespace":
                allowed = section_kind.startswith("public-") or section_kind in ("func", "var", "typedef", "enum")
            else:
                allowed = section_kind.startswith("public-")
            if not allowed:
                continue
            for member in section.findall("memberdef"):
                if member.get("prot") not in (None, "public"):
                    continue
                member_id = member.get("id") or ""
                if member_id in seen:
                    continue
                seen.add(member_id)
                for key, value in _count_sections(member.find("detaileddescription")).items():
                    target[key] += value

    with open(md_path, encoding="utf-8") as handle:
        rendered = handle.read()

    missing = []
    for scope, counts, markers in (
        ("entry", entry_counts, _ENTRY_MARKERS),
        ("member cell", cell_counts, _CELL_MARKERS),
    ):
        for section, expected in counts.items():
            marker = markers[section]
            actual = rendered.count(marker)
            if actual < expected:
                missing.append((scope, marker.strip("|* "), expected, actual))

    if not missing:
        return 0

    for scope, section, expected, actual in missing:
        print(
            f"render_graph_docs: {expected} documented '{section}' section(s) at {scope} level, "
            f"but the reference renders {actual}."
        )
    print("render_graph_docs: the API reference is dropping documented sections (see #161).")
    return 1


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

    # Doxygen does not clean its output directory: a `class*.xml` left by an older run survives and is picked up
    # by anything that walks the XML. That is how the section check came to expect 41 member-cell returns where the
    # reference renders 35 - the six extras were members of compounds that have since moved.
    xml_dir = os.path.join(output_dir, "xml")
    if os.path.isdir(xml_dir):
        shutil.rmtree(xml_dir)

    rc = run([doxygen, doxyfile_path])
    if rc != 0:
        print("render_graph_docs: doxygen failed (see the warnings above).")
        return rc

    # 1. Coverage enforcement.
    rc = run([sys.executable, COVERAGE_CHECKER, xml_dir])
    if rc != 0:
        return rc

    # 2. Markdown API reference.
    target_md = os.path.join(output_dir, "RenderGraph-api.md") if args.check else GENERATED_MD
    rc = run([sys.executable, MD_GENERATOR, xml_dir, target_md])
    if rc != 0:
        return rc

    # 3. The reference has to *render* what the headers document. `gen_api_md.py` used to emit the brief only,
    #    so every `@param`/`@returns`/`@throws` was dropped silently - a contract that exists only in source is
    #    one a caller cannot see (#161). The staleness check cannot catch that class, because a generator that
    #    drops a section deterministically regenerates a file that matches itself.
    rc = check_rendered_sections(xml_dir, target_md)
    if rc != 0:
        return rc

    if args.check:
        if not os.path.isfile(GENERATED_MD):
            print(f"render_graph_docs: {GENERATED_MD} is missing; run without --check to create it.")
            return 1
        if not filecmp.cmp(GENERATED_MD, target_md, shallow=False):
            print("render_graph_docs: docs/RenderGraph-api.md is out of date; regenerate it.")
            print(f"  regenerated with doxygen {doxygen_version(doxygen)} (see docs/README.md for the")
            print("  expected version). If the only difference is a qualifier such as `constexpr`,")
            print("  check the version before treating it as real drift.")
            return 1
        print("RenderGraph docs: coverage OK and RenderGraph-api.md is up to date.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
