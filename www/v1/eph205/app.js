import { api } from './lib/api.js';
import { el, clear, toast } from './lib/ui.js';
import { createLoginPage } from './pages/login.js';
import { createMeasurePage } from './pages/measure.js';
import { createDebugPage } from './pages/debug.js';
import { createHistoryPage } from './pages/history.js';
import { createToolsPage } from './pages/tools.js';
import { createHelpPage } from './pages/help.js';

const PAGES = [
  { key: 'measure', label: '测试' },
  { key: 'debug', label: '调试', adminOnly: true },
  { key: 'history', label: '历史' },
  { key: 'tools', label: '工具' },
  { key: 'help', label: '帮助' },
];

const root = document.getElementById('root');

// 内嵌浏览器（CEF/旧 Chromium）里排障用：把未捕获错误留在窗口上
window.__errors = [];
window.addEventListener('error', (e) => {
  if (window.__errors.length < 50) window.__errors.push(String(e.message || e.error));
});
window.addEventListener('unhandledrejection', (e) => {
  if (window.__errors.length < 50) window.__errors.push('unhandled: ' + String(e.reason));
});

const app = {
  moduleState: null,
  user: null,
  role: '',
  wsStatus: 'connecting',
  progress: null,
  pages: new Map(),
  current: 'measure',
  shell: null,
  pollTimer: 0,
};

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
  renderTitlebar();
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
    renderTitlebar();
    for (const page of app.pages.values()) page.onState?.(state);
  } catch {
    /* 会话断开时忽略，状态条会提示 */
  }
}

// ---------------------------------------------------------------- 标题栏

const statusDot = el('span', { class: 'status-dot' });
const statusText = el('span', { class: 'meta', text: '连接中…' });
const deviceTag = el('span', { class: 'tag muted', text: '未连接' });
const firmwareText = el('span', { class: 'meta' });
const userTag = el('span', { class: 'tag info' });
const btnLogout = el('button', { class: 'secondary', text: '退出' });
const titlebar = el('div', { class: 'titlebar' });

function renderTitlebar() {
  if (!app.user) return;
  const state = app.moduleState;
  const connected = !!state?.connected;
  const simRunning = !!state?.sim?.running;
  statusDot.className = `status-dot ${app.wsStatus === 'open' ? 'on' : 'off'}`;
  statusText.textContent = app.wsStatus === 'open' ? '通讯正常' : '已断开';
  deviceTag.className = `tag ${connected ? 'pass' : 'muted'}`;
  deviceTag.textContent = connected ? `已连接 ${state.port || ''}` : '未连接';
  firmwareText.textContent = state?.device
    ? `固件 ${state.device.firmware} · P${state.device.pCount}/N${state.device.nCount} · MUX ${state.device.muxBoards}${
        simRunning ? ' · 仿真中' : ''
      }`
    : '';
  userTag.textContent = `${app.user} · ${app.role === 'admin' ? '管理员' : '操作员'}`;
}

function buildTitlebar() {
  clear(titlebar);
  titlebar.append(
    el('span', { class: 'app-name', text: 'EP-H205 综合测试系统' }),
    el('span', { style: { display: 'inline-flex', alignItems: 'center' } }, statusDot, statusText),
    deviceTag,
    firmwareText,
    el('span', { class: 'spacer' }),
    userTag,
    btnLogout,
  );
  renderTitlebar();
}

// ---------------------------------------------------------------- 页面

const tabBar = el('div', { class: 'tabs' });
const main = el('div', { class: 'main' });

function visiblePages() {
  return PAGES.filter((p) => !p.adminOnly || app.role === 'admin');
}

function switchPage(key) {
  if (!app.pages.has(key)) key = 'measure';
  app.current = key;
  for (const [k, page] of app.pages) {
    page.el.style.display = k === key ? '' : 'none';
    page.setActive?.(k === key);
  }
  for (const btn of tabBar.children) {
    btn.classList.toggle('active', btn.dataset.key === key);
  }
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

  clear(main);
  for (const [key, page] of app.pages) {
    page.el.style.display = key === 'measure' ? '' : 'none';
    main.appendChild(page.el);
  }
}

function buildTabs() {
  clear(tabBar);
  for (const page of visiblePages()) {
    const btn = el('button', { text: page.label });
    btn.dataset.key = page.key;
    btn.addEventListener('click', () => switchPage(page.key));
    tabBar.appendChild(btn);
  }
}

// ---------------------------------------------------------------- 登录 / 退出

async function logout() {
  await api.logout();
  teardownShell();
  renderLogin();
}

function teardownShell() {
  if (app.pollTimer) window.clearInterval(app.pollTimer);
  app.pollTimer = 0;
  app.pages.clear();
  app.moduleState = null;
  app.progress = null;
  app.user = null;
  app.role = '';
  clear(root);
  app.shell = null;
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
  buildTitlebar();
  buildTabs();
  const shell = el('div', { class: 'app' }, titlebar, tabBar, main);
  root.appendChild(shell);
  app.shell = shell;
  createPages();
  switchPage('measure');
  renderTitlebar();

  app.pollTimer = window.setInterval(() => void refreshState(), 2000);
  void api.subscribe(1).catch(() => undefined);
  void refreshState();
}

btnLogout.addEventListener('click', () => void logout());

// ---------------------------------------------------------------- 启动

async function boot() {
  clear(root);
  root.appendChild(
    el('div', { class: 'login-page' }, el('div', { class: 'field', text: '正在连接服务端…' })),
  );
  try {
    await api.connect();
    app.wsStatus = 'open';
  } catch {
    app.wsStatus = 'error';
    clear(root);
    root.appendChild(
      el('div', { class: 'login-page' }, el('div', { class: 'field', text: '无法连接服务端' })),
    );
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
