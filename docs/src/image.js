// image.js — bring a picture in, reduce it to 1bpp here, embed the result.
//
// The reduction happens in the browser, not on the device. What you see in the
// preview is the byte array that goes into the generated header, and PaperCanvas
// prints those bits unchanged — so there is no second reduction to disagree
// with the first (docs/WEB_TOOL.ja.md §2.4).
//
// The dither matrices are the same ones the library uses, indexed the same way
// (by absolute page position), because an image dithered here and an image
// dithered there have to come out identical.

import { rowBytes } from './layout.js';

/** Bayer 4x4, scaled to 0..255. Mirrors src/PaperCanvas/Dither.h. */
const BAYER4 = [
  8, 136, 40, 168,
  200, 72, 232, 104,
  56, 184, 24, 152,
  248, 120, 216, 88,
];

const BAYER8 = [
  2, 130, 34, 162, 10, 138, 42, 170,
  194, 66, 226, 98, 202, 74, 234, 106,
  50, 178, 18, 146, 58, 186, 26, 154,
  242, 114, 210, 82, 250, 122, 218, 90,
  14, 142, 46, 174, 6, 134, 38, 166,
  206, 78, 238, 110, 198, 70, 230, 102,
  62, 190, 30, 158, 54, 182, 22, 150,
  254, 126, 222, 94, 246, 118, 214, 86,
];

export const MONO_METHODS = ['threshold', 'bayer4x4', 'bayer8x8'];

/**
 * Should this pixel be black?
 *
 * x and y are the pixel's position within the image. The library indexes by
 * absolute page position, which is what makes its output independent of tile
 * boundaries; here the image is reduced once, standalone, so image-local
 * coordinates are the equivalent — and the result is what gets embedded, so
 * nothing downstream re-dithers it at a different offset.
 */
function isBlack(gray, method, threshold, x, y) {
  switch (method) {
    case 'bayer4x4': return gray < BAYER4[((y & 3) << 2) | (x & 3)];
    case 'bayer8x8': return gray < BAYER8[((y & 7) << 3) | (x & 7)];
    case 'threshold':
    default: return gray < threshold;
  }
}

/** Load a File or Blob into an ImageBitmap. */
export async function loadImageFile(file) {
  const bitmap = await createImageBitmap(file);
  return bitmap;
}

/**
 * Scale to (w, h) and reduce to 1bpp.
 *
 * Scaling happens here, at full grey depth, before the reduction — scaling
 * afterwards would resample already-dithered pixels and turn the pattern to
 * mush. The device receives the finished bits and does neither.
 *
 * @returns {{width, height, stride, data: Uint8Array}} the same shape as a
 *          lgfx-font-tool bitmap, so it can be blitted by render.js as-is.
 */
export function reduceToMono(source, w, h, opts = {}) {
  const {
    method = 'threshold',
    threshold = 128,
    invert = false,
    brightness = 0,
    contrast = 1,
  } = opts;

  w = Math.max(1, Math.round(w));
  h = Math.max(1, Math.round(h));

  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, w, h);
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(source, 0, 0, w, h);

  const rgba = ctx.getImageData(0, 0, w, h).data;
  const stride = rowBytes(w);
  const data = new Uint8Array(stride * h);

  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4;
      const a = rgba[i + 3] / 255;
      // Composite onto white: a transparent PNG should read as paper, not black.
      const r = rgba[i] * a + 255 * (1 - a);
      const g = rgba[i + 1] * a + 255 * (1 - a);
      const b = rgba[i + 2] * a + 255 * (1 - a);

      // The same luminance LovyanGFX's grayscale_t uses: (r + 2g + b) / 4.
      let gray = (r + 2 * g + b) / 4;
      gray = (gray - 128) * contrast + 128 + brightness;
      gray = Math.max(0, Math.min(255, gray));
      if (invert) gray = 255 - gray;

      if (isBlack(gray, method, threshold, x, y)) {
        data[y * stride + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
  }

  return { width: w, height: h, stride, data, bpp: 1 };
}

//----------------------------------------------------------------------------
// Output
//----------------------------------------------------------------------------

/**
 * A 1bpp page as a canvas, at an integer zoom.
 *
 * Whole-number zoom only: at a fractional one the browser would resample and
 * some printer dots would look bigger than others, which is exactly the thing
 * the preview is supposed to let you judge.
 */
export function pageToCanvas(page, zoom = 1) {
  const z = Math.max(1, Math.round(zoom));
  const canvas = document.createElement('canvas');
  canvas.width = page.width * z;
  canvas.height = page.height * z;
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#000000';
  for (let y = 0; y < page.height; y++) {
    for (let x = 0; x < page.width; x++) {
      if ((page.data[y * page.stride + (x >> 3)] >> (7 - (x & 7))) & 1) {
        ctx.fillRect(x * z, y * z, z, z);
      }
    }
  }
  return canvas;
}

/** Save the page as a PNG, one image pixel per printer dot. */
export function downloadPng(page, filename, zoom = 1) {
  pageToCanvas(page, zoom).toBlob((blob) => {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  }, 'image/png');
}

export function downloadText(text, filename) {
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}
