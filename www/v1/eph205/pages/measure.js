import { api } from '../lib/api.js';
import { el, clear, createTable, confirmDialog, toast } from '../lib/ui.js';
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

  const portSelect = el('select', { style: { width: '110px' } });
  const simPortSelect = el('select', { style: { width: '110px' } });
  const btnConnect = el('button', { text: '连接' });
  const btnDisconnect = el('button', { class: 'secondary', text: '断开' });
  const btnSim = el('button', { class: 'secondary', text: '启动仿真' });
  const simTag = el('span', { class: 'tag muted', text: '仿真未启动' });
  const deviceTag = el('span', { class: 'tag muted', text: '未连接' });

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

  const modelInput = el('input', { type: 'text', style: { width: '150px' } });
  const codeInput = el('input', { type: 'text', style: { width: '150px' } });
  const operatorInput = el('input', { type: 'text', value: 'tester', style: { width: '110px' } });

  // ------------------------------------------------------------ 测试项

  function numberCell(row, field, min, max, width, integer = true) {
    const input = el('input', {
      type: 'number',
      value: row[field] ?? '',
      min: String(min),
      max: String(max),
      style: { width: `${width}px` },
    });
    input.addEventListener('input', () => {
      const value = input.value === '' ? null : Number(input.value);
      row[field] = value;
    });
    return input;
  }

  function selectCell(row, field, options, width) {
    const select = el('select', { style: { width: `${width}px` } });
    for (const opt of options) {
      select.appendChild(el('option', { value: String(opt.value), text: opt.label }));
    }
    select.value = String(row[field]);
    select.addEventListener('change', () => {
      row[field] = Number(select.value);
    });
    return select;
  }

  const itemColumns = [
    {
      title: '序号',
      width: 70,
      render: (row, i) => numberCell(row, 'seq', 1, 999999, 56),
    },
    { title: 'P', width: 70, render: (row) => numberCell(row, 'p', 1, 640, 56) },
    { title: 'N', width: 70, render: (row) => numberCell(row, 'n', 1, 640, 56) },
    {
      title: '线制',
      width: 84,
      render: (row) =>
        selectCell(row, 'wire', [
          { value: 0, label: '二线' },
          { value: 1, label: '四线' },
        ], 70),
    },
    {
      title: '量程',
      width: 84,
      render: (row) =>
        selectCell(row, 'range', [0, 1, 2, 3, 4, 5].map((v) => ({ value: v, label: `R${v}` })), 70),
    },
    {
      title: '下限 Ω',
      width: 100,
      render: (row) => numberCell(row, 'minOhms', 0, 1000000, 82, false),
    },
    {
      title: '上限 Ω',
      width: 100,
      render: (row) => numberCell(row, 'maxOhms', 0, 1000000, 82, false),
    },
    {
      title: '测量',
      width: 60,
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
      title: '',
      width: 60,
      render: (row) => {
        const btn = el('button', { class: 'link danger', text: '删除' });
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
    empty: '还没有测试项：导入 CSV / Excel / JSON，或点“添加一行”',
    height: 200,
  });

  itemsTable.el.dataset.page = 'items';

  const itemCount = el('span', { class: 'tag', text: '0' });
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

  const btnImport = el('button', { class: 'secondary', text: '导入 CSV / Excel / JSON' });
  btnImport.addEventListener('click', () => fileInput.click());

  const btnAdd = el('button', { class: 'secondary', text: '添加一行' });
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

  const btnClear = el('button', { class: 'secondary', text: '清空' });
  btnClear.addEventListener('click', async () => {
    if (items.length === 0) return;
    if (!(await confirmDialog('清空全部测试项？', { danger: true }))) return;
    items.length = 0;
    itemsTable.setRows(items);
    itemCount.textContent = '0';
  });

  const presetWire = el('select', { style: { width: '76px' } }, el('option', { value: '0', text: '二线' }), el('option', { value: '1', text: '四线' }));
  const presetRange = el('select', { style: { width: '76px' } });
  [0, 1, 2, 3, 4, 5].forEach((v) => presetRange.appendChild(el('option', { value: String(v), text: `R${v}` })));
  const btnPreset = el('button', { class: 'secondary', text: '应用到全部' });
  btnPreset.addEventListener('click', () => {
    for (const item of items) {
      item.wire = Number(presetWire.value);
      item.range = Number(presetRange.value);
    }
    itemsTable.setRows(items);
  });

  // ------------------------------------------------------------ 执行

  const btnStart = el('button', { text: '开始测试' });
  const btnStop = el('button', { class: 'danger', text: '停止', disabled: true });
  const btnExport = el('button', { class: 'secondary', text: '导出 CSV', disabled: true });
  const onlyFailBox = el('input', { type: 'checkbox' });
  const jobTag = el('span', { class: 'tag muted', style: { display: 'none' } });

  const progressFill = el('i');
  const stats = {};
  const statRow = el(
    'div',
    { class: 'stat', style: { margin: '8px 0 12px' } },
    statCell('进度'),
    statCell('通过'),
    statCell('失败'),
    statCell('测量失败'),
    statCell('通过率'),
    statCell('记录号'),
  );
  function statCell(label) {
    const value = el('div', { class: 'stat-value', text: '-' });
    stats[label] = value;
    return el('div', {}, value, el('div', { class: 'stat-label', text: label }));
  }

  // 实时读数
  const liveCanvas = el('canvas', { style: { width: '100%', height: '140px', display: 'none' } });
  const liveValue = el('span', { class: 'stat-value', text: '-' });
  const liveTag = el('span', { class: 'tag muted', text: '' });
  const liveCount = el('span', { class: 'hint', text: '已推送 0 条（WS 数据流）' });
  const liveEmpty = el('div', { class: 'empty-hint', text: '开始测试后，这里按服务端推送实时出数' });

  const resultColumns = [
    { title: '序号', dataIndex: 'seq', key: 'seq', width: 60, render: (r) => r.seq },
    { title: 'P', width: 60, render: (r) => r.p },
    { title: 'N', width: 60, render: (r) => r.n },
    { title: '线制', width: 60, render: (r) => (r.wire === 0 ? '二线' : '四线') },
    { title: '量程', width: 60, render: (r) => `R${r.range}` },
    {
      title: '测量值 Ω',
      width: 110,
      render: (r) => (r.value === null || r.value === undefined ? '-' : Number(r.value).toFixed(3)),
    },
    {
      title: '判定',
      width: 90,
      render: (r) => el('span', { class: `tag ${RESULT_CLASS[r.result] || ''}`, text: RESULT_TEXT[r.result] || r.result }),
    },
    { title: '原因', render: (r) => REASON_TEXT[r.reason] || r.reason || '' },
  ];

  const resultsTable = createTable({
    columns: resultColumns,
    pageSize: 50,
    rowKey: (r) => `${r.seq}-${r.p}-${r.n}`,
    empty: '测试完成后在这里显示逐项结果',
    height: 200,
  });

  resultsTable.el.dataset.page = 'results';

  function renderStats() {
    const done = progress?.done ?? 0;
    const total = progress?.total ?? summary?.total ?? items.length;
    const passed = progress?.passed ?? summary?.passed ?? 0;
    const failed = progress?.failed ?? summary?.failed ?? 0;
    const mfail = progress?.measureFailed ?? summary?.measureFailed ?? 0;
    stats['进度'].textContent = `${done}/${total}`;
    stats['通过'].textContent = String(passed);
    stats['失败'].textContent = String(failed);
    stats['测量失败'].textContent = String(mfail);
    stats['通过率'].textContent = `${total > 0 ? ((passed * 100) / total).toFixed(1) : '0.0'}%`;
    stats['记录号'].textContent = recordId ? `#${recordId}` : '-';
    progressFill.style.width = `${total > 0 ? Math.min(100, (done * 100) / total) : 0}%`;
  }

  function renderLive() {
    liveCount.textContent = `已推送 ${live.length} 条（WS 数据流）`;
    const last = live[live.length - 1];
    if (!last) {
      liveValue.textContent = '-';
      liveTag.textContent = '';
      liveCanvas.style.display = 'none';
      liveEmpty.style.display = '';
      return;
    }
    liveEmpty.style.display = 'none';
    liveCanvas.style.display = '';
    liveValue.textContent = Number(last.ohms).toFixed(3) + ' Ω';
    liveTag.className = `tag ${last.result === 'pass' ? 'pass' : 'fail'}`;
    liveTag.textContent = `P${last.p}-N${last.n} ${last.result === 'pass' ? '通过' : '失败'}`;
    drawChart();
  }

  function drawChart() {
    const dpr = window.devicePixelRatio || 1;
    const width = liveCanvas.clientWidth || 600;
    const height = liveCanvas.clientHeight || 140;
    liveCanvas.width = Math.round(width * dpr);
    liveCanvas.height = Math.round(height * dpr);
    const ctx = liveCanvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = '#0b1220';
    ctx.fillRect(0, 0, width, height);

    const values = live.map((p) => Number(p.ohms)).filter((v) => Number.isFinite(v));
    if (values.length < 2) return;
    const min = Math.min(...values);
    const max = Math.max(...values);
    const flat = max - min < 1e-9;
    const span = Math.max(max - min, Math.abs(max) * 0.02, 0.001);
    const padTop = 10;
    const padBottom = 16;
    const usable = height - padTop - padBottom;
    const xOf = (i) => (i / (values.length - 1)) * (width - 8) + 4;
    const yOf = (v) => (flat ? padTop + usable / 2 : padTop + usable - ((v - min) / span) * usable);

    const mean = values.reduce((a, b) => a + b, 0) / values.length;
    ctx.strokeStyle = '#2a3550';
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(0, yOf(mean));
    ctx.lineTo(width, yOf(mean));
    ctx.stroke();
    ctx.setLineDash([]);

    ctx.strokeStyle = '#4c8dff';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    values.forEach((v, i) => (i === 0 ? ctx.moveTo(xOf(i), yOf(v)) : ctx.lineTo(xOf(i), yOf(v))));
    ctx.stroke();

    live.forEach((p, i) => {
      if (p.result === 'pass' || !Number.isFinite(Number(p.ohms))) return;
      ctx.fillStyle = '#ff5c5c';
      ctx.beginPath();
      ctx.arc(xOf(i), yOf(Number(p.ohms)), 3, 0, Math.PI * 2);
      ctx.fill();
    });

    ctx.fillStyle = '#8ea0c0';
    ctx.font = '11px Consolas, monospace';
    if (flat) ctx.fillText(`恒定 ${max.toFixed(3)} Ω`, 6, 12);
    else {
      ctx.fillText(max.toFixed(3), 6, 12);
      ctx.fillText(min.toFixed(3), 6, height - 4);
      ctx.fillText(`均值 ${mean.toFixed(3)} Ω`, width - 110, height - 4);
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
    simTag.className = `tag ${state.sim?.running ? 'info' : 'muted'}`;
    simTag.textContent = state.sim?.running ? `仿真中 ${state.sim.port || ''}` : '仿真未启动';
    deviceTag.className = `tag ${state.connected ? 'pass' : 'muted'}`;
    deviceTag.textContent = state.connected ? `已连接 ${state.port || ''}` : '未连接';
  }

  function onProgress(p) {
    progress = p;
    running = p.state === 'running';
    btnStart.disabled = running;
    btnStop.disabled = !running;
    jobTag.style.display = '';
    jobTag.className = `tag ${p.state === 'completed' ? 'pass' : running ? 'info' : 'warn'}`;
    jobTag.textContent =
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

  const root = el(
    'div',
    {},
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '设备连接'),
      el(
        'div',
        { class: 'row' },
        el('label', { class: 'field', text: '串口' }),
        portSelect,
        btnConnect,
        btnDisconnect,
        el('span', { style: { width: '10px' } }),
        el('label', { class: 'field', text: '仿真串口' }),
        simPortSelect,
        btnSim,
        simTag,
        deviceTag,
      ),
    ),
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '产品与操作员'),
      el(
        'div',
        { class: 'row' },
        el('label', { class: 'field', text: '型号' }),
        modelInput,
        el('label', { class: 'field', text: '编号' }),
        codeInput,
        el('label', { class: 'field', text: '操作员' }),
        operatorInput,
      ),
    ),
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '测试项', itemCount),
      el(
        'div',
        { class: 'row', style: { marginBottom: '8px' } },
        btnImport,
        fileInput,
        btnAdd,
        btnClear,
        el('span', { style: { width: '10px' } }),
        el('label', { class: 'field', text: '统一线制' }),
        presetWire,
        el('label', { class: 'field', text: '量程' }),
        presetRange,
        btnPreset,
      ),
      itemsTable.el,
    ),
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '执行'),
      el(
        'div',
        { class: 'row', style: { marginBottom: '8px' } },
        btnStart,
        btnStop,
        btnExport,
        jobTag,
        el('span', { style: { width: '10px' } }),
        onlyFailBox,
        el('span', { class: 'field', text: '只看失败' }),
      ),
      el('div', { class: 'progress' }, progressFill),
      statRow,
      el('div', { class: 'panel-title', style: { marginTop: '4px' } }, '实时读数'),
      el('div', { class: 'row', style: { marginBottom: '6px' } }, liveValue, liveTag, liveCount),
      liveEmpty,
      liveCanvas,
      el('div', { style: { height: '8px' } }),
      resultsTable.el,
    ),
  );

  renderLive();
  renderStats();

  return { el: root, onState, onProgress, onStream };
}
