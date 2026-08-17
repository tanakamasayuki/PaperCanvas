// render.js — turn a document into the 1bpp page the printer would produce.
//
// Same bit layout as PaperCanvas: bit = 1 is black, MSB first, rows start on a
// byte boundary every rowBytes. lgfx-font-tool's Bitmap uses that layout too,
// so glyphs are blitted straight in with no conversion.
//
// Elements here mirror PaperCanvas's, and each one resolves its own height as
// it is laid out — the same rule the library follows, and what makes the
// receipt's height answerable before rendering.

import {
  loadFont, createBitmap, drawString, textWidth, fontHeight,
} from '../vendor/lgfx-font-tool.js';

import {
  rowBytes, fitText, blockHeight, alignX, alignY,
  resolveColumns, implicitColumns,
} from './layout.js';

//----------------------------------------------------------------------------
// Font cache
//----------------------------------------------------------------------------

// Two maps: one of in-flight loads so a font is only fetched once, and one of
// resolved fonts so drawing can be synchronous. Rendering has to be synchronous
// because it runs per keystroke, and awaiting inside the layout would let two
// renders interleave and produce a half-updated page.
const loading = new Map();
const loaded = new Map();

/** Fonts are fetched once and reused; the CJK ones are a few hundred KB. */
export async function getFont(name) {
  if (loaded.has(name)) return loaded.get(name);
  if (!loading.has(name)) {
    loading.set(name, loadFont(name).then((f) => { loaded.set(name, f); return f; }));
  }
  return loading.get(name);
}

/** Load every font a document mentions, so rendering can then be synchronous. */
export async function preloadFonts(doc) {
  const names = new Set([doc.font]);
  const visit = (o) => { if (o?.font) names.add(o.font); };
  for (const el of doc.elements ?? []) {
    visit(el);
    if (el.headerFont) names.add(el.headerFont);
    for (const col of el.columns ?? []) visit(col);
  }
  await Promise.all([...names].filter(Boolean).map(getFont));
}

/** The resolved font, or null if it has not been preloaded. */
const fontOf = (name) => loaded.get(name) ?? null;

//----------------------------------------------------------------------------
// Page
//----------------------------------------------------------------------------

export function createPage(width, height) {
  return {
    width,
    height,
    stride: rowBytes(width),
    data: new Uint8Array(rowBytes(width) * Math.max(0, height)),
  };
}

export function getPixel(page, x, y) {
  if (x < 0 || y < 0 || x >= page.width || y >= page.height) return 0;
  return (page.data[y * page.stride + (x >> 3)] >> (7 - (x & 7))) & 1;
}

export function setPixel(page, x, y, on) {
  if (x < 0 || y < 0 || x >= page.width || y >= page.height) return;
  const i = y * page.stride + (x >> 3);
  const mask = 0x80 >> (x & 7);
  if (on) page.data[i] |= mask;
  else page.data[i] &= ~mask;
}

export function fillRect(page, x, y, w, h, on = true) {
  for (let yy = y; yy < y + h; yy++) {
    for (let xx = x; xx < x + w; xx++) setPixel(page, xx, yy, on);
  }
}

/** Blit a lgfx-font-tool bitmap. Same layout, so this is a bit copy. */
function blit(page, bmp, dx, dy) {
  for (let y = 0; y < bmp.height; y++) {
    for (let x = 0; x < bmp.width; x++) {
      if ((bmp.data[y * bmp.stride + (x >> 3)] >> (7 - (x & 7))) & 1) {
        setPixel(page, dx + x, dy + y, true);
      }
    }
  }
}

/** Draw one line of text at (x, y), y being the top of the line box. */
function drawLine(page, font, text, x, y, style) {
  if (!text) return;
  const w = textWidth(font, text, style);
  const h = fontHeight(font, style);
  if (w <= 0 || h <= 0) return;
  const bmp = createBitmap(w, h, 1);
  drawString(bmp, font, text, 0, 0, style);
  blit(page, bmp, x, y);
}

//----------------------------------------------------------------------------
// Elements
//----------------------------------------------------------------------------

const styleOf = (el, doc) => ({
  sizeX: el.size ?? doc.size ?? 1,
  sizeY: el.size ?? doc.size ?? 1,
});

/**
 * How tall an element is. Resolved before drawing, exactly as PaperCanvas
 * resolves it when the element is added — so a receipt's height is known
 * without rendering, and the two agree on where everything lands.
 */
