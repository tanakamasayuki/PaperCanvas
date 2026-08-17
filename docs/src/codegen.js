// codegen.js — emit the C++ the user actually pastes into a sketch.
//
// The tool's output is not a picture, it is a header. The shape is: a struct
// holding the parts that change, and a function that lays them out. Fill in the
// struct, call the function, print.
//
//   MyReceiptItem items[] = {{"Coffee", "x2", "960"}};
//   MyReceiptData d = {"Cafe", "2026-08-17", items, 1, "960"};
//   buildMyReceipt(r, d);
//
// Which text is a literal and which becomes a field is the tool's central
// choice: an element is either static or bound to a field, and the struct is
// whatever the bound ones add up to. See docs/WEB_TOOL.ja.md §2.

import { rowBytes } from './layout.js';

//----------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------

/** A safe C identifier. Leading digits get a prefix rather than being dropped. */
export function ident(name, fallback = 'Page') {
  let s = String(name ?? '').replace(/[^A-Za-z0-9_]/g, '_');
  if (!s) s = fallback;
  if (/^[0-9]/.test(s)) s = `_${s}`;
  return s;
}

/**
 * A C string literal.
 *
 * Non-ASCII is emitted as-is rather than escaped: the sources are UTF-8, the
 * fonts are, and `\xNN` escapes of multi-byte characters are unreadable in a
 * file someone has to maintain.
 */
export function cstr(text) {
  return `"${String(text ?? '')
    .replace(/\\/g, '\\\\')
    .replace(/"/g, '\\"')
    .replace(/\n/g, '\\n')
    .replace(/\r/g, '')
    .replace(/\t/g, '\\t')}"`;
}

const ALIGN_CPP = { left: 'Align::Left', center: 'Align::Center', right: 'Align::Right' };
const VALIGN_CPP = { top: 'VAlign::Top', middle: 'VAlign::Middle', bottom: 'VAlign::Bottom' };

function columnExpr(col) {
  const align = ALIGN_CPP[col.align ?? 'left'];
  const leader = col.leader ? `, '${col.leader.replace(/'/g, "\\'")}'` : '';
  switch (col.unit) {
    case 'px': return `Column::px(${Number(col.value) || 0}, ${align}${leader})`;
    case 'percent': return `Column::percent(${Number(col.value) || 0}, ${align}${leader})`;
    case 'auto': return `Column::autoFit(${align}${leader})`;
    case 'rest':
    default: return `Column::rest(${align}${leader})`;
  }
}

/** Text as either a literal or a struct field. */
function textExpr(el, dataVar) {
  return el.bind ? `${dataVar}.${ident(el.bind, 'field')}` : cstr(el.text ?? '');
}

//----------------------------------------------------------------------------
// Images
//----------------------------------------------------------------------------

/**
 * A 1bpp bitmap as a C array plus a PaperCanvas::Bitmap.
 *
 * Already reduced and already at its final size, so the device prints these
 * bits unchanged — no dithering at run time, and the preview and the paper
 * cannot disagree (docs/WEB_TOOL.ja.md §2.4).
 */
export function emitBitmap(prefix, name, bmp) {
  const sym = `${prefix}_${ident(name, 'image')}`;
  const stride = rowBytes(bmp.width);
  const lines = [];
  for (let i = 0; i < bmp.data.length; i += 12) {
    const row = [...bmp.data.slice(i, i + 12)]
      .map((b) => `0x${b.toString(16).padStart(2, '0')}`)
      .join(', ');
    lines.push(`    ${row},`);
  }
  return [
    `// ${bmp.width}x${bmp.height}, 1bpp, bit = 1 is black, MSB first.`,
    `// Reduced in the browser; PaperCanvas prints it as-is.`,
    `static const uint8_t ${sym}_data[] = {`,
    ...lines,
    `};`,
    `static const PaperCanvas::Bitmap ${sym} = {`,
    `    ${sym}_data, ${bmp.width}, ${bmp.height}, ${stride}};`,
    '',
  ].join('\n');
}

//----------------------------------------------------------------------------
// Field collection
//----------------------------------------------------------------------------

