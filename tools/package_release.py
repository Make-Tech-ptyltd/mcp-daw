#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
"""Builds the MCP DAW app zip (MCP-DAW-<version>-win64.zip) from a finished build.

    pip install markdown                                  # once (turns the guides into web pages)
    python tools/package_release.py --build build/Release
    python tools/package_release.py --build <dir> --source-url https://github.com/<owner>/<repo> --ref v0.4.0

GitHub Actions runs this for every version tag (see .github/workflows/build.yml); running it by hand
gives the same zip for a local check. Layout of the zip (one top-level folder):

    MCP-DAW-0.4.0-win64/
        MCP-DAW.exe, mcp-daw-bridge.exe
        Quick Start.html
        Demo/                  Run Demo.cmd, the demo script, the three songs, their samples
        Samples-Starter-Kit/   the starter samples (the browser opens this folder on first run)
        Scripting/             mcp_daw.py, example scripts, SCRIPTING.html, TOOLS.html
        Licences/              LICENSE.txt (AGPL), THIRD_PARTY_NOTICES.txt, OFL.txt, SOURCE.txt
"""
from __future__ import annotations

import argparse
import html
import os
import re
import subprocess
import sys
import time
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

# Example scripts that go into Scripting/ (dev-only tools such as make_demo.py stay in the repo).
EXAMPLES = ["dilla_beat.py", "dnb_rollercoaster.py", "jungle_showdown.py", "sine_bass.py",
            "make_demo_loops.py", "make_jungle_loops.py"]

PAGE = """<!doctype html>
<html lang="en-AU"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  body {{ font: 16px/1.55 "Segoe UI", system-ui, sans-serif; color: #1c2129; background: #f7f8fa;
         max-width: 860px; margin: 0 auto; padding: 24px 20px 64px; }}
  h1, h2, h3 {{ line-height: 1.25; }}  h1 {{ border-bottom: 3px solid #3fd0c9; padding-bottom: .3em; }}
  h2 {{ margin-top: 2em; border-bottom: 1px solid #d5dae1; padding-bottom: .2em; }}
  a {{ color: #0f7f7a; }}  code {{ background: #e9ecf0; padding: .1em .35em; border-radius: 4px; font-size: .92em; }}
  pre {{ background: #1c2129; color: #edf0f4; padding: 14px 16px; border-radius: 8px; overflow-x: auto; }}
  pre code {{ background: none; padding: 0; color: inherit; }}
  table {{ border-collapse: collapse; width: 100%; margin: 1em 0; }}
  th, td {{ border: 1px solid #d5dae1; padding: 6px 10px; text-align: left; vertical-align: top; }}
  th {{ background: #e9ecf0; }}  blockquote {{ border-left: 4px solid #3fd0c9; margin: 1em 0; padding: .2em 1em; color: #46515f; }}
  footer {{ margin-top: 3em; color: #6b7684; font-size: .9em; }}
</style></head>
<body>
{body}
<footer>MCP DAW {version} &middot; Make Tech Pty Ltd &middot; <a href="https://maketech.com.au">maketech.com.au</a></footer>
</body></html>
"""


def fail(message: str) -> None:
    sys.exit(f"package_release: {message}")


