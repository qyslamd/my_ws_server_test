import { api } from './lib/api.js';
import { el, clear, fmtTime, icon, toast } from './lib/ui.js';
import { createLoginPage } from './pages/login.js';
import { createMeasurePage } from './pages/measure.js';
import { createDebugPage } from './pages/debug.js';
import { createHistoryPage } from './pages/history.js';
import { createToolsPage } from './pages/tools.js';
import { createHelpPage } from './pages/help.js';

const PAGES = [
  { key: 'measure', label: '测试', icon: 'activity', title: '测试',
    sub: '连接设备、执行测量、查看结果' },
  { key: 'debug', label: '调试', icon: 'chip', adminOnly: true, title: '调试',
    sub: '单点测量、参数读写、原始命令与帧日志' },
  { key: 'history', label: '历史', icon: 'archive', title: '历史',
    sub: '历次测量记录、逐项明细与对比' },
  { key: 'tools', label: '工具', icon: 'settings', title: '工具',
    sub: '仿真器点值、运行信息与日志' },
  { key: 'help', label: '帮助', icon: 'doc', title: '帮助',
    sub: '使用说明、协议速查与三维连接图' },
];

const root = document.getElementById('root');

const app = {
  moduleState: null,
  user: null,
  role: '',
  wsStatus: 'connecting',
  progress: null,
  pages: new Map(),
  current: 'measure',
  pollTimer: 0,
  clockTimer: 0,
};

// ---------------------------------------------------------------- 主题

function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  try {
    localStorage.setItem('eph205.theme', theme);
  } catch {
    /* 隐私模式下忽略 */
  }
}

function currentTheme() {
  return document.documentElement.dataset.theme === 'dark' ? 'dark' : 'light';
}

applyTheme((() => {
  try {
    return localStorage.getItem('eph205.theme') || 'light';
  } catch {
    return 'light';
  }
})());

// ---------------------------------------------------------------- 事件分发

api.onEvent((name, payload) => {
  if (name === 'eph205.job.progress') {
    app.progress = payload;
    app.pages.get('measure')?.onProgress?.(payload);
  }
  if (name === 'core.state' || name === 'core.module.state') {
    void refreshState();
  }
});

api.onStream((frame, items) => {
  app.pages.get('measure')?.onStream?.(frame, items);
});

api.onStatus((status) => {
  app.wsStatus = status;
  renderTopbar();
  if (status === 'closed') {
    toast('与服务端的连接已断开', 'error');
  }
});

// ---------------------------------------------------------------- 状态

async function refreshState() {
  if (!api.wsOpen) return;
  try {
    const state = await api.getState();
    app.moduleState = state;
    renderTopbar();
    for (const page of app.pages.values()) page.onState?.(state);
  } catch {
    /* 会话断开时忽略，顶栏会提示 */
  }
}

// ---------------------------------------------------------------- 顶栏

const topTitle = el('div', { class: 'page-title' });
const topSub = el('div', { class: 'page-sub' });
const wsChip = el('span', { class: 'chip' });
const deviceChip = el('span', { class: 'chip' });
const simChip = el('span', { class: 'chip' });
const clockEl = el('span', { class: 'clock' });
const themeBtn = el('button', { class: 'icon-btn', title: '切换深浅色' });
const avatar = el('div', { class: 'avatar' });
const logoutBtn = el('button', { class: 'btn text sm', text: '退出' });
const topbar = el('header', { class: 'topbar' });

function renderTopbar() {
  if (!app.user) return;
  const state = app.moduleState;
  const connected = !!state?.connected;
  const simRunning = !!state?.sim?.running;
  const page = PAGES.find((p) => p.key === app.current) || PAGES[0];
  topTitle.textContent = page.title;
  topSub.textContent = page.sub;
  wsChip.className = `chip ${app.wsStatus === 'open' ? 'success' : 'error'}`;
  wsChip.innerHTML = `<span class="dot"></span>${app.wsStatus === 'open' ? '通讯正常' : '已断开'}`;
  deviceChip.className = `chip ${connected ? 'success' : ''}`;
  deviceChip.textContent = connected ? `已连接 ${state.port || ''}` : '未连接';
  simChip.className = `chip ${simRunning ? 'filled' : ''}`;
  simChip.textContent = simRunning ? `仿真 ${state?.sim?.port || ''}` : '仿真未启动';
  avatar.textContent = (app.user || '?').slice(0, 1).toUpperCase();
  avatar.title = `${app.user} · ${app.role === 'admin' ? '管理员' : '操作员'}`;
  themeBtn.innerHTML = icon(currentTheme() === 'dark' ? 'sun' : 'moon', 'sm');
  logoutBtn.className = 'btn text sm';
}

