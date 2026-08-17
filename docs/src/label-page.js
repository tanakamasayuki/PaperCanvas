// label-page.js — the label editor.
//
// A label is a fixed canvas: nothing grows, nothing pushes anything else out of
// the way, and every element carries its own rectangle. That is a different
// enough model from a receipt that the two get separate pages rather than a
// mode switch (docs/WEB_TOOL.ja.md §1.1).
//
// Rectangles can be dragged and resized on the preview, because typing four
// numbers per element is the part that makes hand-writing a label tedious —
// which is the reason this tool exists at all.

import { renderLabel, preloadFonts } from './render.js';
import { generateLabel } from './codegen.js';
import { pageToCanvas, downloadPng, downloadText, reduceToMono, loadImageFile, MONO_METHODS }
  from './image.js';
import { rowBytes, mmToPx, pxToMm } from './layout.js';
import { el, $, field, input, numberInput, checkbox, select, fontSelect, textSource,
  debounce, makeStore } from './ui.js';

const store = makeStore('papercanvas.label');

const DEFAULT_DOC = {
  kind: 'label',
  name: 'MyLabel',
  width: 400,
  height: 240,
  dpi: 203,
  font: 'lgfxJapanGothic_16',
  size: 1,
  sample: {
    title: '産地直送',
    itemName: 'トマト',
    weight: '1kg',
    price: '580',
    bestBefore: '2026-09-01',
  },
  elements: [
    { type: 'rect', x: 0, y: 0, w: 400, h: 240, fill: false, thickness: 2 },
    { type: 'text', x: 12, y: 12, w: 376, h: 24, bind: 'title', align: 'center' },
    { type: 'row', x: 12, y: 44, w: 376, h: 20,
      cells: [{ bind: 'itemName' }, { bind: 'weight' }, { bind: 'price' }] },
    { type: 'line', x: 12, y: 170, w: 376, h: 1, thickness: 1 },
    { type: 'text', x: 12, y: 182, w: 376, h: 24, bind: 'bestBefore', align: 'left' },
  ],
};

let doc = store.load(structuredClone(DEFAULT_DOC));
let selected = null;
let zoom = 2;

//----------------------------------------------------------------------------

const rerender = debounce(async () => {
  store.save(doc);
  try {
    await preloadFonts(doc);
    const page = renderLabel(doc);
    drawPreview(page);
    $('#meta').innerHTML =
      `<b>${doc.width}</b> &times; <b>${doc.height}</b> px` +
      ` &middot; ${rowBytes(doc.width) * doc.height} bytes` +
      ` &middot; ${pxToMm(doc.width, doc.dpi).toFixed(1)} &times; ` +
      `${pxToMm(doc.height, doc.dpi).toFixed(1)} mm`;
    $('#code').textContent = generateLabel(doc);
  } catch (err) {
    $('#paper').replaceChildren(el('div', { class: 'loading warn', text: String(err) }));
  }
}, 100);

const update = () => { rerender(); buildElementList(); };

//----------------------------------------------------------------------------
// Preview, with draggable rectangles
//----------------------------------------------------------------------------

const HANDLE = 10; // px on screen, so the grab area does not shrink when zoomed out

function drawPreview(page) {
  const canvas = pageToCanvas(page, zoom);
  const ctx = canvas.getContext('2d');

  // Outlines over the rendered page, so what you drag is visibly the same thing
  // that got printed rather than a separate editing view.
  doc.elements.forEach((e, i) => {
    const sel = i === selected;
    ctx.save();
    ctx.strokeStyle = sel ? '#2563eb' : 'rgba(37,99,235,.25)';
    ctx.lineWidth = sel ? 2 : 1;
    ctx.setLineDash(sel ? [] : [3, 3]);
    ctx.strokeRect(e.x * zoom + .5, e.y * zoom + .5, e.w * zoom, e.h * zoom);
    if (sel) {
      ctx.setLineDash([]);
      ctx.fillStyle = '#2563eb';
      ctx.fillRect((e.x + e.w) * zoom - HANDLE, (e.y + e.h) * zoom - HANDLE, HANDLE, HANDLE);
    }
    ctx.restore();
  });

  canvas.style.cursor = 'crosshair';
  canvas.addEventListener('pointerdown', onPointerDown);
  $('#paper').replaceChildren(canvas);
}

function hitTest(px, py) {
  // Last drawn wins, so clicking overlapping elements picks the one on top.
  for (let i = doc.elements.length - 1; i >= 0; i--) {
    const e = doc.elements[i];
    if (px >= e.x && px < e.x + e.w && py >= e.y && py < e.y + e.h) return i;
  }
  return null;
}

