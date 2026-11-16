import { api } from '../lib/api.js';
import { el, createTable, iconEl, toast } from '../lib/ui.js';

export function createToolsPage({ getState, refresh }) {
  let timer = 0;

  const field = (label, control, width) =>
    el('div', { class: 'field', style: width ? { width } : null },
      el('label', { text: label }), control);

  const defaultOhms = el('input', { type: 'number', value: '100' });
  const btnDefault = el('button', { class: 'btn outlined', text: '应用' });
  const pInput = el('input', { type: 'number', value: '1', min: '1', max: '640' });
  const nInput = el('input', { type: 'number', value: '2', min: '1', max: '640' });
  const ohmsInput = el('input', { type: 'number', value: '100' });
  const overBox = el('input', { type: 'checkbox' });
  const overField = el(
    'label',
    { class: 'row', style: { gap: '8px', cursor: 'pointer', height: '48px', alignItems: 'center' } },
    overBox,
    el('span', { class: 'small', text: '超量程' }),
  );
  const btnSet = el('button', { class: 'btn tonal', text: '设置点值' });
  const btnClear = el('button', { class: 'btn text', text: '清除覆盖' });
  const simChip = el('span', { class: 'chip' }, '未启动');
  const infoBox = el('div', { class: 'kv' });

  const overridesTable = createTable({
    columns: [
      { title: 'P', width: 72, num: true, render: (r) => r.p },
      { title: 'N', width: 72, num: true, render: (r) => r.n },
      {
        title: '阻值 Ω',
        num: true,
        render: (r) => (r.ohms === null || r.ohms === undefined
          ? el('span', { class: 'chip error', text: '超量程' })
          : Number(r.ohms).toFixed(3)),
      },
    ],
    pageSize: 50,
    rowKey: (r) => `${r.p}-${r.n}`,
    empty: '没有点值覆盖，全部使用默认阻值',
    height: 180,
  });

  const moduleLog = el('div', { class: 'log-view', text: '暂无日志' });
  const serverLog = el('div', { class: 'log-view', text: '暂无日志' });

  overBox.addEventListener('change', () => {
    ohmsInput.disabled = overBox.checked;
  });

  async function run(fn) {
    try {
      await fn();
      refresh();
    } catch (err) {
      toast(err.message, 'error');
    }
  }

  btnDefault.addEventListener('click', () =>
    run(async () => {
      await api.command('sim_config', { defaultOhms: Number(defaultOhms.value) });
      toast('默认阻值已更新', 'success');
    }),
  );

  btnSet.addEventListener('click', () =>
    run(async () => {
      await api.command('sim_set', {
        p: Number(pInput.value),
        n: Number(nInput.value),
        ohms: Number(ohmsInput.value),
        over: overBox.checked,
      });
      toast(`已设置 P${pInput.value}-N${nInput.value}`, 'success');
    }),
  );

  btnClear.addEventListener('click', () =>
    run(async () => {
      await api.command('sim_clear');
      toast('已清除全部点值覆盖', 'success');
    }),
  );

  const kvItem = (label, value) =>
    el('div', { class: 'item' },
      el('span', { class: 'k', text: label }),
      el('span', { class: 'v', text: value }));

  function onState(state) {
    if (!state) return;
    const sim = state.sim || {};
    defaultOhms.value = String(sim.defaultOhms ?? 100);
    simChip.className = `chip ${sim.running ? 'success' : ''}`;
    simChip.innerHTML = sim.running
      ? `<span class="dot"></span>运行中 ${sim.port || ''}`
      : '未启动';
    const enabled = !!sim.running;
    btnDefault.disabled = !enabled;
    btnSet.disabled = !enabled;
    btnClear.disabled = !enabled;
    overridesTable.setRows(sim.overrides || []);

    infoBox.replaceChildren(
      kvItem('串口', state.storedPort || '-'),
      kvItem('仿真串口', state.simPort || '-'),
      kvItem('历史记录数', String(state.records ?? 0)),
      kvItem('设备', state.connected ? '已连接' : '未连接'),
      kvItem('默认阻值', `${sim.defaultOhms ?? 100} Ω`),
      kvItem('点值覆盖', String((sim.overrides || []).length)),
    );
  }

  async function pollLogs() {
    if (!api.wsOpen) return;
    try {
      const out = await api.logs();
      const mod = (out.modules && out.modules.eph205) || [];
      moduleLog.textContent = mod.length ? mod.join('\n') : '暂无日志';
      moduleLog.scrollTop = moduleLog.scrollHeight;
      const srv = out.server || [];
      serverLog.textContent = srv.length ? srv.join('\n') : '暂无日志';
      serverLog.scrollTop = serverLog.scrollHeight;
    } catch {
      /* 忽略 */
    }
  }

  function setActive(next) {
    if (next) {
      void pollLogs();
      window.clearInterval(timer);
      timer = window.setInterval(pollLogs, 2000);
    } else {
      window.clearInterval(timer);
      timer = 0;
    }
  }

  const root = el(
    'div',
    { class: 'stack' },
    el(
      'div',
      { class: 'card' },
      el(
        'div',
        { class: 'card-header' },
        el('div', {},
          el('h3', { class: 'card-title' }, '仿真器'),
          el('p', { class: 'card-subtitle' }, '默认阻值与点值覆盖，仅管理员可改')),
        simChip,
      ),
      el(
        'div',
        { class: 'row', style: { alignItems: 'flex-end', flexWrap: 'wrap' } },
        field('默认阻值 Ω', defaultOhms, '140px'),
        btnDefault,
        el('span', { class: 'divider', style: { width: '1px', height: '40px', margin: '0 8px' } }),
        field('P', pInput, '90px'),
        field('N', nInput, '90px'),
        field('阻值 Ω', ohmsInput, '120px'),
        overField,
        btnSet,
        btnClear,
      ),
      el('div', { class: 'divider' }),
      overridesTable.el,
    ),
    el(
      'div',
      { class: 'card' },
      el(
        'div',
        { class: 'card-header' },
        el('div', {},
          el('h3', { class: 'card-title' }, '运行信息'),
          el('p', { class: 'card-subtitle' }, '当前串口、链路与存储概况')),
      ),
      infoBox,
    ),
    el(
      'div',
      { class: 'grid-1-1' },
      el(
        'div',
        { class: 'card' },
        el('div', { class: 'card-header' },
          el('div', {},
            el('h3', { class: 'card-title' },
              el('span', { class: 'row', style: { gap: '8px' } }, iconEl('chip', 'sm'), '模块日志')),
            el('p', { class: 'card-subtitle' }, '设备层收发与测量事件'))),
        moduleLog,
      ),
      el(
        'div',
        { class: 'card' },
        el('div', { class: 'card-header' },
          el('div', {},
            el('h3', { class: 'card-title' },
              el('span', { class: 'row', style: { gap: '8px' } }, iconEl('activity', 'sm'), '通讯层日志')),
            el('p', { class: 'card-subtitle' }, '会话、订阅与安全停机记录'))),
        serverLog,
      ),
    ),
  );

  setActive(false);
  return { el: root, setActive, onState };
}
