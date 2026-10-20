// SPDX-License-Identifier: GPL-3.0-only
// The browser build, run in node: tribe.wasm boots, makes sound when PLAY is pressed, draws its screen,
// and answers buttons and keys. Run from the repo root after web/emu/build.sh:
//   node tests/web_emu_test.mjs
import { readFileSync } from "node:fs";
let failed = 0;
const ok = (c, w) => { console.log(`${w.padEnd(56)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const wasi = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(readFileSync("build/emu/tribe.wasm"), {
  wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports;
if (ex._initialize) ex._initialize();
ex.web_master(4096);
ex.web_boot();
const BTN = { PLAY: 10, SEQ: 9, SEL: 1, FX: 0, HOME: 6 };
const render = (frames) => {
  let peak = 0;
  for (let n = 0; n < frames; n += 128) {
    ex.web_render(128);
    const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), 128);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
};
ok(render(4096) === 0, "silent before PLAY");
ok(ex.web_blits() > 0, "the screen was drawn");
ex.web_buttons(1 << BTN.PLAY); render(2048); ex.web_buttons(0);
const peak = render(44100);
ok(peak > 0.05 && peak < 1.5, `sound after PLAY (peak ${peak.toFixed(2)})`);
const lit = ex.web_lit_buttons();
ok((lit >> BTN.PLAY) & 1, "PLAY is lit");
ok((lit >> BTN.SEL) & 1, "SEL is lit (the mix is the first screen)");
ok(((ex.web_dim_buttons() >> BTN.FX) & 1) === 1, "FX is dim: the engine has FX pages");
const fb = new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240);
ok(fb.some((v) => v !== 0), "the framebuffer has pixels");
const b0 = ex.web_blits();
ex.web_buttons(1 << BTN.HOME); render(2048); ex.web_buttons(0); render(2048);
ok(ex.web_blits() > b0, "HOME redraws the screen");
console.log(failed ? `${failed} FAILED` : "all ok");
process.exit(failed ? 1 : 0);
