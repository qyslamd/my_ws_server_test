import { api } from '../lib/api.js';
import { el, clear, toast } from '../lib/ui.js';

const PARAM_TYPES = [
  { value: 1, label: '1 线制' },
  { value: 2, label: '2 量程' },
  { value: 3, label: '3 平均次数' },
  { value: 4, label: '4 稳定延时' },
  { value: 5, label: '5 超时' },
];

const RAW_COMMANDS = [
  { value: 1, label: '0x01 设备信息' },
  { value: 2, label: '0x02 单点测量' },
  { value: 5, label: '0x05 读参数' },
  { value: 7, label: '0x07 点检测试' },
  { value: 8, label: '0x08 复位' },
  { value: 9, label: '0x09 原始 ADC' },
  { value: 10, label: '0x0A 设置模式' },
  { value: 11, label: '0x0B 读模式' },
];

export function createDebugPage({ getState, refresh }) {
  let active = false;
  let timer = 0;

  const pInput = el('input', { type: 'number', value: '1', min: '1', max: '640', style: { width: '80px' } });
  const nInput = el('input', { type: 'number', value: '2', min: '1', max: '640', style: { width: '80px' } });
  const btnMeasure = el('button', { text: '测量' });
  const btnReadParams = el('button', { class: 'secondary', text: '读取参数' });
  const btnReset = el('button', { class: 'secondary', text: '复位设备' });

  const readingBox = el('div', { class: 'row', style: { marginTop: '8px' } });

  const paramType = el('select', { style: { width: '130px' } });
  PARAM_TYPES.forEach((t) => paramType.appendChild(el('option', { value: String(t.value), text: t.label })));
  const paramValue = el('input', { type: 'number', value: '0', min: '0', max: '255', style: { width: '80px' } });
  const btnSetParam = el('button', { class: 'secondary', text: '下发' });

  const modeSelect = el('select', { style: { width: '84px' } }, el('option', { value: '0', text: '恒流' }), el('option', { value: '1', text: '恒压' }));
  const sourceSelect = el(
    'select',
    { style: { width: '110px' } },
    el('option', { value: '1', text: '分离源' }),
    el('option', { value: '2', text: 'Howland' }),
    el('option', { value: '3', text: 'AD7124' }),
  );
  const btnSetMode = el('button', { class: 'secondary', text: '下发模式' });
  const paramsBox = el('div', { class: 'row', style: { marginTop: '8px' } });

  const rawCmd = el('select', { style: { width: '160px' } });
  RAW_COMMANDS.forEach((c) => rawCmd.appendChild(el('option', { value: String(c.value), text: c.label })));
  const rawData = el('input', { type: 'text', placeholder: 'DATA（十六进制，可空）', style: { width: '220px' } });
  const btnRaw = el('button', { class: 'secondary', text: '发送' });
  const rawOut = el('div', { class: 'mono', style: { marginTop: '8px', lineHeight: '1.8' } });

  const checkP = el('input', { type: 'number', value: '1', min: '1', max: '640', style: { width: '80px' } });
  const checkN = el('input', { type: 'number', value: '2', min: '1', max: '640', style: { width: '80px' } });
  const btnCheck = el('button', { class: 'secondary', text: '点检' });
  const checkOut = el('div', { class: 'mono', style: { marginTop: '8px', lineHeight: '1.8' } });

  const frameLog = el('div', { class: 'log-view mono', text: '连接并执行命令后，这里显示收发帧' });

  function busy(on) {
    for (const b of [btnMeasure, btnReadParams, btnReset, btnSetParam, btnSetMode, btnRaw, btnCheck]) {
      b.disabled = on || !getState()?.connected;
    }
  }

  async function run(fn) {
    busy(true);
    try {
      await fn();
    } catch (err) {
      toast(err.message, 'error');
    } finally {
      busy(false);
      refresh();
    }
  }

  btnMeasure.addEventListener('click', () =>
    run(async () => {
      const out = await api.command('measure', { p: Number(pInput.value), n: Number(nInput.value) });
      clear(readingBox);
      readingBox.append(
        info('阻值', out.ohms === null || out.ohms === undefined ? '-' : `${Number(out.ohms).toFixed(3)} Ω`),
        info('状态', out.statusText),
        info('原始 ADC', String(out.rawAdc)),
        info('差分', `${out.differentialUv} µV`),
      );
    }),
  );

  btnReadParams.addEventListener('click', () =>
    run(async () => {
      const out = await api.command('get_params');
      clear(paramsBox);
      paramsBox.append(
        info('线制', out.wire === 0 ? '二线' : '四线'),
        info('量程', out.range === 255 ? '未设置' : `R${out.range}`),
        info('平均次数', String(out.averageTimes)),
        info('稳定延时', String(out.settleDelay)),
        info('模式', out.mode === 0 ? '恒流' : '恒压'),
        info('源', String(out.source)),
      );
      modeSelect.value = String(out.mode ?? 0);
      sourceSelect.value = String(out.source ?? 2);
    }),
  );

  btnReset.addEventListener('click', () =>
    run(async () => {
      await api.command('reset');
      toast('设备已复位', 'success');
    }),
  );

  btnSetParam.addEventListener('click', () =>
    run(async () => {
      await api.command('set_param', { type: Number(paramType.value), value: Number(paramValue.value) });
      toast('参数已下发', 'success');
    }),
  );

  btnSetMode.addEventListener('click', () =>
    run(async () => {
      await api.command('set_mode', { mode: Number(modeSelect.value), source: Number(sourceSelect.value) });
      toast('模式已下发', 'success');
    }),
  );

  btnRaw.addEventListener('click', () =>
    run(async () => {
      const out = await api.command('raw', { cmd: Number(rawCmd.value), data: rawData.value });
      clear(rawOut);
      rawOut.append(
        el('div', {
          text: `应答 ${out.cmdText}（0x${Number(out.cmd).toString(16).padStart(2, '0').toUpperCase()}）· ${out.len} 字节${
            out.statusText ? ` · 状态 ${out.statusText}` : ''
          }`,
        }),
        el('div', { style: { color: '#344054' }, text: out.dataHex || '（空）' }),
      );
      toast(`已收到 ${out.cmdText}`, 'success');
    }),
  );

  btnCheck.addEventListener('click', () =>
    run(async () => {
      const out = await api.command('check_point', { p: Number(checkP.value), n: Number(checkN.value) });
      clear(checkOut);
      checkOut.append(
        el('div', {
          text: `应答 ${out.cmdText} · ${out.len} 字节${out.statusText ? ` · 状态 ${out.statusText}` : ''}`,
        }),
        el('div', { style: { color: '#344054' }, text: out.dataHex || '（空）' }),
      );
    }),
  );

  function info(label, value) {
    return el(
      'span',
      { style: { marginRight: '18px' } },
      el('span', { class: 'field', text: `${label}：` }),
      el('b', { text: value }),
    );
  }

  async function pollFrames() {
    if (!api.wsOpen || !getState()?.connected) return;
    try {
      const out = await api.command('frame_log');
      const lines = [];
      for (const text of out.client || []) lines.push(`上位机  ${text}`);
      for (const text of out.sim || []) lines.push(`仿真器  ${text}`);
      if (lines.length) {
        const merged = (frameLog.dataset.lines ? JSON.parse(frameLog.dataset.lines) : []).concat(lines).slice(-300);
        frameLog.dataset.lines = JSON.stringify(merged);
        frameLog.textContent = merged.join('\n');
        frameLog.scrollTop = frameLog.scrollHeight;
      }
    } catch {
      /* 测量中会返回忙，忽略 */
    }
  }

  function setActive(next) {
    active = next;
    if (next) {
      busy(false);
      timer = window.setInterval(pollFrames, 1500);
    } else if (timer) {
      window.clearInterval(timer);
      timer = 0;
    }
  }

  function onState(state) {
    const connected = !!state?.connected;
    for (const b of [btnMeasure, btnReadParams, btnReset, btnSetParam, btnSetMode, btnRaw, btnCheck]) {
      b.disabled = !connected;
    }
  }

  const root = el(
    'div',
    {},
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '单点测量'),
      el(
        'div',
        { class: 'row' },
        el('label', { class: 'field', text: 'P' }),
        pInput,
        el('label', { class: 'field', text: 'N' }),
        nInput,
        btnMeasure,
        btnReadParams,
        btnReset,
      ),
      readingBox,
    ),
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '参数与模式'),
      el(
        'div',
        { class: 'row' },
        el('label', { class: 'field', text: 'SET_PARAM' }),
        paramType,
        paramValue,
        btnSetParam,
        el('span', { style: { width: '12px' } }),
        el('label', { class: 'field', text: '模式' }),
        modeSelect,
        sourceSelect,
        btnSetMode,
      ),
      paramsBox,
    ),
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '原始命令', el('span', { class: 'hint', text: '直接下发一帧，DATA 用十六进制，如 01 00 02 00' })),
      el('div', { class: 'row' }, rawCmd, rawData, btnRaw),
      rawOut,
    ),
    el(
      'div',
      { class: 'panel' },
      el('div', { class: 'panel-title' }, '点检测位', el('span', { class: 'hint', text: '0x07，当前固件未实现，用来验证错误路径' })),
      el(
        'div',
        { class: 'row' },
        el('label', { class: 'field', text: 'P' }),
        checkP,
        el('label', { class: 'field', text: 'N' }),
        checkN,
        btnCheck,
      ),
      checkOut,
    ),
    el('div', { class: 'panel' }, el('div', { class: 'panel-title' }, '原始帧'), frameLog),
  );

  setActive(false);
  return { el: root, setActive, onState };
}