export function measureElement(el, doc, contentW) {
  const font = fontOf(el.font ?? doc.font);
  if (!font) return 0;
  const style = styleOf(el, doc);

  switch (el.type) {
    case 'text': {
      const lines = fitText(font, resolveText(el, doc), contentW, el.wrap ?? doc.wrap, style);
      return blockHeight(font, lines, el.lineSpacing ?? 0, style);
    }
    case 'row': {
      const cells = rowCells(el, doc);
      const cols = el.columns?.length ? el.columns : implicitColumns(cells.length);
      const gap = el.columnGap ?? doc.columnGap ?? 8;
      const { widths } = resolveColumns(font, cols, cells, contentW, gap, style);
      let h = 0;
      cells.forEach((cell, i) => {
        const lines = fitText(font, cell, widths[i], el.wrap ?? false, style);
        h = Math.max(h, blockHeight(font, lines, el.lineSpacing ?? 0, style));
      });
      return h;
    }
    case 'image':
      return el.bitmap ? el.bitmap.height : 0;
    case 'space':
      return el.height ?? 0;
    case 'line':
      return el.thickness ?? 1;
    case 'rule':
      return fontHeight(font, style);
    default:
      return 0;
  }
}

/** Static text, or the sample value standing in for a bound field. */
export function resolveText(el, doc) {
  if (el.bind) return doc.sample?.[el.bind] ?? `{${el.bind}}`;
  return el.text ?? '';
}

function rowCells(el, doc) {
  return (el.cells ?? []).map((c) =>
    c.bind ? (doc.sample?.[c.bind] ?? `{${c.bind}}`) : (c.text ?? ''));
}

/** Draw one element into `box`. Height comes from measureElement. */
export function drawElement(page, el, doc, box) {
  const font = fontOf(el.font ?? doc.font);
  if (!font) return;
  const style = styleOf(el, doc);

  switch (el.type) {
    case 'text': {
      const lines = fitText(font, resolveText(el, doc), box.w, el.wrap ?? doc.wrap, style);
      const lh = fontHeight(font, style);
      const step = lh + Math.max(0, el.lineSpacing ?? 0);
      const bh = blockHeight(font, lines, el.lineSpacing ?? 0, style);
      let y = alignY(el.valign ?? 'top', box.y, box.h, bh);
      for (const line of lines) {
        // A line that would not fit whole is not drawn at all — half a line at
        // the edge of a label reads as damage, nothing reads as missing.
        if (y + lh <= box.y + box.h) {
          const w = textWidth(font, line, style);
          drawLine(page, font, line, alignX(el.align ?? 'left', box.x, box.w, w), y, style);
        }
        y += step;
      }
      break;
    }

    case 'row': {
      const cells = rowCells(el, doc);
      const cols = el.columns?.length ? el.columns : implicitColumns(cells.length);
      const gap = el.columnGap ?? doc.columnGap ?? 8;
      const { widths } = resolveColumns(font, cols, cells, box.w, gap, style);
      const lh = fontHeight(font, style);
      const step = lh + Math.max(0, el.lineSpacing ?? 0);

      // Kept so a leader runs from the end of one cell to the start of the
      // next, rather than to the column edge — otherwise the dots would run
      // under the next cell's own padding.
      const textEnd = [];
      const textStart = [];

      let x = box.x;
      cells.forEach((cell, i) => {
        const w = widths[i];
        const lines = fitText(font, cell, w, el.wrap ?? false, style);
        let y = box.y;
        textStart[i] = x + w;
        textEnd[i] = x;
        for (const line of lines) {
          if (!line) { y += step; continue; }
          const lw = textWidth(font, line, style);
          const lx = alignX(cols[i].align ?? 'left', x, w, lw);
          drawLine(page, font, line, lx, y, style);
          textStart[i] = Math.min(textStart[i], lx);
          textEnd[i] = Math.max(textEnd[i], lx + lw);
          y += step;
        }
        x += w + gap;
      });

      // Leaders run on the first line only; a wrapped cell has no single
      // baseline for them to sit on.
      for (let i = 0; i + 1 < cells.length; i++) {
        const lead = cols[i].leader;
        if (!lead) continue;
        const cw = textWidth(font, lead, style);
        if (cw <= 0) continue;
        for (let lx = textEnd[i] + cw; lx + cw <= textStart[i + 1] - cw; lx += cw) {
          drawLine(page, font, lead, lx, box.y, style);
        }
      }
      break;
    }

    case 'image': {
      // Already 1bpp and at its final size: the tool does the reduction, so the
      // device prints these bits unchanged (docs/WEB_TOOL.ja.md §2.4).
      const bmp = el.bitmap;
      if (!bmp) break;
      const x = alignX(el.align ?? 'center', box.x, box.w, bmp.width);
      const y = alignY(el.valign ?? 'top', box.y, box.h, bmp.height);
      blit(page, bmp, x, y);
      break;
    }

    case 'line':
      fillRect(page, box.x, box.y, box.w, el.thickness ?? 1, true);
      break;

    case 'rect':
      if (el.fill) {
        fillRect(page, box.x, box.y, box.w, box.h, true);
      } else {
        const t = el.thickness ?? 1;
        for (let i = 0; i < t; i++) {
          fillRect(page, box.x + i, box.y + i, box.w - 2 * i, 1, true);
          fillRect(page, box.x + i, box.y + box.h - 1 - i, box.w - 2 * i, 1, true);
          fillRect(page, box.x + i, box.y + i, 1, box.h - 2 * i, true);
          fillRect(page, box.x + box.w - 1 - i, box.y + i, 1, box.h - 2 * i, true);
        }
      }
      break;

    case 'rule': {
      const ch = el.char || '-';
      const cw = textWidth(font, ch, style);
      if (cw <= 0) break;
      const n = Math.floor(box.w / cw);
      // Centre the leftover, so a rule that does not divide evenly is not
      // lopsided — a separator hugging one margin reads as a mistake.
      let x = box.x + ((box.w - n * cw) >> 1);
      for (let i = 0; i < n; i++) {
        drawLine(page, font, ch, x, box.y, style);
        x += cw;
      }
      break;
    }

    case 'space':
    default:
      break;
  }
}