def project_version() -> str:
    m = re.search(r"project\s*\(\s*MCPDAW\s+VERSION\s+([0-9.]+)", (REPO / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not m:
        fail("could not read the version from CMakeLists.txt")
    return m.group(1)


def md_to_html(md_file: Path, version: str, links: dict) -> str:
    """Markdown guide -> standalone HTML page. `links` renames targets such as TOOLS.md -> TOOLS.html."""
    import markdown   # pip install markdown
    text = md_file.read_text(encoding="utf-8")
    for old, new in links.items():
        text = text.replace(f"]({old})", f"]({new})").replace(f"]({old}#", f"]({new}#")
    body = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    title = re.search(r"^#\s+(.+)$", text, re.M)
    return PAGE.format(title=html.escape(title.group(1) if title else md_file.stem), body=body, version=version)


def crlf(text: str) -> bytes:
    """Windows line endings for text files people open in Notepad."""
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def git_commit() -> str:
    try:
        return subprocess.run(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def build_zip(build: Path, out_dir: Path, platform: str, source_url: str, ref: str) -> Path:
    version = project_version()
    exe = ".exe" if platform.startswith("win") else ""
    bin_dir = build / "MCPDAW_artefacts" / "Release"
    if not bin_dir.is_dir():
        bin_dir = build   # also accept the folder that holds the exes
    app, bridge = bin_dir / f"MCP-DAW{exe}", bin_dir / f"mcp-daw-bridge{exe}"
    for f in (app, bridge):
        if not f.is_file():
            fail(f"missing {f} - build first (Release)")

    import make_demo   # the demo must be complete, with exact file names
    problems = make_demo.check(REPO / "demo")
    if problems:
        fail("the demo is not complete:\n  " + "\n  ".join(problems))
    kit = REPO / "Samples-Starter-Kit"
    if not any(kit.glob("*.wav")):
        fail(f"no samples in {kit}")

    root = f"MCP-DAW-{version}-{platform}"
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"{root}.zip"
    files: list[tuple[str, bytes | Path]] = []   # (path inside the zip, bytes or source file)

    files += [(app.name, app), (bridge.name, bridge)]
    files.append(("Quick Start.html", md_to_html(REPO / "docs" / "QUICKSTART.md", version, {}).encode("utf-8")))

    for f in sorted((REPO / "demo").rglob("*")):
        if f.is_file() and f.name != "run-demo.sh" and "__pycache__" not in f.parts:
            files.append(("Demo/" + f.relative_to(REPO / "demo").as_posix(), f))
    for f in sorted(kit.iterdir()):
        if f.is_file():
            files.append(("Samples-Starter-Kit/" + f.name, f))

    files.append(("Scripting/mcp_daw.py", REPO / "tools" / "mcp_daw.py"))
    for name in EXAMPLES:
        files.append((f"Scripting/{name}", REPO / "tools" / name))
    files.append(("Scripting/SCRIPTING.html",
                  md_to_html(REPO / "docs" / "SCRIPTING.md", version, {"TOOLS.md": "TOOLS.html"}).encode("utf-8")))
    files.append(("Scripting/TOOLS.html", md_to_html(REPO / "docs" / "TOOLS.md", version, {}).encode("utf-8")))

    files.append(("Licences/LICENSE.txt", crlf((REPO / "LICENSE").read_text(encoding="utf-8"))))
    files.append(("Licences/THIRD_PARTY_NOTICES.txt", crlf((REPO / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8"))))
    files.append(("Licences/OFL.txt", crlf((REPO / "assets" / "OFL.txt").read_text(encoding="utf-8"))))
    commit = git_commit()
    where = source_url or "the GitHub page this download came from (github.com, search \"MCP DAW Make Tech\")"
    files.append(("Licences/SOURCE.txt", crlf(
        f"MCP DAW {version} is free software under the GNU Affero General Public License v3.0 or later\n"
        f"(LICENSE.txt). You can get the complete corresponding source code, and build it yourself, from:\n\n"
        f"    {where}\n"
        + (f"    version: {ref}\n" if ref else "")
        + (f"    commit:  {commit}\n" if commit else "")
        + "\nBuilding instructions are in docs/BUILDING.md in the source.\n"
          "Make Tech Pty Ltd - maketech.com.au - ken@maketech.com.au\n")))

    stamp = time.localtime()[:6]   # extracted files show the build date
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for arcname, src in files:
            info = zipfile.ZipInfo(f"{root}/{arcname}", date_time=stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            data = src.read_bytes() if isinstance(src, Path) else src
            info.external_attr = (0o755 if arcname.endswith(exe or ".never") else 0o644) << 16
            z.writestr(info, data)
    size = zip_path.stat().st_size / 1e6
    print(f"Wrote {zip_path} ({len(files)} files, {size:.1f} MB)")
    return zip_path


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", type=Path, required=True, help="build folder (holds MCPDAW_artefacts/Release)")
    ap.add_argument("--out", type=Path, default=REPO / "dist", help="where the zip goes (default: dist/)")
    ap.add_argument("--platform", default="win64", help="name suffix, and .exe files when it starts with 'win'")
    ap.add_argument("--source-url", default=os.environ.get("MCPDAW_SOURCE_URL", "https://github.com/Make-Tech-ptyltd/mcp-daw"),
                    help="public source repository (AGPL source offer), e.g. https://github.com/<owner>/<repo>")
    ap.add_argument("--ref", default="", help="version tag the zip was built from, e.g. v0.4.0")
    args = ap.parse_args()
    build_zip(args.build.resolve(), args.out.resolve(), args.platform, args.source_url, args.ref)


if __name__ == "__main__":
    main()