function tickClock() {
  clockEl.textContent = fmtTime(Date.now()).slice(11);
}

themeBtn.addEventListener('click', () => {
  applyTheme(currentTheme() === 'dark' ? 'light' : 'dark');
  renderTopbar();
});

function buildTopbar() {
  clear(topbar);
  topbar.append(
    el('div', {}, topTitle, topSub),
    el('span', { class: 'spacer' }),
    wsChip,
    deviceChip,
    simChip,
    el('span', { style: { width: '8px' } }),
    clockEl,
    themeBtn,
    avatar,
    logoutBtn,
  );
  renderTopbar();
  tickClock();
  window.clearInterval(app.clockTimer);
  app.clockTimer = window.setInterval(tickClock, 1000);
}

// ---------------------------------------------------------------- 页面

const rail = el('nav', { class: 'rail' });
const content = el('main', { class: 'content' });

function visiblePages() {
  return PAGES.filter((p) => !p.adminOnly || app.role === 'admin');
}

function switchPage(key) {
  if (!app.pages.has(key)) key = 'measure';
  const previous = app.current;
  app.current = key;
  for (const [k, page] of app.pages) {
    const active = k === key;
    page.el.classList.toggle('active', active);
    // 页面自己管着轮询/懒加载（历史首次查询、工具页日志轮询），切走要停。
    if (active) page.setActive?.(true);
    else if (k === previous) page.setActive?.(false);
  }
  for (const btn of rail.children) {
    if (btn.dataset) btn.classList.toggle('active', btn.dataset.key === key);
  }
  renderTopbar();
}

function createPages() {
  const ctx = {
    api,
    getState: () => app.moduleState,
    getRole: () => app.role,
    refresh: () => void refreshState(),
  };
  app.pages.set('measure', createMeasurePage(ctx));
  if (app.role === 'admin') app.pages.set('debug', createDebugPage(ctx));
  app.pages.set('history', createHistoryPage(ctx));
  app.pages.set('tools', createToolsPage(ctx));
  app.pages.set('help', createHelpPage(ctx));

  clear(content);
  for (const [key, page] of app.pages) {
    page.el.classList.add('page');
    page.el.classList.toggle('active', key === 'measure');
    content.appendChild(page.el);
  }
}

function buildRail() {
  clear(rail);
  rail.append(
    el('div', { class: 'rail-logo', html: icon('box', 'sm') }),
    ...visiblePages().map((page) => {
      const btn = el('button', { class: 'rail-item', html: `${icon(page.icon)}<span>${page.label}</span>` });
      btn.dataset.key = page.key;
      btn.addEventListener('click', () => switchPage(page.key));
      return btn;
    }),
    el('div', { class: 'rail-spacer' }),
  );
}

// ---------------------------------------------------------------- 登录 / 退出

async function logout() {
  await api.logout();
  teardownShell();
  renderLogin();
}

function teardownShell() {
  window.clearInterval(app.pollTimer);
  window.clearInterval(app.clockTimer);
  app.pollTimer = 0;
  app.clockTimer = 0;
  app.pages.clear();
  app.moduleState = null;
  app.progress = null;
  app.user = null;
  app.role = '';
  clear(root);
}

function renderLogin() {
  const page = createLoginPage({
    api,
    onLogin: (user) => {
      app.user = user.user;
      app.role = user.role;
      renderShell();
      toast(`欢迎，${user.user}`, 'success');
    },
  });
  clear(root);
  root.appendChild(page.el);
  page.focus();
}

function renderShell() {
  clear(root);
  buildTopbar();
  buildRail();
  const shell = el('div', { class: 'app' }, rail, el('div', { class: 'main' }, topbar, content));
  root.appendChild(shell);
  createPages();
  switchPage('measure');
  renderTopbar();

  app.pollTimer = window.setInterval(() => void refreshState(), 2000);
  void api.subscribe(1).catch(() => undefined);
  void refreshState();
}

logoutBtn.addEventListener('click', () => void logout());

// ---------------------------------------------------------------- 启动

async function boot() {
  clear(root);
  root.appendChild(el('div', { class: 'login-page' }, el('div', { class: 'muted' }, '正在连接服务端…')));
  try {
    await api.connect();
    app.wsStatus = 'open';
  } catch {
    app.wsStatus = 'error';
    clear(root);
    root.appendChild(el('div', { class: 'login-page' }, el('div', { class: 'muted' }, '无法连接服务端')));
    return;
  }
  try {
    const me = await api.me();
    if (me.authenticated) {
      app.user = me.user;
      app.role = me.role;
      renderShell();
      return;
    }
  } catch {
    /* 未登录 */
  }
  renderLogin();
}

void boot();
