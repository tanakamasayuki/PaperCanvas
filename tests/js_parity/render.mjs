// js_parity (JS side) — what the browser tool will preview.
//
// Renders the cases from cases.json with lgfx-font-tool onto a canvas the same
// size as the C++ side's, and prints one hex line per case. The pytest compares
// those lines against the sketch's.
//
// The whole point of the browser tool is that its preview is what gets printed.
// This is where that claim is checked, and it is checked in bytes: 1bpp with no
// antialiasing means there is no "close enough" to hide behind.
//
// Usage:  node render.mjs [cases.json]

import { readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  loadFont, createBitmap, drawString, VERSION,
} from 'lgfx-font-tool';

const HERE = dirname(fileURLToPath(import.meta.url));
const casesPath = process.argv[2] ?? resolve(HERE, 'cases.json');
const spec = JSON.parse(await readFile(casesPath, 'utf8'));

const W = spec.canvasWidth;
const H = spec.canvasHeight;

console.log(`#JS lib=${VERSION} w=${W} h=${H} cases=${spec.cases.length}`);

for (const c of spec.cases) {
  const font = await loadFont(c.font);

  // Same canvas as the C++ side, drawn at the origin with the default
  // top-left datum — which is the datum PaperCanvas sets. Nothing else is
  // applied here, because everything else is PaperCanvas's own layout and is
  // what the comparison is meant to expose.
  const bmp = createBitmap(W, H, 1);
  drawString(bmp, font, c.text, 0, 0, { sizeX: c.size, sizeY: c.size });

  // bmp.stride is (W + 7) >> 3 and the bits are MSB first, which is exactly
  // PaperCanvas's own layout — so the buffer goes out as it is.
  const hex = Buffer.from(bmp.data).toString('hex');
  console.log(`#CASE name=${c.name} data=${hex}`);
}

console.log('#DONE');