/** The bound fields a document needs, in the order they first appear. */
export function collectFields(doc) {
  const fields = [];
  const seen = new Set();
  const add = (name) => {
    const id = ident(name, 'field');
    if (!seen.has(id)) { seen.add(id); fields.push(id); }
  };
  for (const el of doc.elements ?? []) {
    if (el.bind) add(el.bind);
    for (const c of el.cells ?? []) if (c.bind) add(c.bind);
  }
  return fields;
}

/** The table's per-item fields, which become the Item struct. */
export function collectItemFields(doc) {
  const table = (doc.elements ?? []).find((e) => e.type === 'table');
  if (!table) return null;
  const fields = [];
  const seen = new Set();
  for (const col of table.columns ?? []) {
    const id = ident(col.field ?? 'value', 'value');
    if (!seen.has(id)) { seen.add(id); fields.push(id); }
  }
  return { table, fields };
}

//----------------------------------------------------------------------------
// Element emission
//----------------------------------------------------------------------------

function fontExpr(name) {
  return name ? `&fonts::${name}` : 'nullptr';
}

function emitReceiptElement(el, doc, dataVar, out) {
  const indent = '  ';
  switch (el.type) {
    case 'text': {
      const opts = [];
      if (el.font && el.font !== doc.font) opts.push(`.font = ${fontExpr(el.font)}`);
      if (el.size && el.size !== doc.size) opts.push(`.size = ${el.size}f`);
      if (el.align && el.align !== 'left') opts.push(`.align = ${ALIGN_CPP[el.align]}`);
      if (el.wrap !== undefined && el.wrap !== doc.wrap) opts.push(`.wrap = ${el.wrap}`);
      const optStr = opts.length ? `, {${opts.join(', ')}}` : '';
      out.push(`${indent}r.addText(${textExpr(el, dataVar)}${optStr});`);
      break;
    }
    case 'space':
      out.push(`${indent}r.addSpace(${el.height ?? 0});`);
      break;
    case 'line':
      out.push(`${indent}r.addLine(${el.thickness ?? 1});`);
      break;
    case 'rule':
      out.push(`${indent}r.addRule('${(el.char || '-').replace(/'/g, "\\'")}');`);
      break;
    case 'image':
      if (el.symbol) out.push(`${indent}r.addImage(${el.symbol});`);
      break;
    case 'row': {
      const cells = (el.cells ?? []).map((c) => textExpr(c, dataVar));
      out.push(`${indent}r.addRow(${cells.join(', ')});`);
      break;
    }
    default:
      break;
  }
}

/**
 * The table: header, then one row per item, then the rules around them.
 *
 * The loop is the whole point — the caller supplies an array and gets that many
 * rows. The preview does the same expansion over the sample data, so what is on
 * screen is this loop with different input.
 */
function emitTable(table, doc, dataVar, itemStruct, out) {
  const cols = table.columns ?? [];
  out.push('');
  out.push('  // Table: one row per item, however many the caller passes.');
  out.push('  static const Column columns[] = {');
  for (const col of cols) out.push(`      ${columnExpr(col)},`);
  out.push('  };');
  out.push(`  r.setColumns(columns, ${cols.length});`);
  if (table.columnGap !== undefined) out.push(`  r.setColumnGap(${table.columnGap});`);

  if (table.showHeader) {
    const headers = cols.map((c) => cstr(c.header ?? ''));
    out.push(`  r.addRow(${headers.join(', ')});`);
    if (table.headerRule) out.push('  r.addLine(1);');
  }

  out.push(`  for (size_t i = 0; i < ${dataVar}.itemCount; ++i) {`);
  out.push(`    const ${itemStruct}& it = ${dataVar}.items[i];`);
  const cells = cols.map((c) => `it.${ident(c.field ?? 'value', 'value')}`);
  out.push(`    r.addRow(${cells.join(', ')});`);
  out.push('  }');

  if (table.footerRule) out.push('  r.addLine(1);');
  out.push('  r.clearColumns();');
  out.push('');
}

//----------------------------------------------------------------------------
// Generators
//----------------------------------------------------------------------------

