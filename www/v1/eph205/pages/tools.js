import { api } from '../lib/api.js';
import { el, createTable, toast } from '../lib/ui.js';

export function createToolsPage({ getState, refresh }) {
  let active = false;
  let timer = 0;

  const defaultOhms = el('input', { type: 'number', value: '100', style: { width: '110px' } });
  const btnDefault = el('button', { class: 'secondary', text: '应用' });
  const pInput = el('input', { type: 'number', value: '1', min: '1', max: '640', style: { width: '80px' } });
  const nInput = el('input', { type: 'number', value: '2', min: '1', max: '640', style: { width: '80px' } });
  const ohmsInput = el('input', { type: 'number', value: '100', style: { width: '100px' } });
  const overBox = el('input', { type: 'checkbox' });
  const btnSet = el('button', { class: 'secondary', text: '设置点值' });
  const btnClear = el('button', { class: 'secondary', text: '清除覆盖' });
  const simTag = el('span', { class: 'tag muted', text: '未启动' });
  const infoBox = el('div', { class: 'row' });

  const overridesTable = createTable({
    columns: [
      { title: 'P', width: 80, render: (r) => r.p },
      { title: 'N', width: 80, render: (r) => r.n },
      {
        title: '阻值 Ω',
        render: (r) => (r.ohms === null || r.ohms === undefined ? '超量程' : Number(r.ohms).toFixed(3)),
      },
    ],
    pageSize: 50,
    rowKey: (r) => `${r.p}-${r.n}`,
    empty: '没有点值覆盖，全部使用默认阻值',
    height: 180,
  });

  const moduleLog = el('div', { class: 'log-view mono', text: '暂无日志' });
  const serverLog = el('div', { class: 'log-view mono', text: '暂无日志' });

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

  function onState(state) {
    if (!state) return;
    const sim = state.sim || {};
    defaultOhms.value = String(sim.defaultOhms ?? 100);
    simTag.className = `tag ${sim.running ? 'info' : 'muted'}`;
    simTag.textContent = sim.running ? `运行中 ${sim.port || ''}` : '未启动';
    const enabled = !!sim.running;
    btnDefault.disabled = !enabled;
    btnSet.disabled = !enabled;
    btnClear.disabled = !enabled;
    overridesTable.setRows(sim.overrides || []);

    infoBox.replaceChildren(
      info('串口', state.storedPort || '-'),
      info('仿真串口', state.simPort || '-'),
      info('历史记录数', String(state.records ?? 0)),
      info('设备', state.connected ? '已连接' : '未连接'),
    );
  }

  function info(label, value) {
    return el(
      'span',
      { style: { marginRight: '22px' } },
      el('span', { class: 'field', text: `${label}：` }),
      el('b', { text: value }),
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
    active = next;
    if (next) {
      void pollLogs();
      timer = window.setInterval(pollLogs, 2000);
    } else if (timer) {
      window.clearInterval(timer);
      timer = 0;
    }
  }

  const root = el(
    'div',
    {},
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '仿真器', simTag),
      el(
        'div',
        { class: 'row', style: { marginBottom: '8px' } },
        el('label', { class: 'field', text: '默认阻值 Ω' }),
        defaultOhms,
        btnDefault,
      ),
      el(
        'div',
        { class: 'row', style: { marginBottom: '8px' } },
        el('label', { class: 'field', text: 'P' }),
        pInput,
        el('label', { class: 'field', text: 'N' }),
        nInput,
        el('label', { class: 'field', text: '阻值 Ω' }),
        ohmsInput,
        el('label', { class: 'field', text: '超量程' }),
        overBox,
        btnSet,
        btnClear,
      ),
      overridesTable.el,
    ),
    el('div', { class: 'panel' }, el('div', { class: 'panel-title' }, '运行信息'), infoBox),
    el('div', { class: 'panel' }, el('div', { class: 'panel-title' }, '模块日志'), moduleLog),
    el('div', { class: 'panel' }, el('div', { class: 'panel-title' }, '底座日志'), serverLog),
  );

  setActive(false);
  return { el: root, setActive, onState };
}
