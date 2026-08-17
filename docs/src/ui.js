// ui.js — the small pieces both editors use.
//
// No framework: the tool is two pages, and a dependency that has to be fetched
// would break the "works offline, nothing to build" property the whole thing is
// arranged around (docs/WEB_TOOL.ja.md §4).

import { fontCatalog } from '../vendor/lgfx-font-tool.js';

export const el = (tag, attrs = {}, ...kids) => {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else if (k.startsWith('on')) node.addEventListener(k.slice(2), v);
    else if (v !== undefined && v !== null && v !== false) node.setAttribute(k, v);
  }
  for (const kid of kids.flat()) {
    if (kid == null) continue;
    node.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  }
  return node;
};

export const $ = (sel, root = document) => root.querySelector(sel);

/** A labelled control row. */
export function field(label, control, wide = false) {
  return el('div', { class: wide ? 'field wide' : 'field' },
    el('label', { text: label }), control);
}

export function input(value, onchange, attrs = {}) {
  const node = el('input', { value: value ?? '', ...attrs });
  node.addEventListener('input', () => onchange(node.value));
  return node;
}

export function numberInput(value, onchange, attrs = {}) {
  const node = el('input', { type: 'number', value: value ?? 0, ...attrs });
  node.addEventListener('input', () => onchange(Number(node.value)));
  return node;
}

export function checkbox(checked, onchange) {
  const node = el('input', { type: 'checkbox' });
  node.checked = !!checked;
  node.addEventListener('change', () => onchange(node.checked));
  return node;
}

export function select(options, value, onchange) {
  const node = el('select');
  for (const opt of options) {
    const [val, label] = Array.isArray(opt) ? opt : [opt, opt];
    node.append(el('option', { value: val, text: label, selected: val === value || undefined }));
  }
  node.value = value;
  node.addEventListener('change', () => onchange(node.value));
  return node;
}

/**
 * A font picker.
 *
 * Grouped by script, with Japanese first: this is a receipt tool and CJK is the
 * main case, so burying those fonts under sixty Latin ones would be the wrong
 * default even though there are fewer of them.
 */
export function fontSelect(value, onchange) {
  const node = el('select');
  const groups = new Map();
  for (const f of fontCatalog) {
    const key = f.script === 'ja' ? 'Japanese'
      : f.script === 'cn' ? 'Chinese'
      : f.script === 'ko' ? 'Korean'
      : f.script === 'tw' ? 'Chinese (traditional)'
      : 'Latin';
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(f);
  }
  const order = ['Japanese', 'Latin', 'Chinese', 'Chinese (traditional)', 'Korean'];
  for (const key of order) {
    const list = groups.get(key);
    if (!list) continue;
    const g = el('optgroup', { label: key });
    for (const f of list) g.append(el('option', { value: f.name, text: f.name }));
    node.append(g);
  }
  node.value = value;
  node.addEventListener('change', () => onchange(node.value));
  return node;
}

/**
 * A text source: either a literal or a bound field.
 *
 * This is the tool's central choice — the data struct is exactly the set of
 * bound fields — so it is one control rather than two scattered ones.
 */
export function textSource(obj, onchange, placeholder = 'text') {
  const wrap = el('div', { class: 'row' });
  const mode = select([['static', 'static'], ['bind', 'field']],
    obj.bind ? 'bind' : 'static', (v) => {
      if (v === 'bind') { obj.bind = obj.bind || 'value'; delete obj.text; }
      else { obj.text = obj.text ?? ''; delete obj.bind; }
      onchange();
    });
  mode.style.flex = '0 0 84px';

  const value = obj.bind
    ? input(obj.bind, (v) => { obj.bind = v; onchange(); },
        { placeholder: 'field name', class: 'bound' })
    : input(obj.text ?? '', (v) => { obj.text = v; onchange(); },
        { placeholder });
  value.style.flex = '1';

  wrap.append(mode, value);
  return wrap;
}

/** Debounce, so typing does not re-render on every keystroke. */
export function debounce(fn, ms = 120) {
  let timer = null;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), ms);
  };
}

/** Persist a document in localStorage so a reload does not lose the work. */
export function makeStore(key) {
  return {
    load(fallback) {
      try {
        const raw = localStorage.getItem(key);
        if (!raw) return fallback;
        const doc = JSON.parse(raw);
        // Bitmaps do not survive JSON; images have to be re-imported.
        for (const e of doc.elements ?? []) if (e.type === 'image') delete e.bitmap;
        return doc;
      } catch {
        return fallback;
      }
    },
    save(doc) {
      try {
        localStorage.setItem(key, JSON.stringify(doc, (k, v) =>
          (k === 'bitmap' || ArrayBuffer.isView(v) ? undefined : v)));
      } catch { /* quota or private mode; losing the autosave is not fatal */ }
    },
  };
}
