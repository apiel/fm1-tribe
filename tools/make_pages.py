#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the TRIBE site for GitHub Pages (https://apiel.github.io/fm1-tribe/):

  index.html                  the whole site on one page: what TRIBE is, the emulator, the installer, the manual
                              (docs/MANUAL.md). The emulator and the installer are the pages below, in frames
  emu/                        TRIBE in the browser (web/emu: build/emu, from web/emu/build.sh); the page's frame
  install/index.html          the web installer (web/tribe_installer.html, with fm1pkg.js, fm1ota.js
                              and the package's metadata inlined; Chrome or Edge, Web MIDI); the page's frame
  manual/index.html           sends old links to the manual (and its #anchors) to the page above
  new/index.html, img/        what's new in each version (tools/manual_html.py, from docs/NEW.md): a page of its own
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

DISCORD_ICON = ('<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20.3 4.4A19.8 19.8 0 0 0 15.4 3l-.2.5a18.3 18.3 0 0 0-6.4 0L8.6 3A19.8 19.8 0 0 0 3.7 4.4C.6 9 -.2 13.5.2 18a19.9 19.9 0 0 0 6 3l.5-.8c-.9-.3-1.7-.7-2.5-1.2l.6-.5a14.2 14.2 0 0 0 12.4 0l.6.5c-.8.5-1.6.9-2.5 1.2l.5.8a19.9 19.9 0 0 0 6-3c.5-5.2-.8-9.7-3.5-13.6ZM8.3 15.3c-1.2 0-2.1-1.1-2.1-2.4s.9-2.4 2.1-2.4 2.1 1.1 2.1 2.4-.9 2.4-2.1 2.4Zm7.4 0c-1.2 0-2.1-1.1-2.1-2.4s.9-2.4 2.1-2.4 2.1 1.1 2.1 2.4-.9 2.4-2.1 2.4Z"/></svg>')

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
html { scroll-padding-top: 64px; }
main { max-width: 74rem; }
body { padding-block: 32px 64px; }
h1 { font-size: 3rem; margin: 0 0 .4rem; }
.topbar { position: sticky; top: 0; z-index: 5; margin: 0 -16px 1.6rem; padding: 10px 16px; background: var(--ground);
          border-bottom: 1px solid var(--rule); display: flex; flex-wrap: wrap; gap: 6px 22px; align-items: center;
          font: 600 1.05rem "Barlow Semi Condensed", "Barlow", sans-serif; letter-spacing: .03em; }
.topbar a { text-decoration: none; }
.topbar a:hover { text-decoration: underline; }
.topbar .brand { color: var(--ink); font-weight: 700; font-size: 1.2rem; margin-right: 6px; }
.topbar .spacer { flex: 1; }
a.discord { display: inline-flex; align-items: center; gap: .5em; background: #5865f2; color: #fff; border-radius: 999px;
            padding: .5em 1.2em; font: 600 1.05rem "Barlow Semi Condensed", "Barlow", sans-serif; letter-spacing: .03em; text-decoration: none; }
a.discord:hover, a.discord:focus-visible { background: #4752c4; text-decoration: none; outline: none; }
a.discord svg { width: 1.3em; height: 1.3em; fill: currentColor; }
.topbar a.discord { padding: .25em .9em; }
.hero { display: flex; gap: 32px; align-items: center; margin: 0 0 1.6rem; }
.hero img { width: 200px; height: auto; border-radius: 12px; box-shadow: 0 0 0 1px var(--rule); flex: none; }
.hero .lede { margin: 0 0 1.2rem; max-width: 42rem; font-size: 1.1rem; }
.cta { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; }
a.btn { display: inline-block; background: var(--accent); color: #1b1205; border-radius: 999px; padding: .5em 1.2em;
        font: 600 1.05rem "Barlow Semi Condensed", "Barlow", sans-serif; letter-spacing: .03em; text-decoration: none; }
a.btn.alt { background: none; color: var(--ink); box-shadow: inset 0 0 0 1px var(--rule); }
a.btn:hover, a.btn:focus-visible { filter: brightness(1.1); outline: none; }
@media (max-width: 640px) { .hero { flex-direction: column; align-items: flex-start; gap: 16px; } .hero img { width: 140px; } h1 { font-size: 2.4rem; } }
.status { margin: 0 0 2rem; max-width: 60rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; margin: 0 0 1rem; }
.shots img { width: 100%; height: auto; border-radius: 8px; display: block; background: #000; }
p.small { color: var(--muted); font-size: .9rem; }
section.app > h2 { margin-top: 3rem; }
section.app iframe { display: block; width: 100%; max-width: 62rem; border: 0; min-height: 360px; }
section.app > p { max-width: 60rem; }
.dl { margin: 1rem 0 0; }
#manual { max-width: 56rem; }
.callout-discord { margin: 3rem 0 0; padding: 24px 28px; background: var(--sheet); border: 1px solid var(--rule); border-radius: 14px;
                   display: flex; flex-wrap: wrap; gap: 14px 28px; align-items: center; justify-content: space-between; }
.callout-discord h2 { border: 0; margin: 0 0 .2rem; padding: 0; }
.callout-discord p { margin: 0; color: var(--muted); }
</style>
</head>
<body>
<main>
<nav class="topbar" aria-label="On this page"><a class="brand" href="#top">TRIBE</a>
  <a href="#emulator">Emulator</a> <a href="#install">Install</a> <a href="#manual">Manual</a> <a href="new/">What's new</a>
  <a href="__REPO__">Source</a><span class="spacer"></span>
  <a class="discord" href="https://discord.gg/fzHTbjh6x">__DISCORD__ Discord</a></nav>
<div class="hero" id="top">
  <img src="img/screen-splash.png" alt="The TRIBE splash screen">
  <div>
    <h1>TRIBE</h1>
    <p class="lede">Groovebox firmware for the M-VAVE FM-1. Ten tracks, and any engine on any track: kicks, the 909 and
808 voices, the 303. Sixteen clips per track, scatter effects, two master effects every track can send to, and four
master knobs you assign to the volume, a DJ filter, the effects or the tempo.</p>
    <div class="cta"><a class="btn" href="#emulator">Try it in the browser</a> <a class="btn alt" href="#install">Install v__VERSION__</a>
      <a class="btn alt" href="new/">What's new</a> <a class="discord" href="https://discord.gg/fzHTbjh6x">__DISCORD__ Join the Discord</a></div>
  </div>
</div>
<div class="shots">
  <img src="img/screen-seq.png" alt="The sequencer: sixteen steps and a track's knobs">
  <img src="img/screen-clips.png" alt="The clips: ten tracks, sixteen clips each">
  <img src="img/screen-sel.png" alt="The mix: every track's level and its two sends">
  <img src="img/screen-glo-fx.png" alt="The global settings: FX 1 and FX 2">
</div>
<div class="status">TRIBE is a beta, built on X0X and Felucca. It installs and uninstalls the way they do, and the installer can put
M-VAVE's own firmware back. Installing is at your own risk.</div>

<section class="app" aria-labelledby="emulator">
<h2 id="emulator">Try it in the browser</h2>
<p>The same code the FM-1 runs, with sound. Mouse, touch or keyboard; no FM-1 needed.</p>
<iframe src="emu/?embed" title="TRIBE emulator" class="embed" allow="autoplay; midi" loading="lazy"></iframe>
</section>

<section class="app" aria-labelledby="install">
<h2 id="install">Install v__VERSION__ on your FM-1</h2>
<p>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.
<a href="new/">What's new in this version?</a></p>
<iframe src="install/?embed" title="TRIBE web installer" class="embed" allow="midi" loading="lazy"></iframe>
<p class="small dl">For the command-line installer: <a href="firmware/__PKG__">download __PKG__</a>, then
<code>python3 tools/fm1_install.py __PKG__</code>. To go back to the official firmware, use the installer's stock-firmware step above, or M-VAVE's updater, M-UPGRADE, from
<a href="https://www.m-vave.com/download">m-vave.com/download</a>. M-VAVE and FM-1 are trademarks of their owners.</p>
</section>

<div id="manual">
__MANUAL__
</div>

<div class="callout-discord">
  <div><h2>Join the fm1-tribe Discord</h2><p>Questions, bugs, ideas, and what you make with it.</p></div>
  <a class="discord" href="https://discord.gg/fzHTbjh6x">__DISCORD__ Join the Discord</a>
</div>
<script>
// the emulator and the installer sit in frames; each tells its height (they depend on the width)
addEventListener("message", (e) => {
  const h = e.data && e.data.tribeHeight;
  if (!h) return;
  for (const f of document.querySelectorAll("iframe.embed")) if (f.contentWindow === e.source) f.style.height = Math.ceil(h) + "px";
});
</script>
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
                                    .replace("__REPO__", REPO).replace("__DISCORD__", DISCORD_ICON).replace("__CSS__", manual_html.CSS)
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
