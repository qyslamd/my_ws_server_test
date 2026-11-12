import { api } from '../lib/api.js';
import { el, clear, createTable, confirmDialog, fmtTime, openDrawer, toast } from '../lib/ui.js';
import { RESULT_CLASS, RESULT_TEXT, REASON_TEXT, downloadText } from '../lib/model.js';

export function createHistoryPage({ getState, getRole }) {
  let rows = [];
  let total = 0;
  let page = 0;
  const pageSize = 20;
  let selected = [];

  const modelInput = el('input', { type: 'text', style: { width: '140px' } });
  const operatorInput = el('input', { type: 'text', style: { width: '110px' } });
  const statusSelect = el(
    'select',
    { style: { width: '110px' } },
    el('option', { value: '', text: '全部' }),
    el('option', { value: 'completed', text: '已完成' }),
    el('option', { value: 'cancelled', text: '已取消' }),
    el('option', { value: 'aborted', text: '中断' }),
  );
  const btnQuery = el('button', { text: '查询' });
  const btnCompare = el('button', { class: 'secondary', text: '对比', disabled: true });
  const btnExportMany = el('button', { class: 'secondary', text: '批量导出 CSV', disabled: true });
  const selInfo = el('span', { class: 'field', text: '已选 0 条' });
  const totalInfo = el('span', { class: 'field', text: '共 0 条' });

  const isAdmin = () => (getRole ? getRole() : '') === 'admin';

  const columns = [
    { title: 'ID', width: 60, render: (r) => r.id },
    { title: '时间', width: 150, render: (r) => fmtTime(r.createdAt) },
    { title: '型号', width: 150, render: (r) => r.product?.model || '-' },
    { title: '编号', width: 110, render: (r) => r.product?.code || '-' },
    {
      title: '状态',
      width: 90,
      render: (r) => el('span', { class: `tag ${r.status === 'completed' ? 'pass' : 'warn'}`, text: r.status }),
    },
    {
      title: '总数/通过/失败',
      width: 140,
      render: (r) => {
        const s = r.summary || {};
        return `${s.total ?? 0} / ${s.passed ?? 0} / ${s.failed ?? 0}`;
      },
    },
    { title: '通过率', width: 90, render: (r) => `${Number(r.summary?.passRate ?? 0).toFixed(1)}%` },
    { title: '操作员', width: 90, render: (r) => r.operator || '-' },
    { title: '来源', width: 80, render: (r) => (r.deviceSource === 'simulation' ? '仿真' : '真机') },
    {
      title: '操作',
      render: (r) => {
        const btns = [
          el('button', { class: 'link', text: '详情', onclick: () => void openDetail(r.id) }),
          el('button', { class: 'link', text: '导出', onclick: () => void exportOne(r.id) }),
        ];
        if (isAdmin()) {
          btns.push(
            el('button', {
              class: 'link danger',
              text: '删除',
              onclick: async () => {
                if (!(await confirmDialog(`删除记录 #${r.id}？`, { danger: true }))) return;
                try {
                  await api.command('history_delete', { id: r.id });
                  toast(`已删除 #${r.id}`, 'success');
                  void load();
                } catch (err) {
                  toast(err.message, 'error');
                }
              },
            }),
          );
        }
        return el('span', {}, btns);
      },
    },
  ];

  const table = createTable({
    columns,
    pageSize,
    selectable: true,
    rowKey: (r) => r.id,
    empty: '还没有历史记录',
    height: 420,
    onSelectionChange(keys) {
      selected = keys;
      selInfo.textContent = `已选 ${keys.length} 条`;
      btnCompare.disabled = keys.length !== 2;
      btnExportMany.disabled = keys.length === 0;
    },
  });

  table.el.dataset.page = 'history';



  async function load(nextPage = page) {
    if (!api.wsOpen) return;
    page = nextPage;
    try {
      const out = await api.command('history_list', {
        model: modelInput.value,
        operator: operatorInput.value,
        status: statusSelect.value,
        offset: page * pageSize,
        limit: pageSize,
      });
      rows = out.rows || [];
      total = out.total || 0;
      table.setRows(rows);
      totalInfo.textContent = `共 ${total} 条`;
      table.setSelection([]);
      selected = [];
    } catch (err) {
      toast(`查询失败：${err.message}`, 'error');
    }
  }

  btnQuery.addEventListener('click', () => void load(0));

  async function openDetail(id) {
    try {
      const record = await api.command('history_get', { id });
      const s = record.summary || {};
      const body = el(
        'div',
        {},
        el(
          'div',
          { class: 'row', style: { marginBottom: '10px' } },
          el('span', { class: 'tag', text: `总数 ${s.total ?? 0}` }),
          el('span', { class: 'tag info', text: `通过 ${s.passed ?? 0}` }),
          el('span', { class: 'tag fail', text: `失败 ${s.failed ?? 0}` }),
          el('span', { class: 'tag warn', text: `测量失败 ${s.measureFailed ?? 0}` }),
          el('span', { class: 'tag', text: `通过率 ${Number(s.passRate ?? 0).toFixed(1)}%` }),
        ),
        createTable({
          columns: [
            { title: '序号', width: 60, render: (r) => r.seq },
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
          ],
          pageSize: 100,
          rowKey: (r) => `${r.seq}-${r.p}-${r.n}`,
          height: 460,
        }).el,
      );
      const drawer = openDrawer({
        title: `记录 #${record.id} · ${record.product?.model || '未知型号'}`,
        width: 860,
        content: body,
      });
      drawer.body.style.paddingTop = '12px';
    } catch (err) {
      toast(`读取失败：${err.message}`, 'error');
    }
  }

  async function exportOne(id) {
    try {
      const out = await api.command('history_export', { id });
      downloadText(`measurement-${id}.csv`, out.csv);
    } catch (err) {
      toast(`导出失败：${err.message}`, 'error');
    }
  }

  btnExportMany.addEventListener('click', async () => {
    if (selected.length === 0) return;
    try {
      const out = await api.command('history_export_many', { ids: selected.map(Number) });
      downloadText(`measurements-${out.count || selected.length}.csv`, out.csv);
      toast(`已导出 ${out.count || selected.length} 条记录`, 'success');
    } catch (err) {
      toast(`导出失败：${err.message}`, 'error');
    }
  });

  btnCompare.addEventListener('click', async () => {
    if (selected.length !== 2) return;
    try {
      const [x, y] = await Promise.all(selected.map((id) => api.command('history_get', { id })));
      const [base, other] = (x.createdAt ?? 0) <= (y.createdAt ?? 0) ? [x, y] : [y, x];
      showCompare(base, other);
    } catch (err) {
      toast(`读取失败：${err.message}`, 'error');
    }
  });

  function showCompare(base, other) {
    const index = (record) => {
      const map = new Map();
      for (const item of record.items || []) map.set(`${item.p}-${item.n}`, item);
      return map;
    };
    const a = index(base);
    const b = index(other);
    const keys = new Set([...a.keys(), ...b.keys()]);
    const num = (v) => (typeof v === 'number' && Number.isFinite(v) ? v : null);

    const compareRows = [];
    for (const key of keys) {
      const left = a.get(key);
      const right = b.get(key);
      const sample = left || right;
      const baseValue = num(left?.value);
      const otherValue = num(right?.value);
      compareRows.push({
        key,
        p: sample?.p ?? 0,
        n: sample?.n ?? 0,
        baseValue,
        otherValue,
        baseResult: left?.result || '',
        otherResult: right?.result || '',
        delta: baseValue !== null && otherValue !== null ? otherValue - baseValue : null,
        resultChanged: !!left && !!right && left.result !== right.result,
        onlyBase: !!left && !right,
        onlyOther: !left && !!right,
      });
    }
    compareRows.sort((m, n) => m.p - n.p || m.n - n.n);

    let common = 0;
    let onlyBase = 0;
    let onlyOther = 0;
    let changed = 0;
    let maxDelta = 0;
    for (const row of compareRows) {
      if (row.onlyBase) onlyBase += 1;
      else if (row.onlyOther) onlyOther += 1;
      else common += 1;
      if (row.resultChanged) changed += 1;
      if (row.delta !== null) maxDelta = Math.max(maxDelta, Math.abs(row.delta));
    }

    const meta = (record, title, cls) =>
      el(
        'div',
        { style: { flex: '1', minWidth: '260px' } },
        el('span', { class: `tag ${cls}`, text: title }),
        el(
          'div',
          { class: 'mono', style: { lineHeight: '1.8', marginTop: '4px' } },
          `记录 #${record.id} · ${fmtTime(record.createdAt)}`,
          el('br'),
          `型号 ${record.product?.model || '-'} / 编号 ${record.product?.code || '-'}`,
          el('br'),
          `操作员 ${record.operator || '-'} · 来源 ${record.deviceSource === 'simulation' ? '仿真' : '真机'}`,
          el('br'),
          `通过率 ${Number(record.summary?.passRate ?? 0).toFixed(1)}%（${record.summary?.passed ?? 0}/${record.summary?.total ?? 0}）`,
        ),
      );

    const compareTable = createTable({
      columns: [
        { title: 'P', width: 60, render: (r) => r.p },
        { title: 'N', width: 60, render: (r) => r.n },
        { title: '基准值 Ω', width: 100, render: (r) => (r.baseValue === null ? '-' : r.baseValue.toFixed(3)) },
        { title: '对比值 Ω', width: 100, render: (r) => (r.otherValue === null ? '-' : r.otherValue.toFixed(3)) },
        {
          title: '差值 Ω',
          width: 100,
          render: (r) =>
            r.delta === null
              ? '-'
              : el('span', {
                  style: { color: Math.abs(r.delta) > 0.05 ? '#b45309' : '' },
                  text: `${r.delta > 0 ? '+' : ''}${r.delta.toFixed(3)}`,
                }),
        },
        {
          title: '基准判定',
          width: 100,
          render: (r) =>
            r.baseResult
              ? el('span', { class: `tag ${RESULT_CLASS[r.baseResult] || ''}`, text: RESULT_TEXT[r.baseResult] || r.baseResult })
              : '-',
        },
        {
          title: '对比判定',
          width: 100,
          render: (r) =>
            r.otherResult
              ? el('span', { class: `tag ${RESULT_CLASS[r.otherResult] || ''}`, text: RESULT_TEXT[r.otherResult] || r.otherResult })
              : '-',
        },
        {
          title: '变化',
          width: 110,
          render: (r) => {
            if (r.onlyBase) return el('span', { class: 'tag', text: '仅基准有' });
            if (r.onlyOther) return el('span', { class: 'tag info', text: '仅对比有' });
            if (r.resultChanged) return el('span', { class: 'tag fail', text: '判定变化' });
            return el('span', { class: 'field', text: '不变' });
          },
        },
      ],
      pageSize: 100,
      rowKey: (r) => r.key,
      rowClass: (r) => (r.resultChanged ? 'changed' : r.onlyBase || r.onlyOther ? 'partial' : ''),
      height: 520,
    });
    compareTable.setRows(compareRows);

    const body = el(
      'div',
      {},
      el('div', { style: { display: 'flex', gap: '24px', marginBottom: '12px' } }, meta(base, '基准（较早）', ''), meta(other, '对比（较晚）', 'info')),
      el(
        'div',
        { class: 'row', style: { marginBottom: '10px' } },
        el('span', { class: 'tag', text: `共同点 ${common}` }),
        el('span', { class: `tag ${onlyBase ? 'warn' : ''}`, text: `仅基准 ${onlyBase}` }),
        el('span', { class: `tag ${onlyOther ? 'info' : ''}`, text: `仅对比 ${onlyOther}` }),
        el('span', { class: `tag ${changed ? 'fail' : ''}`, text: `判定变化 ${changed}` }),
        el('span', { class: 'tag', text: `最大差值 ${maxDelta.toFixed(3)} Ω` }),
      ),
      changed > 0
        ? el('div', {
            class: 'tag warn',
            style: { display: 'block', marginBottom: '10px', padding: '6px 10px' },
            text: `有 ${changed} 个点的判定和基准不同，已在表格里标红。`,
          })
        : null,
      compareTable.el,
      el('div', {
        class: 'field',
        style: { marginTop: '8px' },
        text: '对齐键是 (P,N)；差值 = 对比值 − 基准值；超过 ±0.05 Ω 的差值标橙。',
      }),
    );

    openDrawer({ title: '测量对比', width: 980, content: body });
  }

  function setActive(active) {
    if (active) void load(0);
  }

  return { el: buildRoot(), setActive, onRoleChange: () => void load(0) };

  function buildRoot() {
    return el(
      'div',
      {},
      el(
        'div',
        { class: 'panel' },
        el('div', { class: 'panel-title' }, '历史记录'),
        el(
          'div',
          { class: 'row', style: { marginBottom: '8px' } },
          el('label', { class: 'field', text: '型号' }),
          modelInput,
          el('label', { class: 'field', text: '操作员' }),
          operatorInput,
          el('label', { class: 'field', text: '状态' }),
          statusSelect,
          btnQuery,
          el('span', { class: 'spacer' }),
          selInfo,
          btnCompare,
          btnExportMany,
          totalInfo,
        ),
        table.el,
      ),
    );
  }
}