//----------------------------------------------------------------------------
// Documents
//----------------------------------------------------------------------------

/**
 * Expand a receipt into a flat element list.
 *
 * The table is the point: one row is designed, and it is emitted once per item
 * in the sample data. That is exactly what the generated C++ does at run time
 * with the caller's array (docs/WEB_TOOL.ja.md §2.2), so what is previewed here
 * is the same loop.
 */
export function expandReceipt(doc) {
  const out = [];
  for (const el of doc.elements ?? []) {
    if (el.type !== 'table') {
      out.push(el);
      continue;
    }
    const cols = el.columns ?? [];
    if (el.showHeader) {
      out.push({
        type: 'row',
        columns: cols,
        columnGap: el.columnGap,
        font: el.headerFont ?? el.font,
        size: el.headerSize ?? el.size,
        cells: cols.map((c) => ({ text: c.header ?? '' })),
      });
      if (el.headerRule) out.push({ type: 'line', thickness: 1 });
    }
    for (const item of doc.sampleItems ?? []) {
      out.push({
        type: 'row',
        columns: cols,
        columnGap: el.columnGap,
        font: el.font,
        size: el.size,
        wrap: el.wrap,
        cells: cols.map((c) => ({ text: item[c.field] ?? '' })),
      });
    }
    if (el.footerRule) out.push({ type: 'line', thickness: 1 });
  }
  return out;
}

/** Lay out a receipt: stack elements, resolving each height as it is placed. */
export function layoutReceipt(doc) {
  const m = doc.margin ?? { top: 0, bottom: 0, left: 0, right: 0 };
  const contentW = Math.max(0, doc.width - m.left - m.right);
  const placed = [];
  let cursor = 0;
  for (const el of expandReceipt(doc)) {
    const h = measureElement(el, doc, contentW);
    placed.push({ el, box: { x: m.left, y: m.top + cursor, w: contentW, h } });
    cursor += h;
  }
  const height = placed.length ? m.top + cursor + m.bottom : 0;
  return { placed, height, contentW };
}

export function renderReceipt(doc) {
  const { placed, height } = layoutReceipt(doc);
  const page = createPage(doc.width, height);
  for (const { el, box } of placed) drawElement(page, el, doc, box);
  return page;
}

export function renderLabel(doc) {
  const page = createPage(doc.width, doc.height);
  for (const el of doc.elements ?? []) {
    drawElement(page, el, doc, { x: el.x, y: el.y, w: el.w, h: el.h });
  }
  return page;
}

export const renderPage = (doc) =>
  doc.kind === 'label' ? renderLabel(doc) : renderReceipt(doc);