function header(name, doc, extra = []) {
  return [
    `// ${name}.h — generated by the PaperCanvas layout tool.`,
    '//   https://tanakamasayuki.github.io/PaperCanvas/',
    '//',
    '// Fill in the data struct and call the build function. Regenerating this',
    '// file overwrites it, so keep your own code elsewhere.',
    ...extra,
    '#pragma once',
    '',
    '#include <PaperCanvas.h>',
    '',
  ];
}

export function generateReceipt(doc) {
  const name = ident(doc.name, 'MyReceipt');
  const dataStruct = `${name}Data`;
  const itemInfo = collectItemFields(doc);
  const itemStruct = `${name}Item`;
  const fields = collectFields(doc);

  const out = header(name, doc, [
    `//`,
    `// Receipt, ${doc.width} dots wide.`,
    '']);

  out.push('using PaperCanvas::Align;');
  out.push('using PaperCanvas::Column;');
  out.push('');

  for (const el of doc.elements ?? []) {
    if (el.type === 'image' && el.bitmap && el.symbol) {
      out.push(emitBitmap(name, el.name ?? 'image', el.bitmap));
    }
  }

  if (itemInfo) {
    out.push(`/// One line of the table. The caller passes an array of these.`);
    out.push(`struct ${itemStruct} {`);
    for (const f of itemInfo.fields) out.push(`  const char* ${f};`);
    out.push('};');
    out.push('');
  }

  out.push('/// Everything the layout needs from the caller.');
  out.push(`struct ${dataStruct} {`);
  for (const f of fields) out.push(`  const char* ${f};`);
  if (itemInfo) {
    out.push(`  const ${itemStruct}* items;`);
    out.push('  size_t itemCount;');
  }
  out.push('};');
  out.push('');

  out.push(`/// Build the receipt. Call r.build() or r.stream() afterwards.`);
  out.push(`inline void build${name}(PaperCanvas::Receipt& r, const ${dataStruct}& d) {`);
  if (doc.font) out.push(`  r.setFont(${fontExpr(doc.font)});`);
  if (doc.size && doc.size !== 1) out.push(`  r.setTextSize(${doc.size}f);`);
  const m = doc.margin ?? {};
  if (m.top || m.bottom || m.left || m.right) {
    out.push(`  r.setMargin(${m.top ?? 0}, ${m.bottom ?? 0}, ${m.left ?? 0}, ${m.right ?? 0});`);
  }
  if (doc.wrap !== undefined) out.push(`  r.setWrap(${doc.wrap});`);
  out.push('');

  let lastAlign = 'left';
  for (const el of doc.elements ?? []) {
    if (el.type === 'table') {
      emitTable(el, doc, 'd', itemStruct, out);
      continue;
    }
    // setAlign carries forward in PaperCanvas, so only emit it when it changes.
    const align = el.align ?? 'left';
    if (el.type === 'text' && align !== lastAlign) {
      out.push(`  r.setAlign(${ALIGN_CPP[align]});`);
      lastAlign = align;
    }
    emitReceiptElement(el, doc, 'd', out);
  }

  out.push('}');
  out.push('');
  out.push(usageComment(name, dataStruct, itemStruct, fields, itemInfo, doc));
  return out.join('\n');
}

