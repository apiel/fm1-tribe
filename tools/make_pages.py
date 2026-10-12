#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the TRIBE site for GitHub Pages (https://apiel.github.io/fm1-tribe/):

  index.html                  what TRIBE is, and the ways in: emulator, install, manual, source
  emu/                        TRIBE in the browser (web/emu: build/emu, from web/emu/build.sh)
  install/index.html          the web installer (web/tribe_installer.html, with fm1pkg.js, fm1ota.js
                              and the package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html, img/     the manual (tools/manual_html.py, from docs/MANUAL.md)
  new/index.html, img/        what's new in each version (the same, from docs/NEW.md)
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

REPO = "https://github.com/apiel/fm1-tribe"

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TRIBE for the FM-1</title>
<meta name="description" content="Ten-track groovebox firmware for the M-VAVE FM-1: any engine on any track, 16 clips per track, scatter effects, two master effects.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&display=swap">
<style>
:root { --ground: #f4f5f6; --ink: #1c1f23; --muted: #5b626a; --rule: #d9dde1; --accent: #c8700a; --card: #fff; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --ground: #131518; --ink: #e6e8eb; --muted: #9aa1a9; --rule: #2d3137; --accent: #f0a043; --card: #1a1d21; color-scheme: dark; }
}
:root[data-theme="dark"] { --ground: #131518; --ink: #e6e8eb; --muted: #9aa1a9; --rule: #2d3137; --accent: #f0a043; --card: #1a1d21; color-scheme: dark; }
body { background: var(--ground); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 48px 64px;
       font: 400 17px/1.6 "Barlow", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 40rem; margin: 0 auto; }
h1 { font: 700 2.6rem/1.1 "Barlow Semi Condensed", "Barlow", sans-serif; margin: 0 0 .4rem; }
.lede { color: var(--muted); margin: 0 0 1.6rem; max-width: 34rem; }
.status { border: 1px solid var(--rule); background: var(--card); border-radius: 8px; padding: 12px 16px; margin: 0 0 2rem; }
.ways { display: grid; gap: 12px; margin: 0 0 2rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--card); border: 1px solid var(--rule);
          border-radius: 10px; padding: 14px 18px; }
.ways a:hover, .ways a:focus-visible { border-color: var(--accent); outline: none; }
.ways .card { background: var(--card); border: 1px solid var(--rule); border-radius: 10px; padding: 14px 18px; }
.ways .card a { color: var(--accent); text-decoration: none; border: 0; padding: 0; background: none; display: inline; }
.ways .card a:hover, .ways .card a:focus-visible { text-decoration: underline; }
.ways .card a.new { font: 400 .95rem "Barlow", sans-serif; margin-left: .5rem; }
.ways strong { font: 600 1.25rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent); display: block; }
.ways span { color: var(--muted); font-size: .95rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin: 0 0 2rem; }
.shots img { width: 100%; height: auto; border-radius: 8px; image-rendering: pixelated; display: block; background: #000; }
p.small { color: var(--muted); font-size: .9rem; }
.video { position: relative; aspect-ratio: 16 / 9; max-width: 100%; margin: 1.5rem 0; border-radius: 6px; overflow: hidden; background: #000; }
.video iframe { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }
a { color: var(--accent); }
</style>
</head>
<body>
<main>
<h1>TRIBE</h1>
<p class="lede">Groovebox firmware for the M-VAVE FM-1. Ten tracks, and any engine on any track: kicks, the 909 and
808 voices, the 303. Sixteen clips per track, scatter effects, two master effects every track can send to, and four
master knobs you assign to the volume, a DJ filter, the effects or the tempo.</p>
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
  <a href="manual/"><strong>Manual</strong><span>Getting started in a few steps, then every page, every effect and every parameter.</span></a>
  <a href="firmware/__PKG__"><strong>Download __PKG__</strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="https://discord.gg/fzHTbjh6x"><strong>Discord</strong><span>Join the fm1-tribe channel: questions, bugs, and what you make with it.</span></a>
  <a href="__REPO__"><strong>Source</strong><span>GitHub, GPL-3.0. A fork of X0X, built on Felucca by Hügelton Instruments.</span></a>
</nav>
<p class="small">To go back to the official firmware, use M-VAVE's updater, M-UPGRADE, from
<a href="https://www.m-vave.com/download">m-vave.com/download</a>. M-VAVE and FM-1 are trademarks of their owners.</p>
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
    # the manual (a complete page; its images beside it)
    subprocess.run([sys.executable, str(SRC / "tools" / "manual_html.py"), str(SRC / "docs" / "MANUAL.md"),
                    str(out / "manual" / "index.html")], check=True, capture_output=True)
    shutil.copytree(SRC / "docs" / "img", out / "manual" / "img")
    # what's new (the same converter; its pictures beside it)
    subprocess.run([sys.executable, str(SRC / "tools" / "manual_html.py"), str(SRC / "docs" / "NEW.md"),
                    str(out / "new" / "index.html")], check=True, capture_output=True)
    shutil.copytree(SRC / "docs" / "img", out / "new" / "img")
    shutil.copytree(SRC / "docs" / "img", out / "img")
    # the landing page
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO), encoding="utf-8")
    emu = SRC / "build" / "emu"
    if not (emu / "tribe.wasm").exists():
        raise SystemExit("no build/emu/tribe.wasm: run web/emu/build.sh (needs Emscripten)")
    shutil.copytree(emu, out / "emu")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), manual/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
