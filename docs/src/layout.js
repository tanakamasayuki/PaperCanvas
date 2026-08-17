// layout.js — the layout rules, mirrored from PaperCanvas.
//
// This is the one place where the tool re-implements the library. Glyphs come
// from lgfx-font-tool, which is verified against the real LovyanGFX, so what is
// left here is where things go: column widths, wrapping, alignment, stacking.
//
// Every rule below has a counterpart in src/PaperCanvas/. When they disagree the
// preview lies about what will be printed, which is the one failure this whole
// tool exists to prevent — so tests/js_parity/ compares the two on real output.
// If you change something here, change it there too, and vice versa.
//
// References are to docs/CORE_DESIGN.ja.md.

import { textWidth, fontHeight } from '../vendor/lgfx-font-tool.js';

/** Bytes per row of a 1bpp bitmap. PaperCanvas::rowBytes. */
export const rowBytes = (width) => (width + 7) >> 3;

/** Millimetres to pixels. PaperCanvas::mmToPx — rounds to nearest. */
export const mmToPx = (mm, dpi) => Math.floor((mm * dpi) / 25.4 + 0.5);
export const pxToMm = (px, dpi) => (px * 25.4) / dpi;

export const ALIGN = ['left', 'center', 'right'];
export const VALIGN = ['top', 'middle', 'bottom'];

/** Column units, matching PaperCanvas::Column::Unit. */
export const UNITS = ['px', 'percent', 'rest', 'auto'];

//----------------------------------------------------------------------------
// Text
//----------------------------------------------------------------------------

/**
 * Split text into codepoints the way LovyanGFX does, so a surrogate pair counts
 * once rather than twice.
 */
function chars(text) {
  return Array.from(text ?? '');
}

/**
 * Break `text` so no line exceeds `limit` px, returning the lines.
 *
 * PaperCanvas resolves wrapping once, when the element is added, and stores the
 * result (storeWrapped). Doing the same here — rather than re-deciding while
 * drawing — is what keeps the two from drifting apart.
 *
 * Simple character wrapping, no word breaking: that is what the library does,
 * and Japanese receipts are the main use.
 */
export function wrapText(font, text, limit, style = {}) {
  if (!limit) return String(text ?? '').split('\n');
  const out = [];
  for (const paragraph of String(text ?? '').split('\n')) {
    let line = '';
    let lineW = 0;
    for (const ch of chars(paragraph)) {
      const w = textWidth(font, ch, style);
      if (lineW && lineW + w > limit) {
        out.push(line);
        line = '';
        lineW = 0;
      }
      line += ch;
      lineW += w;
    }
    out.push(line);
  }
  return out;
}

/**
 * Truncate each line to `limit` px at a character boundary.
 *
 * The counterpart of storeClipped. Cutting between characters rather than
 * pixels avoids leaving half a glyph at the edge, and matches the library.
 */
export function clipText(font, text, limit, style = {}) {
  if (!limit) return String(text ?? '').split('\n');
  return String(text ?? '').split('\n').map((paragraph) => {
    let line = '';
    let lineW = 0;
    for (const ch of chars(paragraph)) {
      const w = textWidth(font, ch, style);
      if (lineW + w > limit) break;
      line += ch;
      lineW += w;
    }
    return line;
  });
}

/** Wrapped or clipped, per the element's setting. PageBase::storeFitted. */
export function fitText(font, text, limit, wrap, style = {}) {
  return wrap ? wrapText(font, text, limit, style) : clipText(font, text, limit, style);
}

/** Height of a block of lines. PageBase::textBlockHeight. */
export function blockHeight(font, lines, lineSpacing = 0, style = {}) {
  const lh = fontHeight(font, style);
  const gap = lineSpacing > 0 ? lineSpacing : 0;
  return lines.length * lh + (lines.length - 1) * gap;
}

/** Where a line starts, given the box and the alignment. */
export function alignX(align, boxX, boxW, lineW) {
  if (align === 'center') return boxX + ((boxW - lineW) >> 1);
  if (align === 'right') return boxX + boxW - lineW;
  return boxX;
}

/** Where a block starts vertically. Labels only; a receipt box is exact. */
export function alignY(valign, boxY, boxH, blockH) {
  if (valign === 'middle') return boxY + ((boxH - blockH) >> 1);
  if (valign === 'bottom') return boxY + boxH - blockH;
  return boxY;
}

//----------------------------------------------------------------------------
// Columns
//----------------------------------------------------------------------------

/**
 * Resolve column widths to pixels. PageBase::resolveWidths.
 *
 * The order is fixed and every division truncates, so the result is exact
 * rather than dependent on float rounding — the library does the same in
 * integer arithmetic, and the two have to agree to the pixel.
 *
 * @param {object[]} columns  {unit, value, align, leader}
 * @param {string[]} cells    this row's text, for Auto columns
 * @param {number} boxW
 * @param {number} gap
 * @returns {{widths: number[], clipped: boolean}}
 */
export function resolveColumns(font, columns, cells, boxW, gap, style = {}) {
  const n = columns.length;
  if (!n) return { widths: [], clipped: false };

  const gaps = gap * (n - 1);
  const usable = Math.max(0, boxW - gaps);

  const widths = new Array(n).fill(0);
  let taken = 0;
  let restCount = 0;

  for (let i = 0; i < n; i++) {
    const col = columns[i];
    let w = 0;
    switch (col.unit) {
      case 'px':
        w = Math.max(0, Math.floor(col.value));
        break;
      case 'percent':
        w = Math.floor((usable * Math.max(0, col.value)) / 100);
        break;
      case 'auto':
        w = cells[i] ? textWidth(font, cells[i], style) : 0;
        break;
      case 'rest':
      default:
        restCount++;
        break;
    }
    widths[i] = Math.min(w, usable);
    taken += widths[i];
  }

  let clipped = false;
  if (taken > usable) {
    // Shrink from the last column back, so the leading columns — the ones a
    // reader scans down — keep their position.
    let excess = taken - usable;
    for (let i = n - 1; i >= 0 && excess > 0; i--) {
      const give = Math.min(widths[i], excess);
      widths[i] -= give;
      excess -= give;
    }
    clipped = true;
    taken = usable;
  }

  let left = usable - taken;
  if (restCount) {
    const each = Math.floor(left / restCount);
    let extra = left - each * restCount;
    for (let i = 0; i < n; i++) {
      if (columns[i].unit !== 'rest') continue;
      widths[i] = each + extra;
      extra = 0; // the remainder all goes to the first Rest column
    }
  } else if (left && n) {
    widths[n - 1] += left;
  }

  return { widths, clipped };
}

/**
 * The implicit layout, used when no columns are declared: the first cell takes
 * the rest and the others exactly what they need. PageBase::columnsFor.
 */
export function implicitColumns(n) {
  const cols = [{ unit: 'rest', value: 0, align: 'left', leader: '' }];
  for (let i = 1; i < n; i++) {
    cols.push({
      unit: 'auto',
      value: 0,
      align: i + 1 === n ? 'right' : 'center',
      leader: '',
    });
  }
  return cols;
}