export function generateLabel(doc) {
  const name = ident(doc.name, 'MyLabel');
  const dataStruct = `${name}Data`;
  const fields = collectFields(doc);

  const out = header(name, doc, [
    '//',
    `// Label, ${doc.width} x ${doc.height} dots.`,
    '']);

  out.push('using PaperCanvas::Align;');
  out.push('using PaperCanvas::Rect;');
  out.push('using PaperCanvas::VAlign;');
  out.push('');

  for (const el of doc.elements ?? []) {
    if (el.type === 'image' && el.bitmap && el.symbol) {
      out.push(emitBitmap(name, el.name ?? 'image', el.bitmap));
    }
  }

  out.push('/// Everything the layout needs from the caller.');
  out.push(`struct ${dataStruct} {`);
  for (const f of fields) out.push(`  const char* ${f};`);
  out.push('};');
  out.push('');

  out.push(`inline void build${name}(PaperCanvas::Label& lb, const ${dataStruct}& d) {`);
  if (doc.font) out.push(`  lb.setFont(${fontExpr(doc.font)});`);
  if (doc.size && doc.size !== 1) out.push(`  lb.setTextSize(${doc.size}f);`);
  out.push('');

  for (const el of doc.elements ?? []) {
    const rect = `Rect{${el.x}, ${el.y}, ${el.w}, ${el.h}}`;
    switch (el.type) {
      case 'text': {
        const opts = [];
        if (el.font && el.font !== doc.font) opts.push(`.font = ${fontExpr(el.font)}`);
        if (el.size && el.size !== doc.size) opts.push(`.size = ${el.size}f`);
        if (el.align && el.align !== 'left') opts.push(`.align = ${ALIGN_CPP[el.align]}`);
        if (el.valign && el.valign !== 'top') opts.push(`.valign = ${VALIGN_CPP[el.valign]}`);
        if (el.wrap) opts.push('.wrap = true');
        const optStr = opts.length ? `, {${opts.join(', ')}}` : '';
        out.push(`  lb.addText(${rect}, ${textExpr(el, 'd')}${optStr});`);
        break;
      }
      case 'image':
        if (el.symbol) out.push(`  lb.addImage(${rect}, ${el.symbol});`);
        break;
      case 'rect':
        out.push(`  lb.addRect(${rect}, ${el.fill ? 'true' : 'false'}, ${el.thickness ?? 1});`);
        break;
      case 'line':
        out.push(`  lb.addLine(${el.x}, ${el.y}, ${el.x + el.w - 1}, ${el.y}, ${el.thickness ?? 1});`);
        break;
      case 'row': {
        const cells = (el.cells ?? []).map((c) => textExpr(c, 'd'));
        out.push(`  lb.addRow(${rect}, ${cells.join(', ')});`);
        break;
      }
      default:
        break;
    }
  }

  out.push('}');
  out.push('');
  out.push(usageComment(name, dataStruct, null, fields, null, doc));
  return out.join('\n');
}

/** A worked example, so the header is usable without reading the docs. */
function usageComment(name, dataStruct, itemStruct, fields, itemInfo, doc) {
  const isReceipt = doc.kind !== 'label';
  const lines = ['/* Usage:', ''];

  if (itemInfo) {
    const sample = (doc.sampleItems ?? []).slice(0, 2);
    lines.push(`  ${itemStruct} items[] = {`);
    for (const item of (sample.length ? sample : [{}])) {
      const vals = itemInfo.fields.map((f) => cstr(item[f] ?? f));
      lines.push(`      {${vals.join(', ')}},`);
    }
    lines.push('  };');
  }

  const values = fields.map((f) => cstr(doc.sample?.[f] ?? f));
  if (itemInfo) {
    values.push('items', `sizeof(items) / sizeof(items[0])`);
  }
  lines.push(`  ${dataStruct} d = {${values.join(', ')}};`);
  lines.push('');

  if (isReceipt) {
    lines.push(`  PaperCanvas::Receipt r(${doc.width});`);
    lines.push(`  build${name}(r, d);`);
    lines.push('');
    lines.push('  static uint8_t page[PaperCanvas::rowBytes(' + doc.width + ') * 800];');
    lines.push('  if (r.build(page, sizeof(page))) {');
    lines.push('    sendToPrinter(page, r.width(), r.height());   // your driver');
    lines.push('  }');
  } else {
    lines.push(`  PaperCanvas::Label lb(${doc.width}, ${doc.height});`);
    lines.push(`  build${name}(lb, d);`);
    lines.push('');
    lines.push(`  static uint8_t page[PaperCanvas::rowBytes(${doc.width}) * ${doc.height}];`);
    lines.push('  if (lb.build(page, sizeof(page))) {');
    lines.push('    sendToPrinter(page, lb.width(), lb.height());   // your driver');
    lines.push('  }');
  }

  lines.push('*/');
  return lines.join('\n');
}

export const generate = (doc) =>
  doc.kind === 'label' ? generateLabel(doc) : generateReceipt(doc);
