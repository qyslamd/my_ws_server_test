// DOM 工具与基础组件（表格 / 对话框 / 提示条）。
// 样式全部走 MD3 令牌与 md3.css 的类，不在这里写死颜色。

export function el(tag, attrs, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (value === null || value === undefined || value === false) continue;
    if (key === 'class') node.className = value;
    else if (key === 'text') node.textContent = value;
    else if (key === 'html') node.innerHTML = value;
    else if (key === 'style' && typeof value === 'object') Object.assign(node.style, value);
    else if (key.startsWith('on') && typeof value === 'function') {
      node.addEventListener(key.slice(2).toLowerCase(), value);
    } else if (value === true) node.setAttribute(key, '');
    else node.setAttribute(key, value);
  }
  append(node, children);
  return node;
}

function append(node, children) {
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false) continue;
    node.appendChild(
      typeof child === 'string' || typeof child === 'number'
        ? document.createTextNode(String(child))
        : child,
    );
  }
}

export function clear(node) {
  while (node.firstChild) node.removeChild(node.firstChild);
  return node;
}

export function fmtTime(ms) {
  if (!ms) return '-';
  const d = new Date(ms);
  const p = (v) => String(v).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(
    d.getHours(),
  )}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

/** 内联图标（icons.js 提供 window.icon），返回 markup 字符串。 */
export function icon(name, cls = '') {
  if (typeof window.icon === 'function') return window.icon(name, cls);
  return '';
}

/**
 * 图标节点。
 * icon() 返回的是字符串，当作 el() 的子节点传入会被转义成文本（屏幕上出现
 * "<svg ...>"）。需要图标跟文字并排时用这个函数，它返回真正的 SVG 元素。
 */
export function iconEl(name, cls = '') {
  const tpl = document.createElement('template');
  tpl.innerHTML = icon(name, cls).trim();
  return tpl.content.firstElementChild || document.createTextNode('');
}

// 单个 snckbar 元素复用，避免提示堆叠
let snackbarEl = null;
let snackbarTimer = 0;

export function toast(message, type = 'info') {
  if (!snackbarEl) {
    snackbarEl = el('div', { class: 'snackbar' });
    document.body.appendChild(snackbarEl);
  }
  clear(snackbarEl);
  const tone = type === 'error' ? 'alert' : type === 'success' ? 'check' : 'bell';
  snackbarEl.append(
    el('span', { html: icon(tone, 'sm') }),
    el('span', { text: message }),
  );
  snackbarEl.classList.add('show');
  window.clearTimeout(snackbarTimer);
  snackbarTimer = window.setTimeout(() => snackbarEl.classList.remove('show'), 2800);
}

