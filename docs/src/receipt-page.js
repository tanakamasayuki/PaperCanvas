// receipt-page.js — the receipt editor.
//
// A receipt is a stack that grows with its content, and its centre is a table
// whose rows repeat once per item at run time. So the editor designs *one* row
// against a set of columns, and previews it against sample items — which is the
// same loop the generated C++ runs over the caller's array.

import { renderReceipt, preloadFonts, layoutReceipt } from './render.js';
import { generateReceipt } from './codegen.js';
import { pageToCanvas, downloadPng, downloadText, reduceToMono, loadImageFile, MONO_METHODS }
  from './image.js';
import { rowBytes, pxToMm } from './layout.js';
import { el, $, field, input, numberInput, checkbox, select, fontSelect, textSource,
  debounce, makeStore } from './ui.js';

const store = makeStore('papercanvas.receipt');

const DEFAULT_DOC = {
  kind: 'receipt',
  name: 'MyReceipt',
  width: 384,
  dpi: 203,
  font: 'lgfxJapanGothic_16',
  size: 1,
  wrap: true,
  columnGap: 8,
  margin: { top: 8, bottom: 16, left: 4, right: 4 },
  sample: {
    shopName: 'PaperCanvas Cafe',
    datetime: '2026-08-17 14:32',
    total: '1580',
  },
  sampleItems: [
    { name: 'コーヒー', qty: 'x2', price: '960' },
    { name: 'サンドイッチ', qty: 'x1', price: '620' },
  ],
  elements: [
    { type: 'text', bind: 'shopName', align: 'center' },
    { type: 'text', bind: 'datetime', align: 'center', size: 1 },
    { type: 'space', height: 6 },
    { type: 'rule', char: '-' },
    {
      type: 'table',
      showHeader: false,
      headerRule: true,
      footerRule: true,
      columnGap: 8,
      columns: [
        { unit: 'rest', value: 0, align: 'left', leader: '', field: 'name', header: '品名' },
        { unit: 'auto', value: 0, align: 'center', leader: '', field: 'qty', header: '数量' },
        { unit: 'auto', value: 0, align: 'right', leader: '', field: 'price', header: '金額' },
      ],
    },
    { type: 'row', cells: [{ text: '合計' }, { bind: 'total' }] },
    { type: 'space', height: 8 },
    { type: 'text', text: 'ご来店ありがとうございます', align: 'center' },
  ],
};

let doc = store.load(structuredClone(DEFAULT_DOC));
let selected = null;

//----------------------------------------------------------------------------

const rerender = debounce(async () => {
  store.save(doc);
  try {
    await preloadFonts(doc);
    const page = renderReceipt(doc);
    const { height } = layoutReceipt(doc);
    const holder = $('#paper');
    holder.replaceChildren(pageToCanvas(page, Number($('#zoom').value)));
    $('#meta').innerHTML =
      `<b>${page.width}</b> &times; <b>${page.height}</b> px` +
      ` &middot; ${rowBytes(page.width) * page.height} bytes` +
      ` &middot; ${pxToMm(page.height, doc.dpi).toFixed(1)} mm of paper` +
      ` &middot; ${doc.sampleItems.length} sample items`;
    $('#code').textContent = generateReceipt(doc);
    void height;
  } catch (err) {
    $('#paper').replaceChildren(el('div', { class: 'loading warn', text: String(err) }));
  }
}, 100);

const update = () => { rerender(); buildElementList(); };

//----------------------------------------------------------------------------
// Panels
//----------------------------------------------------------------------------

function buildPagePanel() {
  const p = $('#page-panel');
  p.replaceChildren(
    el('h2', { text: 'Page' }),
    field('Name', input(doc.name, (v) => { doc.name = v; rerender(); },
      { placeholder: 'MyReceipt' })),
    field('Width (dots)', numberInput(doc.width, (v) => { doc.width = v; update(); },
      { min: 8, max: 2048 })),
    field('DPI', numberInput(doc.dpi, (v) => { doc.dpi = v; rerender(); }, { min: 1 })),
    field('Font', fontSelect(doc.font, (v) => { doc.font = v; update(); })),
    field('Size', numberInput(doc.size, (v) => { doc.size = v || 1; update(); },
      { step: 0.5, min: 0.5 })),
    field('Wrap', checkbox(doc.wrap, (v) => { doc.wrap = v; update(); })),
    field('Margins', el('div', { class: 'row' },
      ...['top', 'bottom', 'left', 'right'].map((k) => {
        const n = numberInput(doc.margin[k], (v) => { doc.margin[k] = v; update(); },
          { min: 0, title: k });
        n.style.width = '58px';
        return n;
      }))),
    el('p', { class: 'hint' },
      'Width is the printer\'s printable dots, not the paper width. 58 mm paper is ',
      el('b', { text: '384' }), ' on nearly every printer.'),
  );
}