function onPointerDown(ev) {
  const canvas = ev.currentTarget;
  const rect = canvas.getBoundingClientRect();
  const px = Math.floor((ev.clientX - rect.left) / zoom);
  const py = Math.floor((ev.clientY - rect.top) / zoom);

  let mode = 'move';
  let index = selected;

  // A grab inside the selected element's handle resizes; anything else selects.
  if (index != null) {
    const e = doc.elements[index];
    const hx = (e.x + e.w) * zoom - HANDLE;
    const hy = (e.y + e.h) * zoom - HANDLE;
    const cx = ev.clientX - rect.left;
    const cy = ev.clientY - rect.top;
    if (cx >= hx && cy >= hy) mode = 'resize';
  }
  if (mode !== 'resize') {
    index = hitTest(px, py);
    selected = index;
    buildElementList();
    if (index == null) { rerender(); return; }
  }

  const e = doc.elements[index];
  const start = { px, py, x: e.x, y: e.y, w: e.w, h: e.h };
  canvas.setPointerCapture(ev.pointerId);

  const onMove = (mv) => {
    const nx = Math.floor((mv.clientX - rect.left) / zoom);
    const ny = Math.floor((mv.clientY - rect.top) / zoom);
    const dx = nx - start.px;
    const dy = ny - start.py;
    if (mode === 'resize') {
      e.w = Math.max(1, start.w + dx);
      e.h = Math.max(1, start.h + dy);
    } else {
      e.x = start.x + dx;
      e.y = start.y + dy;
    }
    rerender();
    buildElementList();
  };
  const onUp = () => {
    canvas.removeEventListener('pointermove', onMove);
    canvas.removeEventListener('pointerup', onUp);
  };
  canvas.addEventListener('pointermove', onMove);
  canvas.addEventListener('pointerup', onUp);
  ev.preventDefault();
}

//----------------------------------------------------------------------------
// Panels
//----------------------------------------------------------------------------

function buildPagePanel() {
  const p = $('#page-panel');
  const mmW = el('span', { class: 'hint' });
  const sync = () => {
    mmW.textContent =
      `${pxToMm(doc.width, doc.dpi).toFixed(1)} × ${pxToMm(doc.height, doc.dpi).toFixed(1)} mm`;
  };
  sync();

  p.replaceChildren(
    el('h2', { text: 'Label' }),
    field('Name', input(doc.name, (v) => { doc.name = v; rerender(); })),
    field('Width (px)', numberInput(doc.width, (v) => { doc.width = v; sync(); update(); },
      { min: 8, max: 2048 })),
    field('Height (px)', numberInput(doc.height, (v) => { doc.height = v; sync(); update(); },
      { min: 8, max: 4096 })),
    field('DPI', numberInput(doc.dpi, (v) => { doc.dpi = v; sync(); rerender(); }, { min: 1 })),
    field('', mmW),
    field('From mm', el('div', { class: 'row' },
      (() => {
        const w = numberInput(Number(pxToMm(doc.width, doc.dpi).toFixed(1)), () => {},
          { step: 0.1, title: 'width mm' });
        const h = numberInput(Number(pxToMm(doc.height, doc.dpi).toFixed(1)), () => {},
          { step: 0.1, title: 'height mm' });
        w.style.width = h.style.width = '68px';
        const go = el('button', {
          text: 'set',
          onclick: () => {
            doc.width = mmToPx(Number(w.value), doc.dpi);
            doc.height = mmToPx(Number(h.value), doc.dpi);
            buildPagePanel(); update();
          },
        });
        return [w, h, go];
      })())),
    field('Font', fontSelect(doc.font, (v) => { doc.font = v; update(); })),
    field('Size', numberInput(doc.size, (v) => { doc.size = v || 1; update(); },
      { step: 0.5, min: 0.5 })),
    el('p', { class: 'hint' },
      'Label stock is sold in millimetres, so converting once here is usually ',
      'right — unlike a receipt, where the printer publishes dots.'),
  );
}

function buildDataPanel() {
  const fields = el('div');
  for (const [k, v] of Object.entries(doc.sample)) {
    fields.append(field(k, input(v, (val) => { doc.sample[k] = val; rerender(); })));
  }
  $('#data-panel').replaceChildren(
    el('h2', { text: 'Sample data' }),
    el('p', { class: 'hint' },
      'Stand-in values for the bound fields. These become the example in the ',
      'generated header, not the printed text.'),
    fields,
  );
}

//----------------------------------------------------------------------------
// Elements
//----------------------------------------------------------------------------

const KINDS = [
  ['text', 'Text'],
  ['row', 'Row'],
  ['image', 'Image'],
  ['rect', 'Rect'],
  ['line', 'Line'],
];

function newElement(kind) {
  const base = { x: 12, y: 12, w: 120, h: 24 };
  switch (kind) {
    case 'text': return { type: 'text', ...base, text: 'Text', align: 'left', valign: 'top' };
    case 'row': return { type: 'row', ...base, w: doc.width - 24,
      cells: [{ text: 'Label' }, { text: 'Value' }] };
    case 'image': return { type: 'image', ...base, w: 80, h: 60, name: 'image', align: 'center' };
    case 'rect': return { type: 'rect', ...base, fill: false, thickness: 1 };
    case 'line': return { type: 'line', ...base, h: 1, thickness: 1 };
    default: return { type: 'rect', ...base };
  }
}

