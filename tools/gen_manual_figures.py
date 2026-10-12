#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The manual's figures, drawn (no photographs: M-VAVE's and Felucca's are not ours to use):

  docs/img/fm1-panel.svg   the FM-1 front panel, with the firmware's real screen in it
  docs/img/fm1-keys.svg    the key bed: what the black and white keys do in TRIBE
  docs/img/screen-*.png    screenshots from the simulator (2x, nearest neighbour)

  tools/gen_manual_figures.py [SHOT_DIR]      (default build/scen: run tests/run_tests.sh first; tests/scenarios/manual.tribe takes the shots)

The panel layout follows the FM-1: two knob pairs and the octave buttons on the left, the
screen in the middle, four knobs and two rows of six buttons on the right, and the keys below:
11 black keys in groups of 3, 2, 3, 2, 1 over 16 white keys (F to G)."""
import base64
import io
import sys
from pathlib import Path

from PIL import Image

SRC = Path(__file__).resolve().parents[1]
OUT = SRC / "docs" / "img"
FONT = "'Barlow Semi Condensed', 'Arial Narrow', Arial, sans-serif"

BODY, TRAY, KEY_W, KEY_B, KNOB, BTN, LABEL = "#3a3d42", "#26282c", "#d9dadc", "#c9cacd", "#1f2124", "#2e3135", "#e8e8ea"
WHITE_N = 16
BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]          # black key k sits after white key BLACK_AFTER[k]
SHOTS = {"m_seq.png": "screen-seq.png", "m_step.png": "screen-step.png", "m_fx.png": "screen-fx.png", "m_env.png": "screen-env.png",
         "m_edit.png": "screen-edit.png", "m_scatter.png": "screen-scatter.png", "m_303.png": "screen-303.png", "m_lfo.png": "screen-lfo.png",
         "m_clips.png": "screen-clips.png", "m_sel.png": "screen-sel.png", "m_arp.png": "screen-arp.png", "m_rec.png": "screen-rec.png",
         "m_seqcfg.png": "screen-seqcfg.png", "m_glo.png": "screen-glo.png", "m_glo_fx.png": "screen-glo-fx.png", "m_save.png": "screen-save.png",
         "splash_c.png": "screen-splash.png"}


def png_data_uri(path, scale=1):
    im = Image.open(path).convert("RGB")
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(b.getvalue()).decode()


def knob(x, y, r, label, what=None):
    """a knob: its name above, what it does in X0X below (the manual's map)"""
    return (f'<g><circle cx="{x}" cy="{y}" r="{r + 5}" fill="{TRAY}"/>'
            f'<circle cx="{x}" cy="{y}" r="{r}" fill="{KNOB}" stroke="#44474c" stroke-width="2"/>'
            f'<line x1="{x}" y1="{y - r + 5}" x2="{x}" y2="{y - r + 15}" stroke="{LABEL}" stroke-width="3" stroke-linecap="round"/>'
            f'<text x="{x}" y="{y - r - 12}" text-anchor="middle" class="lbl">{label}</text>'
            + (f'<text x="{x}" y="{y + r + 22}" text-anchor="middle" class="what">{what}</text>' if what else "") + '</g>')


def what(x, y, t):
    """what a button does, under it (one or two lines, split at |)"""
    a, _, b = t.partition("|")
    return (f'<text x="{x}" y="{y}" text-anchor="middle" class="what">{a}</text>' +
            (f'<text x="{x}" y="{y + 14}" text-anchor="middle" class="what">{b}</text>' if b else ""))


def button(x, y, w, h, label, two=None):
    t = (f'<text x="{x + w / 2}" y="{y + h / 2 + 5}" text-anchor="middle" class="btn">{label}</text>' if not two else
         f'<text x="{x + w / 2}" y="{y + h / 2 - 1}" text-anchor="middle" class="btn small">{label}</text>'
         f'<text x="{x + w / 2}" y="{y + h / 2 + 12}" text-anchor="middle" class="btn small">{two}</text>')
    return f'<g><rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="{BTN}" stroke="#4a4d52" stroke-width="1.5"/>{t}</g>'


def keybed(x0, y0, w, white_label=None, black_label=None, black_sub=None, white_sub=None, h=230):
    """the 16 white keys and 11 black keys as pills, optionally numbered / labelled"""
    s = [f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18" fill="{TRAY}"/>']
    pitch = (w - 24) / WHITE_N
    kw = pitch * 0.78
    for i in range(WHITE_N):
        x = x0 + 12 + i * pitch + (pitch - kw) / 2
        s.append(f'<rect x="{x:.1f}" y="{y0 + h * 0.43:.1f}" width="{kw:.1f}" height="{h * 0.52:.1f}" rx="{kw / 2:.1f}" fill="{KEY_W}"/>')
        if white_label:
            s.append(f'<text x="{x + kw / 2:.1f}" y="{y0 + h * 0.43 + h * 0.52 * 0.55:.1f}" text-anchor="middle" class="key">{white_label(i)}</text>')
        if white_sub:
            s.append(f'<text x="{x + kw / 2:.1f}" y="{y0 + h * 0.43 + h * 0.52 * 0.78:.1f}" text-anchor="middle" class="keysub">{white_sub(i)}</text>')
    for k, a in enumerate(BLACK_AFTER):
        x = x0 + 12 + (a + 1) * pitch - kw / 2
        s.append(f'<rect x="{x:.1f}" y="{y0 + h * 0.06:.1f}" width="{kw:.1f}" height="{h * 0.33:.1f}" rx="{kw / 2:.1f}" fill="{KEY_B}"/>')
        if black_label:
            s.append(f'<text x="{x + kw / 2:.1f}" y="{y0 + h * 0.06 + h * 0.33 * 0.45:.1f}" text-anchor="middle" class="key">{black_label(k)}</text>')
        if black_sub:
            s.append(f'<text x="{x + kw / 2:.1f}" y="{y0 + h * 0.06 + h * 0.33 * 0.78:.1f}" text-anchor="middle" class="keysub">{black_sub(k)}</text>')
    return "".join(s)


STYLE = (f"<style>.lbl{{font:600 17px {FONT};fill:{LABEL};letter-spacing:.06em}}"
         f".btn{{font:600 15px {FONT};fill:{LABEL};letter-spacing:.05em}}.btn.small{{font-size:12px}}"
         f".key{{font:700 17px {FONT};fill:#2a2c30}}.keysub{{font:600 12px {FONT};fill:#4d5056}}"
         f".cap{{font:600 15px {FONT};fill:#9aa0a8;letter-spacing:.04em}}"
         f".what{{font:500 13px {FONT};fill:#f0a043}}</style>")


def panel(screen_png):
    """the FM-1's front panel, with the firmware's screen in it and, under each control, what it does in a word
    or two"""
    W, H = 1000, 720
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" role="img" '
         f'aria-label="The FM-1 front panel, each control labelled with what it does">', STYLE,
         f'<rect x="4" y="4" width="{W - 8}" height="{H - 8}" rx="44" fill="{BODY}"/>',
         knob(105, 92, 24, "MASTER", "Set in GLO"), knob(215, 92, 24, "SELECT", "Set in GLO"),
         knob(105, 210, 24, "PRESETS", "Set in GLO"), knob(215, 210, 24, "ALGORITHM", "Set in GLO"),
         f'<rect x="58" y="284" width="210" height="58" rx="14" fill="{TRAY}"/>',
         button(72, 294, 82, 38, "OCT-"), button(170, 294, 82, 38, "OCT+"),
         what(113, 362, "Page / octave -"), what(211, 362, "Page / octave +"),
         f'<rect x="300" y="44" width="236" height="236" rx="26" fill="#111214"/>',
         f'<image x="318" y="62" width="200" height="200" href="{png_data_uri(screen_png)}" style="image-rendering:pixelated"/>']
    for i in range(4):
        s.append(knob(610 + i * 104, 92, 22, f"KNOB{i + 1}"))
    s.append(what(766, 136, "The page's four values"))
    s.append(f'<rect x="568" y="146" width="400" height="236" rx="18" fill="{TRAY}"/>')
    # each button: what it does, in a word or two (the details: the manual's table)
    top = [("FX", "Effects"), ("SEL", "Engine, mix"), ("ENV", "Envelope"), ("LFO", "LFO"), ("EDIT", "Sound"),
           ("GLO", "Global")]
    bot = [("HOME", "Clips"), ("SAVE", "Save"), ("ARP", "Keys, arp"), ("SEQ", "Steps|hold: config"),
           (("PLAY", "STOP"), "Play"), ("REC", "Record")]
    for i in range(6):
        x = 582 + i * 63
        s.append(button(x, 158, 54, 48, top[i][0]))
        if top[i][1]:
            s.append(what(x + 27, 224, top[i][1]))
        b = bot[i][0]
        s.append(button(x, 264, 54, 48, b[0], b[1]) if isinstance(b, tuple) else button(x, 264, 54, 48, b))
        if bot[i][1]:
            s.append(what(x + 27, 330, bot[i][1]))
    s.append(keybed(36, 400, W - 72, h=270))
    s.append(what(W / 2, 696, "Black keys: the ten tracks (the last: MUTE)   ·   White keys: steps, clips, scatter FX or notes"))
    s.append("</svg>")
    return "".join(s)


def keymap():
    W, H = 1000, 330
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" role="img" '
         f'aria-label="The keys in TRIBE: black keys 1 to 10 select the tracks, the eleventh mutes; white keys 1 to 16 are steps, clips or scatter effects">',
         STYLE, f'<rect x="0" y="0" width="{W}" height="{H}" rx="28" fill="{BODY}"/>',
         keybed(20, 20, W - 40, white_label=lambda i: str(i + 1), black_label=lambda k: str(k + 1) if k < 10 else "M",
                black_sub=lambda k: "track" if k < 10 else "mute", white_sub=lambda i: "step", h=250),
         f'<text x="40" y="306" class="cap">BLACK KEYS: THE TEN TRACKS, THE LAST ONE IS MUTE   ·   WHITE KEYS: STEPS (CLIPS PAGE: CLIPS)</text>',
         "</svg>"]
    return "".join(s)


def main():
    shots = Path(sys.argv[1]) if len(sys.argv) > 1 else SRC / "build" / "scen"
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "fm1-panel.svg").write_text(panel(shots / "m_seq.png"))
    (OUT / "fm1-keys.svg").write_text(keymap())
    for src, dst in SHOTS.items():
        Image.open(shots / src).convert("RGB").resize((480, 480), Image.NEAREST).save(OUT / dst, optimize=True)
    print(f"figures in {OUT}: fm1-panel.svg, fm1-keys.svg, {len(SHOTS)} screenshots")


if __name__ == "__main__":
    main()