function buildDataPanel() {
  const p = $('#data-panel');
  const fields = el('div');
  for (const [k, v] of Object.entries(doc.sample)) {
    fields.append(field(k, input(v, (val) => { doc.sample[k] = val; rerender(); })));
  }
  p.replaceChildren(
    el('h2', { text: 'Sample data' }),
    el('p', { class: 'hint' },
      'Stand-in values for the bound fields, so the preview shows something. ',
      'These become the example in the generated header, not the printed text.'),
    fields,
  );
}

function buildItemsPanel() {
  const table = doc.elements.find((e) => e.type === 'table');
  const p = $('#items-panel');
  if (!table) { p.replaceChildren(); return; }

  const rows = el('tbody');
  doc.sampleItems.forEach((item, i) => {
    const tr = el('tr');
    for (const col of table.columns) {
      tr.append(el('td', {}, input(item[col.field] ?? '', (v) => {
        item[col.field] = v; rerender();
      })));
    }
    tr.append(el('td', {}, el('button', {
      class: 'danger', text: '×', title: 'remove',
      onclick: () => { doc.sampleItems.splice(i, 1); update(); },
    })));
    rows.append(tr);
  });

  p.replaceChildren(
    el('h2', { text: 'Sample items' }),
    el('p', { class: 'hint' },
      'The table repeats once per item. At run time the count comes from the ',
      'caller, so add a couple here to see how the columns behave.'),
    el('table', { class: 'cols' },
      el('thead', {}, el('tr', {},
        ...table.columns.map((c) => el('th', { text: c.field })), el('th'))),
      rows),
    el('div', { class: 'row', style: 'margin-top:8px' },
      el('button', {
        text: '+ item',
        onclick: () => {
          const blank = {};
          for (const c of table.columns) blank[c.field] = c.field;
          doc.sampleItems.push(blank);
          update();
        },
      })),
  );
}

//----------------------------------------------------------------------------
// Elements
//----------------------------------------------------------------------------

const KINDS = [
  ['text', 'Text'],
  ['row', 'Row'],
  ['table', 'Table (repeats)'],
  ['image', 'Image'],
  ['rule', 'Rule'],
  ['line', 'Line'],
  ['space', 'Space'],
];

function newElement(kind) {
  switch (kind) {
    case 'text': return { type: 'text', text: 'Text', align: 'left' };
    case 'row': return { type: 'row', cells: [{ text: 'Label' }, { text: 'Value' }] };
    case 'table': return {
      type: 'table', showHeader: false, headerRule: true, footerRule: true, columnGap: 8,
      columns: [
        { unit: 'rest', value: 0, align: 'left', leader: '', field: 'name', header: 'Name' },
        { unit: 'auto', value: 0, align: 'right', leader: '', field: 'price', header: 'Price' },
      ],
    };
    case 'image': return { type: 'image', name: 'image', align: 'center' };
    case 'rule': return { type: 'rule', char: '-' };
    case 'line': return { type: 'line', thickness: 1 };
    case 'space': return { type: 'space', height: 8 };
    default: return { type: 'space', height: 8 };
  }
}

function summary(e) {
  switch (e.type) {
    case 'text': return e.bind ? `{${e.bind}}` : (e.text || '(empty)');
    case 'row': return (e.cells ?? []).map((c) => c.bind ? `{${c.bind}}` : c.text).join(' | ');
    case 'table': return `${e.columns.length} columns${e.showHeader ? ', header' : ''}`;
    case 'image': return e.bitmap ? `${e.bitmap.width}x${e.bitmap.height}` : '(no image)';
    case 'rule': return `"${e.char}"`;
    case 'line': return `${e.thickness}px`;
    case 'space': return `${e.height}px`;
    default: return '';
  }
}

function buildElementList() {
  const list = $('#elements');
  list.replaceChildren();
  doc.elements.forEach((e, i) => {
    const isSel = selected === i;
    const node = el('div', { class: isSel ? 'el selected' : 'el' });
    node.append(el('div', {
      class: 'el-head',
      onclick: () => { selected = isSel ? null : i; buildElementList(); },
    },
      el('span', { class: 'el-kind', text: e.type }),
      el('span', { class: 'el-summary', text: summary(e) }),
      el('button', { text: '↑', title: 'move up', onclick: (ev) => { ev.stopPropagation(); move(i, -1); } }),
      el('button', { text: '↓', title: 'move down', onclick: (ev) => { ev.stopPropagation(); move(i, 1); } }),
      el('button', { class: 'danger', text: '×', onclick: (ev) => { ev.stopPropagation(); remove(i); } }),
    ));
    if (isSel) node.append(el('div', { class: 'el-body' }, editor(e)));
    list.append(node);
  });
  buildItemsPanel();
}