function summary(e) {
  const pos = `${e.x},${e.y} ${e.w}×${e.h}`;
  switch (e.type) {
    case 'text': return `${e.bind ? `{${e.bind}}` : (e.text || '(empty)')} — ${pos}`;
    case 'row': return `${(e.cells ?? []).map((c) => c.bind ? `{${c.bind}}` : c.text).join(' | ')} — ${pos}`;
    case 'image': return `${e.bitmap ? `${e.bitmap.width}x${e.bitmap.height}` : '(no image)'} — ${pos}`;
    default: return pos;
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
      onclick: () => { selected = isSel ? null : i; buildElementList(); rerender(); },
    },
      el('span', { class: 'el-kind', text: e.type }),
      el('span', { class: 'el-summary', text: summary(e) }),
      el('button', { text: '↑', onclick: (ev) => { ev.stopPropagation(); move(i, -1); } }),
      el('button', { text: '↓', onclick: (ev) => { ev.stopPropagation(); move(i, 1); } }),
      el('button', { class: 'danger', text: '×', onclick: (ev) => { ev.stopPropagation(); remove(i); } }),
    ));
    if (isSel) node.append(el('div', { class: 'el-body' }, editor(e)));
    list.append(node);
  });
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

function rectFields(e) {
  return field('Rect', el('div', { class: 'row' },
    ...['x', 'y', 'w', 'h'].map((k) => {
      const n = numberInput(e[k], (v) => { e[k] = v; update(); }, { title: k });
      n.style.width = '62px';
      return n;
    })), true);
}

function editor(e) {
  const box = el('div');
  box.append(rectFields(e));

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
      box.append(field('VAlign', select(['top', 'middle', 'bottom'], e.valign ?? 'top',
        (v) => { e.valign = v; update(); })));
      box.append(field('Wrap', checkbox(e.wrap, (v) => { e.wrap = v; update(); })));
      common();
      box.append(el('p', { class: 'hint' },
        'Wrapping is off by default: a fixed rectangle has nowhere to grow, so ',
        'text that does not fit is cut rather than pushed out of the label.'));
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

    case 'image':
      box.append(imageEditor(e));
      break;

    case 'rect':
      box.append(field('Fill', checkbox(e.fill, (v) => { e.fill = v; update(); })));
      box.append(field('Thickness', numberInput(e.thickness ?? 1,
        (v) => { e.thickness = v; update(); }, { min: 1 })));
      break;

    case 'line':
      box.append(field('Thickness', numberInput(e.thickness ?? 1,
        (v) => { e.thickness = v; e.h = v; update(); }, { min: 1 })));
      box.append(el('p', { class: 'hint' },
        'Horizontal or vertical only. A page is a grid of dots, and an ',
        'unantialiased diagonal does not survive the reduction predictably.'));
      break;

    default:
      break;
  }
  return box;
}

function imageEditor(e) {
  const box = el('div');
  const file = el('input', { type: 'file', accept: 'image/*' });
  file.addEventListener('change', async () => {
    const f = file.files?.[0];
    if (!f) return;
    e.source = await loadImageFile(f);
    e.name = f.name.replace(/\.[^.]+$/, '') || 'image';
    e.srcW = e.source.width;
    e.srcH = e.source.height;
    regen(e);
    update();
  });

  box.append(field('File', file, true));
  if (e.source) {
    box.append(field('Name', input(e.name, (v) => { e.name = v; regen(e); rerender(); })));
    box.append(field('Fit box', el('button', {
      text: 'match rect',
      onclick: () => { regen(e); update(); },
    })));
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
    box.append(el('p', { class: 'hint' },
      'Reduced here and embedded as bytes; the device prints them unchanged. ',
      'Resize the rectangle and press "match rect" to re-reduce at the new size — ',
      'scaling already-dithered pixels would turn the pattern to mush.'));
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
  e.symbol = `${(doc.name || 'MyLabel').replace(/[^A-Za-z0-9_]/g, '_')}_${(e.name || 'image').replace(/[^A-Za-z0-9_]/g, '_')}`;
}

//----------------------------------------------------------------------------

export function start() {
  buildPagePanel();
  buildDataPanel();
  buildElementList();

  $('#add-kind').replaceChildren(...KINDS.map(([v, t]) => el('option', { value: v, text: t })));
  $('#add').addEventListener('click', () => {
    doc.elements.push(newElement($('#add-kind').value));
    selected = doc.elements.length - 1;
    update();
  });

  $('#zoom').addEventListener('change', () => {
    zoom = Number($('#zoom').value);
    rerender();
  });
  $('#png').addEventListener('click', () =>
    downloadPng(renderLabel(doc), `${doc.name || 'label'}.png`, zoom));
  $('#download').addEventListener('click', () =>
    downloadText(generateLabel(doc), `${doc.name || 'MyLabel'}.h`));
  $('#copy').addEventListener('click', async () => {
    await navigator.clipboard.writeText(generateLabel(doc));
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
