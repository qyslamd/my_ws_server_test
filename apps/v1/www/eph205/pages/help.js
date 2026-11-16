import { el, clear, icon } from '../lib/ui.js';

const PROTOCOL = `AA 55 CMD LEN DATA SUM8

帧总长 = 5 + LEN，SUM8 从 CMD 起逐字节求和取低 8 位。

请求                      应答
0x01 GET_DEVICE_INFO  ->  0x81 14 字节
0x02 MEASURE_SINGLE   ->  0x82 13 字节
0x03 MEASURE_BATCH    ->  0x83 6 + 4N 字节
0x04 SET_PARAM        ->  0x80 1 字节
0x05 GET_PARAM        ->  0x84 7 字节
0x08 RESET_DEVICE     ->  0x80 1 字节
0x0A SET_MODE         ->  0x80 1 字节
0x0B GET_MODE         ->  0x86 2 字节

批量单帧最多 62 组（5 + 4×62 = 253）。`;

const TABS = [
  { key: 'guide', label: '使用说明', icon: 'doc' },
  { key: 'protocol', label: '协议速查', icon: 'chip' },
  { key: 'stack', label: '技术栈', icon: 'box' },
  { key: 'view3d', label: '三维视图', icon: 'eye' },
];

export function createHelpPage() {
  const content = el('div');
  const tabButtons = new Map();
  let viewer = null;
  let current = 'guide';
  let pageActive = false;

  const tabBar = el('div', { class: 'tabs' });
  for (const tab of TABS) {
    const btn = el('button', {
      class: 'tab',
      html: `<span class="row" style="gap:8px">${icon(tab.icon, 'sm')}<span>${tab.label}</span></span>`,
    });
    btn.addEventListener('click', () => show(tab.key));
    tabButtons.set(tab.key, btn);
    tabBar.appendChild(btn);
  }

  const section = (title, ...body) =>
    el(
      'section',
      { style: { marginBottom: '20px' } },
      el('h3', { class: 'card-title', style: { marginBottom: '8px' } }, title),
      ...body,
    );

  const list = (items) =>
    el(
      'div',
      { class: 'list' },
      items.map((text, i) =>
        el(
          'div',
          { class: 'list-item' },
          el('span', { class: 'lead small mono', text: String(i + 1) }),
          el('div', { class: 'body' }, el('p', { class: 'title', text })),
        ),
      ),
    );

  const paragraph = (text) => el('p', { class: 'muted', style: { margin: '0 0 8px' }, text });

  function show(key) {
    current = key;
    for (const [k, btn] of tabButtons) {
      btn.classList.toggle('active', k === key);
    }
    clear(content);
    if (key === 'guide') {
      content.append(
        section(
          '快速开始（无硬件，com0com 仿真）',
          list([
            '确认本机有 com0com 端口对（当前为 COM7 ↔ COM8）。',
            '「测试」页选择仿真串口 COM8，点「启动仿真」。',
            '选择串口 COM7，点「连接」。',
            '导入 CSV / Excel / JSON 测试项，或点「添加一行」手工录入。',
            '点「开始测试」，看进度与逐项结果。',
            '完成后点「导出 CSV」。',
          ]),
        ),
        section(
          '导入格式',
          paragraph('CSV 带表头（推荐）：'),
          el('pre', { class: 'code-block', style: { marginBottom: '12px' },
            text: '序号,P,N,线制,量程,下限,上限,测量' }),
          el('div', { class: 'kv' },
            el('div', { class: 'item' }, el('span', { class: 'k', text: '线制' }),
              el('span', { class: 'v', text: '二线 / 四线，或 0 / 1' })),
            el('div', { class: 'item' }, el('span', { class: 'k', text: '量程' }),
              el('span', { class: 'v', text: '0..5' })),
            el('div', { class: 'item' }, el('span', { class: 'k', text: '上限' }),
              el('span', { class: 'v', text: '留空表示只判下限' })),
            el('div', { class: 'item' }, el('span', { class: 'k', text: '测量' }),
              el('span', { class: 'v', text: '0 / 否 表示跳过' })),
          ),
          paragraph('Excel：读第一个工作表，有名为「测试项」的表则优先；表头与 CSV 相同。'),
          paragraph('JSON：数组，或 { items: [...], product: {...} }。'),
        ),
        section(
          '判定规则',
          list([
            '测量值为 NaN（超量程）→ 测量失败。',
            '整帧失败 → 本帧全部测量失败，原因记为整帧失败。',
            '勾掉测量 → 未测。',
            '其余按上下限判通过/失败，上限为空只判下限。',
          ]),
        ),
      );
    } else if (key === 'protocol') {
      content.append(
        el('pre', { class: 'code-block', text: PROTOCOL }),
      );
    } else if (key === 'stack') {
      content.append(
        section(
          '技术栈',
          list([
            '服务端：C++17，v1 通讯层（Mongoose + nlohmann/json），零 Qt。',
            '设备链路：Win32 串口，com0com 虚拟串口对做仿真。',
            '前端：原生 ES module + CSS，无框架、无构建；three.js 与 SheetJS 本地 vendor。',
            '控制/查询：HTTP REST；进度与事件：WebSocket。',
            '存储：SQLite（data/eph205/measurements.db），旧 JSONL 自动导入。',
            '界面规范：Material Design 3 定制版，浅色/深色跟随顶栏开关。',
          ]),
        ),
      );
    } else if (key === 'view3d') {
      if (!viewer) {
        content.append(el('div', { class: 'empty-hint', text: '正在加载三维资源…' }));
        // 动态 import：不进首屏，打开这一页才下载 three.js
        import('../lib/viewer3d.js')
          .then((mod) => {
            viewer = mod.createViewer3d();
            viewer.setActive(pageActive && current === 'view3d');
            if (current === 'view3d') {
              clear(content);
              content.appendChild(viewer.el);
            }
          })
          .catch((err) => {
            clear(content);
            content.appendChild(
              el('div', { class: 'empty-hint', text: '三维视图加载失败：' + err.message }),
            );
          });
      } else {
        content.appendChild(viewer.el);
      }
    }
    // 只看三维这一页时才渲染，切走标签页就停 rAF
    viewer?.setActive(pageActive && key === 'view3d');
  }

  show('guide');

  const root = el(
    'div',
    { class: 'stack' },
    el(
      'div',
      { class: 'card' },
      tabBar,
      el('div', { style: { paddingTop: '20px' } }, content),
    ),
  );

  return {
    el: root,
    setActive(next) {
      pageActive = !!next;
      viewer?.setActive(pageActive && current === 'view3d');
    },
  };
}
