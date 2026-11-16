import { api } from '../lib/api.js';
import { el, clear, createTable, confirmDialog, icon, toast } from '../lib/ui.js';
import {
  RESULT_CLASS,
  RESULT_TEXT,
  REASON_TEXT,
  downloadText,
  itemsToJson,
  makeItem,
  parseFile,
} from '../lib/model.js';

export function createMeasurePage({ getState, getRole, refresh }) {
  const isAdmin = () => (getRole ? getRole() : '') === 'admin';
  const items = [];
  let live = [];
  let results = [];
  let summary = null;
  let recordId = null;
  let jobId = null;
  let running = false;
  let onlyFail = false;
  let portsLoaded = false;
  let progress = null;

  // ------------------------------------------------------------ 设备连接

  const portSelect = el('select', {});
  const simPortSelect = el('select', {});
  const btnConnect = el('button', { class: 'btn filled', html: `${icon('plug', 'sm')}<span>连接</span>` });
  const btnDisconnect = el('button', { class: 'btn outlined', text: '断开' });
  const btnSim = el('button', { class: 'btn tonal', text: '启动仿真' });
  const deviceChip = el('span', { class: 'chip' });
  const simChip = el('span', { class: 'chip' });

  async function loadPorts(state) {
    if (portsLoaded || !api.wsOpen) return;
    portsLoaded = true;
    try {
      const out = await api.command('ports');
      const fill = (select, value) => {
        clear(select);
        for (const port of out.ports || []) {
          select.appendChild(el('option', { value: port, text: port }));
        }
        if (value) select.value = value;
        if (!select.value && select.options.length) select.selectedIndex = 0;
      };
      fill(portSelect, out.storedPort || state?.storedPort);
      fill(simPortSelect, out.simPort || state?.simPort);
    } catch {
      portsLoaded = false;
    }
  }

  async function run(fn, okText) {
    try {
      await fn();
      if (okText) toast(okText, 'success');
      refresh();
    } catch (err) {
      toast(err.message, 'error');
    }
  }

  btnConnect.addEventListener('click', () =>
    run(async () => {
      await api.setParams({ port: portSelect.value, simPort: simPortSelect.value });
      await api.command('connect', { port: portSelect.value });
    }, `已连接 ${portSelect.value}`),
  );

  btnDisconnect.addEventListener('click', () =>
    run(async () => {
      await api.command('disconnect');
    }, '已断开'),
  );

  btnSim.addEventListener('click', () =>
    run(async () => {
      const state = getState();
      if (state?.sim?.running) {
        await api.command('sim_stop');
      } else {
        await api.setParams({ simPort: simPortSelect.value });
        await api.command('sim_start', { port: simPortSelect.value });
      }
    }),
  );

  // ------------------------------------------------------------ 产品信息

  const modelInput = el('input', { type: 'text' });
  const codeInput = el('input', { type: 'text' });
  const operatorInput = el('input', { type: 'text', value: 'tester' });

  // ------------------------------------------------------------ 测试项

  function numberCell(row, field, min, max, integer = true) {
    const input = el('input', { class: 'inline-input', type: 'number', value: row[field] ?? '' });
    input.min = String(min);
    input.max = String(max);
    input.step = integer ? '1' : '0.1';
    input.addEventListener('input', () => {
      row[field] = input.value === '' ? null : Number(input.value);
    });
    return input;
  }

  function selectCell(row, field, options) {
    const select = el('select', { class: 'inline-input' });
    for (const opt of options) select.appendChild(el('option', { value: String(opt.value), text: opt.label }));
    select.value = String(row[field]);
    select.addEventListener('change', () => {
      row[field] = Number(select.value);
    });
    return select;
  }

  const itemColumns = [
    { title: '序号', width: 70, render: (row) => numberCell(row, 'seq', 1, 999999) },
    { title: 'P', width: 70, render: (row) => numberCell(row, 'p', 1, 640) },
    { title: 'N', width: 70, render: (row) => numberCell(row, 'n', 1, 640) },
    {
      title: '线制', width: 90,
      render: (row) => selectCell(row, 'wire', [{ value: 0, label: '二线' }, { value: 1, label: '四线' }]),
    },
    {
      title: '量程', width: 90,
      render: (row) => selectCell(row, 'range', [0, 1, 2, 3, 4, 5].map((v) => ({ value: v, label: `R${v}` }))),
    },
    { title: '下限 Ω', width: 110, num: true, render: (row) => numberCell(row, 'minOhms', 0, 1000000, false) },
    { title: '上限 Ω', width: 110, num: true, render: (row) => numberCell(row, 'maxOhms', 0, 1000000, false) },
    {
      title: '测量', width: 80,
      render: (row) => {
        const box = el('input', { type: 'checkbox' });
        box.checked = row.measure;
        box.addEventListener('change', () => {
          row.measure = box.checked;
        });
        return box;
      },
    },
    {
      title: '', width: 64,
      render: (row) => {
        const btn = el('button', { class: 'icon-btn', title: '删除', html: icon('close', 'sm') });
        btn.addEventListener('click', () => {
          const index = items.indexOf(row);
          if (index >= 0) items.splice(index, 1);
          itemsTable.setRows(items);
          itemCount.textContent = String(items.length);
        });
        return btn;
      },
    },
  ];

  const itemsTable = createTable({
    columns: itemColumns,
    pageSize: 50,
    rowKey: (row) => row.key,
    empty: '还没有测试项：导入 CSV / Excel / JSON，或点「添加一行」',
    // 表头 44 + 行高 61 × 5（行内有 32px 输入框），取整倍数是避免底部露出半行
    height: 349,
  });
  itemsTable.el.dataset.page = 'items';

  const itemCount = el('span', { class: 'chip' }, '0');
  const fileInput = el('input', { type: 'file', accept: '.csv,.json,.txt,.xlsx,.xls', style: { display: 'none' } });

  fileInput.addEventListener('change', async () => {
    const file = fileInput.files && fileInput.files[0];
    if (!file) return;
    try {
      const parsed = await parseFile(file);
      if (parsed.items.length === 0) {
        toast('没有解析到有效测试项（需要 P/N 两列）', 'error');
        return;
      }
      items.length = 0;
      items.push(...parsed.items);
      itemsTable.setRows(items);
      itemCount.textContent = String(items.length);
      if (parsed.product?.model) modelInput.value = parsed.product.model;
      if (parsed.product?.code) codeInput.value = parsed.product.code;
      toast(`导入 ${parsed.items.length} 项${parsed.skipped ? `，跳过 ${parsed.skipped} 行` : ''}`, 'success');
    } catch (err) {
      toast(`导入失败：${err.message}`, 'error');
    } finally {
      fileInput.value = '';
    }
  });

  const btnImport = el('button', { class: 'btn outlined', html: `${icon('download', 'sm')}<span>导入 CSV / Excel / JSON</span>` });
  btnImport.addEventListener('click', () => fileInput.click());

  const btnAdd = el('button', { class: 'btn text', html: `${icon('plus', 'sm')}<span>添加一行</span>` });
  btnAdd.addEventListener('click', () => {
    const last = items[items.length - 1];
    const item = makeItem(last ? last.p + 1 : 1, last ? last.n + 1 : 2);
    item.seq = items.length + 1;
    if (last) {
      item.wire = last.wire;
      item.range = last.range;
    }
    items.push(item);
    itemsTable.setRows(items);
    itemsTable.setSelection([]);
    itemCount.textContent = String(items.length);
  });

  const btnClear = el('button', { class: 'btn text', text: '清空' });
  btnClear.addEventListener('click', async () => {
    if (items.length === 0) return;
    if (!(await confirmDialog('清空全部测试项？', { danger: true }))) return;
    items.length = 0;
    itemsTable.setRows(items);
    itemCount.textContent = '0';
  });

  const presetWire = el('select', {});
  presetWire.append(el('option', { value: '0', text: '二线' }), el('option', { value: '1', text: '四线' }));
  const presetRange = el('select', {});
  [0, 1, 2, 3, 4, 5].forEach((v) => presetRange.appendChild(el('option', { value: String(v), text: `R${v}` })));
  const btnPreset = el('button', { class: 'btn text', text: '应用到全部' });
  btnPreset.addEventListener('click', () => {
    for (const item of items) {
      item.wire = Number(presetWire.value);
      item.range = Number(presetRange.value);
    }
    itemsTable.setRows(items);
  });

  // ------------------------------------------------------------ 执行

  const btnStart = el('button', { class: 'btn filled', html: `${icon('play', 'sm')}<span>开始测试</span>` });
  const btnStop = el('button', { class: 'btn danger', text: '停止', disabled: true });
  const btnExport = el('button', { class: 'btn outlined', html: `${icon('download', 'sm')}<span>导出 CSV</span>`, disabled: true });
  const onlyFailBox = el('input', { type: 'checkbox', 'data-testid': 'only-fail' });
  const jobChip = el('span', { class: 'chip', style: { display: 'none' } });

  const progressBar = el('div', { class: 'bar', style: { width: '0%' } });

  const stats = {};
  // stat-value / stat-label 是自动化脚本（tools/cef_test）的取值钩子，不是样式类，别删
  const kpiGrid = el('div', { class: 'kpi-grid', style: { margin: '12px 0' } });
  for (const label of ['进度', '通过', '失败', '测量失败', '通过率', '记录号']) {
    const value = el('div', { class: 'v stat-value', text: '-' });
    stats[label] = value;
    kpiGrid.appendChild(el('div', { class: 'kpi' }, value, el('div', { class: 'k stat-label', text: label })));
  }

  const liveCanvas = el('canvas', { class: 'canvas-host', style: { display: 'none' } });
  const liveValue = el('span', { class: 'v', style: { fontFamily: 'var(--md-font-mono)' }, text: '-' });
  const liveTag = el('span', { class: 'chip' });
  const liveCount = el('span', { class: 'small muted', text: '已推送 0 条' });
  const liveEmpty = el('div', { class: 'empty-hint', text: '开始测试后，这里按服务端推送实时出数' });

  const resultColumns = [
    { title: '序号', width: 64, num: true, render: (r) => r.seq },
    { title: 'P', width: 64, num: true, render: (r) => r.p },
    { title: 'N', width: 64, num: true, render: (r) => r.n },
    { title: '线制', width: 64, render: (r) => (r.wire === 0 ? '二线' : '四线') },
    { title: '量程', width: 64, render: (r) => `R${r.range}` },
    {
      title: '测量值 Ω', width: 110, num: true,
      render: (r) => (r.value === null || r.value === undefined ? '-' : Number(r.value).toFixed(3)),
    },
    {
      title: '判定', width: 100,
      render: (r) => el('span', { class: `chip ${RESULT_CLASS[r.result] || ''}`, text: RESULT_TEXT[r.result] || r.result }),
    },
    { title: '原因', render: (r) => REASON_TEXT[r.reason] || r.reason || '' },
  ];

  const resultsTable = createTable({
    columns: resultColumns,
    pageSize: 50,
    rowKey: (r) => `${r.seq}-${r.p}-${r.n}`,
    empty: '测试完成后在这里显示逐项结果',
    rowClass: (r) => (r.result === 'fail' ? 'row-error' : ''),
    // 表头 44 + 行高 40 × 6
    height: 284,
  });
  resultsTable.el.dataset.page = 'results';

  function renderStats() {
    const done = progress?.done ?? 0;
    const total = progress?.total ?? summary?.total ?? items.length;
    const passed = progress?.passed ?? summary?.passed ?? 0;
    const failed = progress?.failed ?? summary?.failed ?? 0;
    const mfail = progress?.measureFailed ?? summary?.measureFailed ?? 0;
    const rate = total > 0 ? (passed * 100) / total : 0;
    stats['进度'].textContent = `${done}/${total}`;
    stats['通过'].textContent = String(passed);
    stats['失败'].textContent = String(failed);
    stats['测量失败'].textContent = String(mfail);
    stats['通过率'].textContent = `${rate.toFixed(1)}%`;
    stats['记录号'].textContent = recordId ? `#${recordId}` : '-';
    stats['通过'].classList.toggle('ok', passed > 0);
    stats['失败'].classList.toggle('bad', failed > 0);
    stats['测量失败'].classList.toggle('warn', mfail > 0);
    progressBar.style.width = `${total > 0 ? Math.min(100, (done * 100) / total) : 0}%`;
  }

  function renderLive() {
    liveCount.textContent = `已推送 ${live.length} 条`;
    const last = live[live.length - 1];
    if (!last) {
      liveValue.textContent = '-';
      liveTag.textContent = '';
      liveTag.className = 'chip';
      liveTag.style.display = 'none';
      liveCanvas.style.display = 'none';
      liveEmpty.style.display = '';
      return;
    }
    liveEmpty.style.display = 'none';
    liveCanvas.style.display = '';
    liveTag.style.display = '';
    liveValue.textContent = `${Number(last.ohms).toFixed(3)} Ω`;
    liveTag.className = `chip ${last.result === 'pass' ? 'success' : 'error'}`;
    liveTag.textContent = `P${last.p}-N${last.n} ${last.result === 'pass' ? '合格' : '不合格'}`;
    drawChart();
  }

  function drawChart() {
    const dpr = window.devicePixelRatio || 1;
    const width = liveCanvas.clientWidth || 600;
    const height = liveCanvas.clientHeight || 150;
    liveCanvas.width = Math.round(width * dpr);
    liveCanvas.height = Math.round(height * dpr);
    const ctx = liveCanvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    const styles = getComputedStyle(document.documentElement);
    const surface = styles.getPropertyValue('--md-surface-container-highest').trim() || '#e0e3e9';
    const line = styles.getPropertyValue('--md-primary').trim() || '#1b5e8c';
    const muted = styles.getPropertyValue('--md-on-surface-variant').trim() || '#41474d';
    const bad = styles.getPropertyValue('--md-error').trim() || '#ba1a1a';

    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = surface;
    ctx.fillRect(0, 0, width, height);

    const values = live.map((p) => Number(p.ohms)).filter((v) => Number.isFinite(v));
    if (values.length < 2) return;
    const min = Math.min(...values);
    const max = Math.max(...values);
    const flat = max - min < 1e-9;
    const span = Math.max(max - min, Math.abs(max) * 0.02, 0.001);
    const padTop = 12;
    const padBottom = 20;
    const usable = height - padTop - padBottom;
    const xOf = (i) => (i / (values.length - 1)) * (width - 16) + 8;
    const yOf = (v) => (flat ? padTop + usable / 2 : padTop + usable - ((v - min) / span) * usable);

    ctx.strokeStyle = line;
    ctx.lineWidth = 2;
    ctx.lineCap = 'round';
    ctx.beginPath();
    values.forEach((v, i) => (i === 0 ? ctx.moveTo(xOf(i), yOf(v)) : ctx.lineTo(xOf(i), yOf(v))));
    ctx.stroke();

    live.forEach((p, i) => {
      if (p.result === 'pass' || !Number.isFinite(Number(p.ohms))) return;
      ctx.fillStyle = bad;
      ctx.beginPath();
      ctx.arc(xOf(i), yOf(Number(p.ohms)), 3.5, 0, Math.PI * 2);
      ctx.fill();
    });

    ctx.fillStyle = muted;
    ctx.font = '12px "Cascadia Mono", Consolas, monospace';
    if (flat) {
      ctx.fillText(`恒定 ${max.toFixed(3)} Ω`, 10, 18);
    } else {
      ctx.fillText(max.toFixed(3), 10, 18);
      ctx.fillText(min.toFixed(3), 10, height - 6);
      ctx.fillText(`均值 ${(values.reduce((a, b) => a + b, 0) / values.length).toFixed(3)} Ω`, width - 120, height - 6);
    }
  }

  async function loadRecord(id) {
    try {
      const record = await api.command('history_get', { id });
      results = record.items || [];
      summary = record.summary || null;
      recordId = id;
      if (record.product?.model) modelInput.value = record.product.model;
      if (record.product?.code) codeInput.value = record.product.code;
      applyResultFilter();
      renderStats();
      btnExport.disabled = false;
    } catch (err) {
      toast(`读取结果失败：${err.message}`, 'error');
    }
  }

  function applyResultFilter() {
    resultsTable.setRows(onlyFail ? results.filter((r) => r.result !== 'pass') : results);
  }

  onlyFailBox.addEventListener('change', () => {
    onlyFail = onlyFailBox.checked;
    applyResultFilter();
  });

  btnStart.addEventListener('click', async () => {
    if (items.length === 0) return toast('请先导入或添加测试项', 'warn');
    if (!getState()?.connected) return toast('请先连接设备', 'warn');
    btnStart.disabled = true;
    live = [];
    results = [];
    summary = null;
    recordId = null;
    progress = { done: 0, total: items.length, passed: 0, failed: 0, measureFailed: 0 };
    renderLive();
    applyResultFilter();
    renderStats();
    btnExport.disabled = true;
    try {
      jobId = await api.startJob('measure_run', {
        product: { model: modelInput.value, code: codeInput.value },
        operator: operatorInput.value,
        items: itemsToJson(items),
        deviceSource: getState()?.sim?.running ? 'simulation' : 'serial',
      });
      toast(`测量已开始 ${jobId}`, 'success');
    } catch (err) {
      btnStart.disabled = false;
      toast(`启动失败：${err.message}`, 'error');
    }
  });

  btnStop.addEventListener('click', async () => {
    if (!jobId) return;
    try {
      await api.cancelJob(jobId);
      toast('已请求停止，等待当前帧结束', 'info');
    } catch (err) {
      toast(`停止失败：${err.message}`, 'error');
    }
  });

  btnExport.addEventListener('click', async () => {
    if (!recordId) return;
    try {
      const out = await api.command('history_export', { id: recordId });
      downloadText(`measurement-${recordId}.csv`, out.csv);
    } catch (err) {
      toast(`导出失败：${err.message}`, 'error');
    }
  });

  // ------------------------------------------------------------ 页面对外

  function onState(state) {
    if (!state) return;
    loadPorts(state);
    btnConnect.disabled = state.connected;
    btnDisconnect.disabled = !state.connected;
    portSelect.disabled = state.connected;
    simPortSelect.disabled = !!state.sim?.running;
    btnSim.disabled = !isAdmin();
    btnSim.textContent = state.sim?.running ? '停止仿真' : '启动仿真';
    simChip.className = `chip ${state.sim?.running ? 'filled' : ''}`;
    simChip.textContent = state.sim?.running ? `仿真中 ${state.sim.port || ''}` : '仿真未启动';
    deviceChip.className = `chip ${state.connected ? 'success' : ''}`;
    deviceChip.textContent = state.connected ? `已连接 ${state.port || ''}` : '未连接';
  }

  function onProgress(p) {
    progress = p;
    running = p.state === 'running';
    btnStart.disabled = running;
    btnStop.disabled = !running;
    jobChip.style.display = '';
    jobChip.className = `chip ${p.state === 'completed' ? 'success' : running ? 'filled' : 'warning'}`;
    jobChip.textContent =
      p.state === 'completed' ? '已完成' : running ? '进行中' : p.state === 'cancelled' ? '已取消' : '已中断';
    renderStats();
    if (!running && p.recordId) void loadRecord(p.recordId);
    if (!running) btnStart.disabled = false;
  }

  function onStream(frame, batch) {
    if (frame.streamId !== 1 || !batch.length) return;
    const bytes =
      frame.records.reduce((n, r) => n + r.payload.length, 0) + (frame.data ? frame.data.length : 0);
    api.credit(1, bytes);
    live.push(...batch);
    if (live.length > 400) live = live.slice(-400);
    renderLive();
  }

  const field = (label, control, width) =>
    el('div', { class: 'field', style: width ? { width } : null },
      el('label', { text: label }), control);

  const root = el(
    'div',
    { class: 'stack' },
    el(
      'div',
      { class: 'card' },
      el(
        'div',
        { class: 'card-header' },
        el('div', {}, el('h3', { class: 'card-title' }, '设备连接'),
          el('p', { class: 'card-subtitle' }, '串口 COM7 接客户端，仿真端 COM8 由本机模拟响应')),
        el('div', { class: 'row' }, deviceChip, simChip),
      ),
      el(
        'div',
        { class: 'row', style: { flexWrap: 'wrap' } },
        field('串口', portSelect, '128px'),
        btnConnect,
        btnDisconnect,
        el('span', { style: { width: '12px' } }),
        field('仿真串口', simPortSelect, '128px'),
        btnSim,
      ),
    ),
    el(
      'div',
      { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', {}, el('h3', { class: 'card-title' }, '产品与操作员')),
        el('span', { class: 'card-subtitle', text: '记录会带上这三项，便于事后追溯' })),
      el(
        'div',
        { class: 'row', style: { flexWrap: 'wrap' } },
        field('型号', modelInput, '200px'),
        field('编号', codeInput, '200px'),
        field('操作员', operatorInput, '160px'),
      ),
    ),
    el(
      'div',
      { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', { class: 'row' }, el('h3', { class: 'card-title' }, '测试项'), itemCount),
        el('div', { class: 'small muted', text: '支持 CSV / Excel / JSON，表头见帮助页' })),
      el(
        'div',
        { class: 'row', style: { marginBottom: '12px', flexWrap: 'wrap' } },
        btnImport,
        fileInput,
        btnAdd,
        btnClear,
        el('span', { style: { width: '12px' } }),
        field('统一线制', presetWire, '96px'),
        field('量程', presetRange, '96px'),
        btnPreset,
      ),
      itemsTable.el,
    ),
    el(
      'div',
      { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', { class: 'row' }, el('h3', { class: 'card-title' }, '执行'), jobChip),
        el('div', { class: 'row' },
          btnStart, btnStop, btnExport,
          el('label', { class: 'switch', title: '只看失败' },
            onlyFailBox,
            el('span', { class: 'track' }, el('span', { class: 'thumb' }))),
          el('span', { class: 'small muted', text: '只看失败' }))),
      el('div', { class: 'progress-linear' }, progressBar),
      kpiGrid,
      el('div', { class: 'grid-2' },
        el('div', {},
          el('div', { class: 'row', style: { marginBottom: '8px' } },
            el('span', { class: 'card-title' }, '实时读数'), liveCount),
          el('div', { class: 'row', style: { marginBottom: '8px' } }, liveValue, liveTag),
          liveEmpty,
          liveCanvas),
        el('div', {}, el('div', { class: 'card-title', style: { marginBottom: '8px' } }, '逐项结果'), resultsTable.el)),
    ),
  );

  renderLive();
  renderStats();

  return { el: root, onState, onProgress, onStream };
}