function move(i, delta) {
  const j = i + delta;
  if (j < 0 || j >= doc.elements.length) return;
  [doc.elements[i], doc.elements[j]] = [doc.elements[j], doc.elements[i]];
  if (selected === i) selected = j;
  update();
}

function remove(i) {
  doc.elements.splice(i, 1);
  selected = null;
  update();
}

function editor(e) {
  const box = el('div');
  const common = () => {
    box.append(field('Font', fontSelect(e.font ?? doc.font, (v) => {
      e.font = v === doc.font ? undefined : v; update();
    })));
    box.append(field('Size', numberInput(e.size ?? doc.size, (v) => {
      e.size = v === doc.size ? undefined : v; update();
    }, { step: 0.5, min: 0.5 })));
  };

  switch (e.type) {
    case 'text':
      box.append(field('Text', textSource(e, update), true));
      box.append(field('Align', select(['left', 'center', 'right'], e.align ?? 'left',
        (v) => { e.align = v; update(); })));
      box.append(field('Wrap', checkbox(e.wrap ?? doc.wrap,
        (v) => { e.wrap = v; update(); })));
      common();
      break;

    case 'row':
      (e.cells ?? []).forEach((c, i) => {
        box.append(field(`Cell ${i + 1}`, el('div', { class: 'row' },
          textSource(c, update),
          el('button', { class: 'danger', text: '×',
            onclick: () => { e.cells.splice(i, 1); update(); } })), true));
      });
      box.append(el('div', { class: 'row' },
        el('button', { text: '+ cell', onclick: () => { e.cells.push({ text: '' }); update(); } })));
      common();
      break;

    case 'table':
      box.append(columnEditor(e));
      box.append(field('Header row', checkbox(e.showHeader, (v) => { e.showHeader = v; update(); })));
      box.append(field('Rule after header', checkbox(e.headerRule, (v) => { e.headerRule = v; update(); })));
      box.append(field('Rule after items', checkbox(e.footerRule, (v) => { e.footerRule = v; update(); })));
      box.append(field('Column gap', numberInput(e.columnGap ?? 8,
        (v) => { e.columnGap = v; update(); }, { min: 0 })));
      common();
      break;

    case 'image':
      box.append(imageEditor(e));
      break;

    case 'rule':
      box.append(field('Character', input(e.char, (v) => { e.char = v.slice(0, 1) || '-'; update(); },
        { maxlength: 1, style: 'width:48px' })));
      common();
      break;

    case 'line':
      box.append(field('Thickness', numberInput(e.thickness, (v) => { e.thickness = v; update(); },
        { min: 1 })));
      break;

    case 'space':
      box.append(field('Height', numberInput(e.height, (v) => { e.height = v; update(); },
        { min: 0 })));
      break;

    default:
      break;
  }
  return box;
}

/** The column definition, which is also the shape of the Item struct. */
function columnEditor(table) {
  const body = el('tbody');
  table.columns.forEach((col, i) => {
    body.append(el('tr', {},
      el('td', {}, input(col.field, (v) => { col.field = v; update(); },
        { placeholder: 'field', class: 'bound' })),
      el('td', {}, input(col.header ?? '', (v) => { col.header = v; update(); },
        { placeholder: 'heading' })),
      el('td', {}, select(['rest', 'auto', 'px', 'percent'], col.unit,
        (v) => { col.unit = v; update(); })),
      el('td', {}, numberInput(col.value ?? 0, (v) => { col.value = v; update(); },
        { min: 0, disabled: col.unit === 'rest' || col.unit === 'auto' ? '' : undefined })),
      el('td', {}, select(['left', 'center', 'right'], col.align,
        (v) => { col.align = v; update(); })),
      el('td', {}, input(col.leader ?? '', (v) => { col.leader = v.slice(0, 1); update(); },
        { maxlength: 1, placeholder: '.' })),
      el('td', {}, el('button', { class: 'danger', text: '×',
        onclick: () => { table.columns.splice(i, 1); update(); } })),
    ));
  });

  return el('div', {},
    el('table', { class: 'cols' },
      el('thead', {}, el('tr', {},
        el('th', { text: 'field' }), el('th', { text: 'heading' }),
        el('th', { text: 'width' }), el('th', { text: 'value' }),
        el('th', { text: 'align' }), el('th', { text: 'leader' }), el('th'))),
      body),
    el('div', { class: 'row', style: 'margin-top:6px' },
      el('button', {
        text: '+ column',
        onclick: () => {
          table.columns.push({ unit: 'auto', value: 0, align: 'right', leader: '',
            field: `col${table.columns.length + 1}`, header: '' });
          for (const item of doc.sampleItems) {
            const f = table.columns.at(-1).field;
            item[f] = item[f] ?? f;
          }
          update();
        },
      })),
    el('p', { class: 'hint' },
      'The ', el('b', { text: 'field' }), ' names become the Item struct. Column widths ',
      'resolve per row from this layout, never from the text length — which is ',
      'what keeps the columns lined up down the page.'),
  );
}

