// 极简 DOM 工具：构造元素、表格、抽屉、对话框、toast。
// 没有框架，全靠直接操作 DOM；组件只在数据变化时重建自己的容器。

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
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(
    d.getMinutes(),
  )}:${p(d.getSeconds())}`;
}

export function toast(message, type = 'info') {
  const host = document.getElementById('toast-host');
  const node = el('div', { class: `toast ${type}`, text: message });
  host.appendChild(node);
  setTimeout(() => {
    node.style.opacity = '0';
    node.style.transition = 'opacity .2s';
    setTimeout(() => node.remove(), 220);
  }, 2600);
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
    headRow.appendChild(el('th', { style: { width: '34px' } }, all));
  }
  for (const col of columns) {
    headRow.appendChild(
      el('th', { style: col.width ? { width: `${col.width}px` } : null }, col.title),
    );
  }

  const tbody = el('tbody');
  const footInfo = el('span');
  const prev = el('button', { class: 'secondary', text: '上一页' });
  const next = el('button', { class: 'secondary', text: '下一页' });
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
  const foot = el(
    'div',
    { class: 'table-foot' },
    footInfo,
    el('span', { class: 'spacer' }),
    prev,
    next,
  );

  const table = el('table', { class: 'grid' }, el('thead', {}, headRow), tbody);
  const root = el(
    'div',
    { class: 'table-wrap' },
    el('div', { class: 'table-scroll', style: { '--table-h': `${height}px` } }, table),
    foot,
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
        el('tr', {}, el('td', { class: 'empty', colspan: String(columns.length + (selectable ? 1 : 0)) }, empty)),
      );
    }
    slice.forEach((row, i) => {
      const key = rowKey(row, page * pageSize + i);
      const tr = el('tr', { class: rowClass ? rowClass(row) : '' });
      if (selected.has(key)) tr.classList.add('selected');
      if (selectable) {
        const box = el('input', { type: 'checkbox' });
        box.checked = selected.has(key);
        box.addEventListener('change', () => {
          if (box.checked) selected.add(key);
          else selected.delete(key);
          tr.classList.toggle('selected', box.checked);
          notify();
        });
        tr.appendChild(el('td', {}, box));
      }
      for (const col of columns) {
        const value = col.render ? col.render(row, page * pageSize + i) : row[col.key];
        const td = el('td');
        append(td, [value === null || value === undefined ? '' : value]);
        tr.appendChild(td);
      }
      tbody.appendChild(tr);
    });

    const total = rows.length;
    const from = total === 0 ? 0 : page * pageSize + 1;
    const to = Math.min(total, (page + 1) * pageSize);
    footInfo.textContent = `第 ${page + 1} / ${pageCount} 页 · ${from}-${to} / 共 ${total} 条`;
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

/** 右侧抽屉。返回 { close, body } */
export function openDrawer({ title, width = 900, content, onClose = null }) {
  const overlay = el('div', { class: 'overlay' });
  const body = el('div', { class: 'drawer-body' }, content);
  const drawer = el(
    'div',
    { class: 'drawer', style: { width: `${width}px` } },
    el(
      'div',
      { class: 'drawer-head' },
      el('span', { text: title }),
      el('span', { class: 'spacer' }),
      el('button', { class: 'close', text: '×', onclick: () => close() }),
    ),
    body,
  );
  function close() {
    overlay.remove();
    drawer.remove();
    if (onClose) onClose();
  }
  overlay.addEventListener('click', close);
  document.body.appendChild(overlay);
  document.body.appendChild(drawer);
  return { close, body };
}

/** 确认框，返回 Promise<boolean> */
export function confirmDialog(message, { okText = '确定', danger = false } = {}) {
  return new Promise((resolve) => {
    const overlay = el('div', { class: 'overlay' });
    const done = (value) => {
      overlay.remove();
      dialog.remove();
      resolve(value);
    };
    const dialog = el(
      'div',
      { class: 'dialog' },
      el('h3', { text: '请确认' }),
      el('div', { text: message }),
      el(
        'div',
        { class: 'row' },
        el('button', { class: 'secondary', text: '取消', onclick: () => done(false) }),
        el('button', {
          class: danger ? 'danger' : '',
          text: okText,
          onclick: () => done(true),
        }),
      ),
    );
    overlay.addEventListener('click', () => done(false));
    document.body.appendChild(overlay);
    document.body.appendChild(dialog);
  });
}
