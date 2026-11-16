import { api } from '../lib/api.js';
import { el, clear, icon, toast } from '../lib/ui.js';

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
  let timer = 0;

  const field = (label, control, width) =>
    el('div', { class: 'field', style: width ? { width } : null }, el('label', { text: label }), control);

  const pInput = el('input', { type: 'number', value: '1', min: '1', max: '640' });
  const nInput = el('input', { type: 'number', value: '2', min: '1', max: '640' });
  const btnMeasure = el('button', { class: 'btn filled', text: '测量' });
  const btnReadParams = el('button', { class: 'btn outlined', text: '读取参数' });
  const btnReset = el('button', { class: 'btn outlined', text: '复位设备' });
  const readingBox = el('div', { class: 'kv', style: { marginTop: '12px' } });

  const paramType = el('select', {});
  PARAM_TYPES.forEach((t) => paramType.appendChild(el('option', { value: String(t.value), text: t.label })));
  const paramValue = el('input', { type: 'number', value: '0', min: '0', max: '255' });
  const btnSetParam = el('button', { class: 'btn tonal', text: '下发' });

  const modeSelect = el('select', {}, el('option', { value: '0', text: '恒流' }), el('option', { value: '1', text: '恒压' }));
  const sourceSelect = el('select', {},
    el('option', { value: '1', text: '分离源' }),
    el('option', { value: '2', text: 'Howland' }),
    el('option', { value: '3', text: 'AD7124' }));
  const btnSetMode = el('button', { class: 'btn tonal', text: '下发模式' });
  const paramsBox = el('div', { class: 'kv', style: { marginTop: '12px' } });

  const rawCmd = el('select', {});
  RAW_COMMANDS.forEach((c) => rawCmd.appendChild(el('option', { value: String(c.value), text: c.label })));
  const rawData = el('input', { type: 'text', placeholder: '如 01 00 02 00' });
  const btnRaw = el('button', { class: 'btn tonal', text: '发送' });
  const rawOut = el('div', {
    class: 'log-view',
    style: { marginTop: '12px', height: '80px' },
    text: '发送后这里显示应答',
  });

  const checkP = el('input', { type: 'number', value: '1', min: '1', max: '640' });
  const checkN = el('input', { type: 'number', value: '2', min: '1', max: '640' });
  const btnCheck = el('button', { class: 'btn outlined', text: '点检' });
  const checkOut = el('div', {
    class: 'log-view',
    style: { marginTop: '12px', height: '72px' },
    text: '点「点检」后这里显示应答',
  });

  const frameLog = el('div', { class: 'log-view', text: '连接并执行命令后，这里显示收发帧' });

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

  function info(label, value) {
    return el('span', { class: 'item' },
      el('span', { class: 'k', text: label }),
      el('span', { class: 'v', text: value }));
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
    }));

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
    }));

  btnReset.addEventListener('click', () =>
    run(async () => {
      await api.command('reset');
      toast('设备已复位', 'success');
    }));

  btnSetParam.addEventListener('click', () =>
    run(async () => {
      await api.command('set_param', { type: Number(paramType.value), value: Number(paramValue.value) });
      toast('参数已下发', 'success');
    }));

  btnSetMode.addEventListener('click', () =>
    run(async () => {
      await api.command('set_mode', { mode: Number(modeSelect.value), source: Number(sourceSelect.value) });
      toast('模式已下发', 'success');
    }));

  btnRaw.addEventListener('click', () =>
    run(async () => {
      const out = await api.command('raw', { cmd: Number(rawCmd.value), data: rawData.value });
      rawOut.textContent =
        `应答 ${out.cmdText}（0x${Number(out.cmd).toString(16).padStart(2, '0').toUpperCase()}）` +
        ` · ${out.len} 字节${out.statusText ? ` · 状态 ${out.statusText}` : ''}\n` +
        (out.dataHex || '（空）');
      toast(`已收到 ${out.cmdText}`, 'success');
    }));

  btnCheck.addEventListener('click', () =>
    run(async () => {
      const out = await api.command('check_point', { p: Number(checkP.value), n: Number(checkN.value) });
      checkOut.textContent =
        `应答 ${out.cmdText} · ${out.len} 字节${out.statusText ? ` · 状态 ${out.statusText}` : ''}\n` +
        (out.dataHex || '（空）');
    }));

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
    if (next) {
      busy(false);
      window.clearInterval(timer);
      timer = window.setInterval(pollFrames, 1500);
    } else {
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
    { class: 'stack' },
    el('div', { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', {}, el('h3', { class: 'card-title' }, '单点测量'),
          el('p', { class: 'card-subtitle' }, '0x02 单点采集，返回阻值、状态与原始 ADC')),
        el('div', { class: 'row' }, btnReadParams, btnReset)),
      el('div', { class: 'row', style: { flexWrap: 'wrap' } },
        field('P', pInput, '96px'), field('N', nInput, '96px'), btnMeasure),
      readingBox),
    el('div', { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', {}, el('h3', { class: 'card-title' }, '参数与模式'),
          el('p', { class: 'card-subtitle' }, '0x04 下发单个参数；0x0A 设置恒流/恒压与激励源'))),
      el('div', { class: 'row', style: { flexWrap: 'wrap' } },
        field('参数', paramType, '140px'), field('值', paramValue, '96px'), btnSetParam,
        el('span', { style: { width: '12px' } }),
        field('模式', modeSelect, '110px'), field('源', sourceSelect, '130px'), btnSetMode),
      paramsBox),
    el('div', { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', {}, el('h3', { class: 'card-title' }, '原始命令'),
          el('p', { class: 'card-subtitle' }, '直接下发一帧，DATA 用十六进制')),
        btnRaw),
      el('div', { class: 'row' }, field('命令', rawCmd, '170px'), field('DATA', rawData, '240px')),
      rawOut),
    el('div', { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', {}, el('h3', { class: 'card-title' }, '点检测位'),
          el('p', { class: 'card-subtitle' }, '0x07，当前固件未实现，用来验证错误路径')),
        btnCheck),
      el('div', { class: 'row' }, field('P', checkP, '96px'), field('N', checkN, '96px')),
      checkOut),
    el('div', { class: 'card' },
      el('div', { class: 'card-header' },
        el('div', {}, el('h3', { class: 'card-title' }, '原始帧'),
          el('p', { class: 'card-subtitle' }, '上位机与仿真器的收发记录，最多 300 行'))),
      frameLog),
  );

  setActive(false);
  return { el: root, setActive, onState };
}