function imageEditor(e) {
  const box = el('div');
  const file = el('input', { type: 'file', accept: 'image/*' });
  file.addEventListener('change', async () => {
    const f = file.files?.[0];
    if (!f) return;
    e.source = await loadImageFile(f);
    e.name = (f.name.replace(/\.[^.]+$/, '') || 'image');
    e.symbol = null;
    e.srcW = e.source.width;
    e.srcH = e.source.height;
    e.w = e.w ?? Math.min(doc.width, e.source.width);
    e.h = e.h ?? Math.round(e.source.height * (e.w / e.source.width));
    regen(e);
    update();
  });

  box.append(field('File', file, true));
  if (e.source) {
    box.append(field('Name', input(e.name, (v) => { e.name = v; rerender(); })));
    box.append(field('Width', numberInput(e.w, (v) => {
      e.w = Math.max(1, v);
      if (e.lockAspect !== false) e.h = Math.round(e.srcH * (e.w / e.srcW));
      regen(e); update();
    }, { min: 1, max: 2048 })));
    box.append(field('Height', numberInput(e.h, (v) => {
      e.h = Math.max(1, v); regen(e); update();
    }, { min: 1 })));
    box.append(field('Dither', select(MONO_METHODS, e.mono ?? 'threshold',
      (v) => { e.mono = v; regen(e); update(); })));
    if ((e.mono ?? 'threshold') === 'threshold') {
      box.append(field('Threshold', numberInput(e.threshold ?? 128,
        (v) => { e.threshold = v; regen(e); update(); }, { min: 0, max: 255 })));
    }
    box.append(field('Brightness', numberInput(e.brightness ?? 0,
      (v) => { e.brightness = v; regen(e); update(); }, { min: -128, max: 128 })));
    box.append(field('Contrast', numberInput(e.contrast ?? 1,
      (v) => { e.contrast = v; regen(e); update(); }, { step: 0.1, min: 0.1, max: 4 })));
    box.append(field('Invert', checkbox(e.invert, (v) => { e.invert = v; regen(e); update(); })));
    box.append(field('Align', select(['left', 'center', 'right'], e.align ?? 'center',
      (v) => { e.align = v; update(); })));
    box.append(el('p', { class: 'hint' },
      'Reduced here and embedded as bytes. The device prints them unchanged, so ',
      'the preview is exact.'));
  } else {
    box.append(el('p', { class: 'hint', text: 'Pick a file to embed it as 1bpp.' }));
  }
  return box;
}

function regen(e) {
  if (!e.source) return;
  e.bitmap = reduceToMono(e.source, e.w, e.h, {
    method: e.mono ?? 'threshold',
    threshold: e.threshold ?? 128,
    invert: e.invert,
    brightness: e.brightness ?? 0,
    contrast: e.contrast ?? 1,
  });
  e.symbol = `${(doc.name || 'MyReceipt').replace(/[^A-Za-z0-9_]/g, '_')}_${(e.name || 'image').replace(/[^A-Za-z0-9_]/g, '_')}`;
}

//----------------------------------------------------------------------------

export function start() {
  buildPagePanel();
  buildDataPanel();
  buildElementList();

  $('#add-kind').replaceChildren(...KINDS.map(([v, t]) =>
    el('option', { value: v, text: t })));
  $('#add').addEventListener('click', () => {
    doc.elements.push(newElement($('#add-kind').value));
    selected = doc.elements.length - 1;
    update();
  });

  $('#zoom').addEventListener('change', rerender);
  $('#png').addEventListener('click', () =>
    downloadPng(renderReceipt(doc), `${doc.name || 'receipt'}.png`,
      Number($('#zoom').value)));
  $('#download').addEventListener('click', () =>
    downloadText(generateReceipt(doc), `${doc.name || 'MyReceipt'}.h`));
  $('#copy').addEventListener('click', async () => {
    await navigator.clipboard.writeText(generateReceipt(doc));
    $('#copy').textContent = 'copied';
    setTimeout(() => { $('#copy').textContent = 'copy'; }, 1200);
  });
  $('#reset').addEventListener('click', () => {
    if (!confirm('Discard this layout and start again?')) return;
    doc = structuredClone(DEFAULT_DOC);
    selected = null;
    buildPagePanel(); buildDataPanel(); update();
  });

  rerender();
}