/** 通用表格：分页 + 可选多选。返回 { el, setRows, getRows, getSelection, setSelection } */
export function createTable({
  columns,
  pageSize = 50,
  selectable = false,
  rowKey = (row, i) => i,
  empty = '暂无数据',
  rowClass = null,
  onSelectionChange = null,
  height = 260,
}) {
  let rows = [];
  let page = 0;
  let selected = new Set();

  const headRow = el('tr');
  if (selectable) {
    const all = el('input', { type: 'checkbox' });
    all.addEventListener('change', () => {
      const keys = pageRows().map((r, i) => rowKey(r, page * pageSize + i));
      if (all.checked) keys.forEach((k) => selected.add(k));
      else keys.forEach((k) => selected.delete(k));
      render();
      notify();
    });
    headRow.appendChild(el('th', { style: { width: '40px' } }, all));
  }
  for (const col of columns) {
    const cls = [col.num ? 'num' : '', col.cls || ''].filter(Boolean).join(' ');
    headRow.appendChild(
      el('th', { class: cls || null, style: col.width ? { width: `${col.width}px` } : null },
        col.title),
    );
  }

  const tbody = el('tbody');
  const pagerInfo = el('span');
  const prev = el('button', { class: 'btn text sm', text: '上一页' });
  const next = el('button', { class: 'btn text sm', text: '下一页' });
  prev.addEventListener('click', () => {
    if (page > 0) {
      page -= 1;
      render();
    }
  });
  next.addEventListener('click', () => {
    if ((page + 1) * pageSize < rows.length) {
      page += 1;
      render();
    }
  });
  const pager = el(
    'div',
    { class: 'pager' },
    pagerInfo,
    el('span', { class: 'spacer' }),
    prev,
    next,
  );

  const table = el('table', { class: 'data' }, el('thead', {}, headRow), tbody);
  const root = el(
    'div',
    {},
    el('div', { class: 'table-wrap', style: { maxHeight: `${height}px` } }, table),
    pager,
  );

  function pageRows() {
    return rows.slice(page * pageSize, page * pageSize + pageSize);
  }

  function notify() {
    if (onSelectionChange) onSelectionChange([...selected]);
  }

  function render() {
    clear(tbody);
    const slice = pageRows();
    const pageCount = Math.max(1, Math.ceil(rows.length / pageSize));
    if (page >= pageCount) page = pageCount - 1;
    if (slice.length === 0) {
      tbody.appendChild(
        el('tr', {}, el('td', {
          class: 'empty-hint',
          colspan: String(columns.length + (selectable ? 1 : 0)),
        }, empty)),
      );
    }
    slice.forEach((row, i) => {
      const key = rowKey(row, page * pageSize + i);
      const tr = el('tr', { class: rowClass ? rowClass(row) : '' });
      if (selected.has(key)) tr.classList.add('row-selected');
      if (selectable) {
        const box = el('input', { type: 'checkbox' });
        box.checked = selected.has(key);
        box.addEventListener('change', () => {
          if (box.checked) selected.add(key);
          else selected.delete(key);
          tr.classList.toggle('row-selected', box.checked);
          notify();
        });
        tr.appendChild(el('td', {}, box));
      }
      for (const col of columns) {
        const value = col.render ? col.render(row, page * pageSize + i) : row[col.key];
        const td = el('td', { class: col.num ? 'num' : null });
        append(td, [value === null || value === undefined ? '' : value]);
        tr.appendChild(td);
      }
      tbody.appendChild(tr);
    });

    const total = rows.length;
    const from = total === 0 ? 0 : page * pageSize + 1;
    const to = Math.min(total, (page + 1) * pageSize);
    pagerInfo.textContent = `第 ${page + 1} / ${pageCount} 页 · ${from}-${to} / 共 ${total} 条`;
    prev.disabled = page === 0;
    next.disabled = (page + 1) * pageSize >= total;
  }

  render();

  return {
    el: root,
    setRows(next) {
      rows = next || [];
      const keys = new Set(rows.map((r, i) => rowKey(r, i)));
      selected = new Set([...selected].filter((k) => keys.has(k)));
      const pageCount = Math.max(1, Math.ceil(rows.length / pageSize));
      if (page >= pageCount) page = pageCount - 1;
      render();
    },
    getRows: () => rows,
    getSelection: () => [...selected],
    setSelection(keys) {
      selected = new Set(keys);
      render();
    },
  };
}

/** 对话框（MD3 dialog + scrim）。返回 { close, body } */
export function openDrawer({ title, width = 900, content, onClose = null }) {
  const scrim = el('div', { class: 'scrim' });
  const body = el('div', { class: 'drawer-body' }, content);
  const dialog = el(
    'div',
    { class: 'dialog', style: { width: `min(${width}px, calc(100vw - 48px))` } },
    el(
      'div',
      { class: 'card-header', style: { marginBottom: '12px' } },
      el('h2', { style: { margin: 0 }, text: title }),
      el('button', {
        class: 'icon-btn close',
        title: '关闭',
        html: icon('close'),
        onclick: () => close(),
      }),
    ),
    body,
  );
  function close() {
    scrim.classList.remove('open');
    window.setTimeout(() => {
      scrim.remove();
      if (onClose) onClose();
    }, 200);
  }
  scrim.addEventListener('click', (e) => {
    if (e.target === scrim) close();
  });
  document.addEventListener('keydown', function onEsc(e) {
    if (e.key === 'Escape') {
      document.removeEventListener('keydown', onEsc);
      close();
    }
  });
  scrim.appendChild(dialog);
  document.body.appendChild(scrim);
  requestAnimationFrame(() => scrim.classList.add('open'));
  return { close, body };
}

/** 确认框，返回 Promise<boolean> */
export function confirmDialog(message, { okText = '确定', danger = false } = {}) {
  return new Promise((resolve) => {
    const scrim = el('div', { class: 'scrim' });
    const finish = (value) => {
      scrim.classList.remove('open');
      window.setTimeout(() => scrim.remove(), 200);
      resolve(value);
    };
    const dialog = el(
      'div',
      { class: 'dialog' },
      el('h2', { text: '请确认' }),
      el('p', { class: 'muted', text: message }),
      el(
        'div',
        { class: 'actions' },
        el('button', { class: 'btn text', text: '取消', onclick: () => finish(false) }),
        el('button', {
          class: `btn ${danger ? 'danger' : 'filled'}`,
          text: okText,
          onclick: () => finish(true),
        }),
      ),
    );
    scrim.addEventListener('click', (e) => {
      if (e.target === scrim) finish(false);
    });
    scrim.appendChild(dialog);
    document.body.appendChild(scrim);
    requestAnimationFrame(() => scrim.classList.add('open'));
  });
}
