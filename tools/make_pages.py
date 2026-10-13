#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the TRIBE site for GitHub Pages (https://apiel.github.io/fm1-tribe/):

  index.html                  one page: what TRIBE is, the ways in (emulator, install, source), and the manual
                              (docs/MANUAL.md) below it
  emu/                        TRIBE in the browser (web/emu: build/emu, from web/emu/build.sh)
  install/index.html          the web installer (web/tribe_installer.html, with fm1pkg.js, fm1ota.js
                              and the package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html           sends old links to the manual (and its #anchors) to the page above
  new/index.html, img/        what's new in each version (tools/manual_html.py, from docs/NEW.md)
  firmware/tribe-VERSION.fwsc the package the installer writes; also the download

  tools/make_pages.py build/tribe-0.1-beta.fwsc 0.1-beta OUT_DIR

The package must be a release build (./build.sh --release X.Y-beta): its identity, FM-1_9XXYYZZ, is
what the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)
sys.path.insert(0, str(SRC / "tools"))
import manual_html  # noqa: E402

REPO = "https://github.com/apiel/fm1-tribe"

FORWARD = """<!doctype html>
<meta charset="utf-8"><title>TRIBE manual</title>
<meta http-equiv="refresh" content="0; url=../#manual">
<script>location.replace("../" + (location.hash || "#manual"))</script>
<p>The manual is now on <a href="../#manual">the main page</a>.</p>
"""

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TRIBE for the FM-1</title>
<meta name="description" content="Ten-track groovebox firmware for the M-VAVE FM-1: any engine on any track, 16 clips per track, scatter effects, two master effects.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&family=IBM+Plex+Mono:wght@400&display=swap">
<style>__CSS__
main { max-width: 46rem; }
body { padding-block: 48px 64px; }
h1 { font-size: 2.6rem; margin: 0 0 .4rem; }
.hero { display: flex; gap: 24px; align-items: center; margin: 0 0 1.6rem; }
.hero img { width: 150px; height: auto; border-radius: 12px; box-shadow: 0 0 0 1px var(--rule); flex: none; }
.hero .lede { margin: 0; max-width: 34rem; }
@media (max-width: 520px) { .hero { flex-direction: column; align-items: flex-start; } }
.topnav { display: flex; flex-wrap: wrap; gap: 6px 20px; margin: 0 0 1.6rem; font: 600 1rem "Barlow Semi Condensed", "Barlow", sans-serif; letter-spacing: .03em; }
.topnav a { text-decoration: none; }
.topnav a:hover { text-decoration: underline; }
.status { margin: 0 0 2rem; }
.ways { display: grid; gap: 12px; margin: 0 0 2rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--sheet); border: 1px solid var(--rule);
          border-radius: 10px; padding: 14px 18px; }
.ways a:hover, .ways a:focus-visible { border-color: var(--accent); outline: none; }
.ways .card { background: var(--sheet); border: 1px solid var(--rule); border-radius: 10px; padding: 14px 18px; }
.ways .card a { color: var(--accent); text-decoration: none; border: 0; padding: 0; background: none; display: inline; }
.ways .card a:hover, .ways .card a:focus-visible { text-decoration: underline; }
.ways .card a.new { font: 400 .95rem "Barlow", sans-serif; margin-left: .5rem; }
.ways strong { font: 600 1.25rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent); display: block; }
.ways span { color: var(--muted); font-size: .95rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin: 0 0 2rem; }
.shots img { width: 100%; height: auto; border-radius: 8px; display: block; background: #000; }
p.small { color: var(--muted); font-size: .9rem; }
#manual > h2:first-child { margin-top: 3.5rem; }
</style>
</head>
<body>
<main>
<div class="hero">
  <img src="img/screen-splash.png" alt="The TRIBE splash screen">
  <div>
    <h1>TRIBE</h1>
    <p class="lede">Groovebox firmware for the M-VAVE FM-1. Ten tracks, and any engine on any track: kicks, the 909 and
808 voices, the 303. Sixteen clips per track, scatter effects, two master effects every track can send to, and four
master knobs you assign to the volume, a DJ filter, the effects or the tempo.</p>
  </div>
</div>
<nav class="topnav" aria-label="On this page"><a href="emu/">Emulator</a> <a href="install/">Install</a> <a href="#manual">Manual</a>
  <a href="new/">What's new</a> <a href="https://discord.gg/fzHTbjh6x">Discord</a> <a href="__REPO__">Source</a></nav>
<div class="shots">
  <img src="img/screen-seq.png" alt="The sequencer: sixteen steps and a track's knobs">
  <img src="img/screen-clips.png" alt="The clips: ten tracks, sixteen clips each">
  <img src="img/screen-sel.png" alt="The mix: every track's level and its two sends">
  <img src="img/screen-glo-fx.png" alt="The global settings: FX 1 and FX 2">
</div>
<div class="status">TRIBE is a beta, built on X0X and Felucca. It installs and uninstalls the way they do, and the installer can put
M-VAVE's own firmware back. Installing is at your own risk.</div>
<nav class="ways" aria-label="Get TRIBE">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, with sound. Mouse, touch or keyboard; no FM-1 needed.</span></a>
  <div class="card"><strong><a href="install/">Install v__VERSION__</a> <a class="new" href="new/">What's new?</a></strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></div>
  <a href="firmware/__PKG__"><strong>Download __PKG__</strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="https://discord.gg/fzHTbjh6x"><strong>Discord</strong><span>Join the fm1-tribe channel: questions, bugs, and what you make with it.</span></a>
</nav>
<p class="small">To go back to the official firmware, use M-VAVE's updater, M-UPGRADE, from
<a href="https://www.m-vave.com/download">m-vave.com/download</a>. M-VAVE and FM-1 are trademarks of their owners.</p>
<div id="manual">
__MANUAL__
</div>
</main>
</body>
</html>
"""


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9(\d\d|\d{6})", product) or product == "FM-1_900":
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y-beta)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"tribe-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "manual", "firmware", "new"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    # the installer
    page = (SRC / "web" / "tribe_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"tribe_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    # the manual: part of the landing page; the old address forwards to it
    md = (SRC / "docs" / "MANUAL.md").read_text(encoding="utf-8")
    md = re.sub(r"\A# TRIBE\n\n.*?\n\n", "", md, count=1, flags=re.S)       # the page has its own title and lede
    md = md.replace("](../", "](")
    manual = manual_html.convert(md)
    (out / "manual" / "index.html").write_text(FORWARD, encoding="utf-8")
    # what's new (the same converter; its pictures beside it)
    subprocess.run([sys.executable, str(SRC / "tools" / "manual_html.py"), str(SRC / "docs" / "NEW.md"),
                    str(out / "new" / "index.html")], check=True, capture_output=True)
    shutil.copytree(SRC / "docs" / "img", out / "new" / "img")
    shutil.copytree(SRC / "docs" / "img", out / "img")
    # the landing page
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO).replace("__CSS__", manual_html.CSS)
                                    .replace("__MANUAL__", manual), encoding="utf-8")
    emu = SRC / "build" / "emu"
    if not (emu / "tribe.wasm").exists():
        raise SystemExit("no build/emu/tribe.wasm: run web/emu/build.sh (needs Emscripten)")
    shutil.copytree(emu, out / "emu")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
