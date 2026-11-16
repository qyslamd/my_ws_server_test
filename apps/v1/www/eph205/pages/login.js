import { el, icon } from '../lib/ui.js';

export function createLoginPage({ api, onLogin }) {
  const error = el('div', { class: 'error-text' });
  const user = el('input', { id: 'username', type: 'text', value: 'admin', autocomplete: 'username' });
  const pass = el('input', { id: 'password', type: 'password', value: 'admin', autocomplete: 'current-password' });
  const button = el('button', { class: 'btn filled lg', text: '登录' });

  async function submit() {
    if (button.disabled) return;
    button.disabled = true;
    error.textContent = '';
    try {
      const out = await api.login(user.value.trim(), pass.value);
      onLogin({ user: out.user, role: out.role });
    } catch (err) {
      error.textContent = err.message === 'invalid credentials' ? '用户名或密码错误' : err.message;
    } finally {
      button.disabled = false;
    }
  }

  button.addEventListener('click', submit);
  const onEnter = (e) => {
    if (e.key === 'Enter') submit();
  };
  user.addEventListener('keydown', onEnter);
  pass.addEventListener('keydown', onEnter);

  const brand = el(
    'div',
    { class: 'login-brand' },
    el('div', { class: 'logo' }, el('span', { class: 'logo-mark', html: icon('box', 'sm') }), 'EP-H205'),
    el(
      'div',
      {},
      el('h1', { text: '综合测试系统' }),
      el('p', {
        class: 'tagline',
        text: '电阻导通、多路切换与批量判定：一套底座同时支撑浏览器与桌面两种形态。',
      }),
      el(
        'div',
        { class: 'facts' },
        el('div', { class: 'fact' }, el('div', { class: 'v', text: '640' }), el('div', { class: 'k', text: 'P/N 点位' })),
        el('div', { class: 'fact' }, el('div', { class: 'v', text: '6' }), el('div', { class: 'k', text: '电流量程' })),
        el('div', { class: 'fact' }, el('div', { class: 'v', text: '62' }), el('div', { class: 'k', text: '单帧批组' })),
      ),
    ),
    el('div', { class: 'small', style: { color: 'rgba(255,255,255,.66)' }, text: 'v0.1 · 离线部署' }),
  );

  const panel = el(
    'div',
    { class: 'login-panel' },
    el(
      'div',
      { class: 'login-card' },
      el('h2', { text: '登录' }),
      el('p', { class: 'sub', text: '请使用操作员或管理员账号进入系统' }),
      el('div', { class: 'field' }, el('label', { for: 'username', text: '用户名' }), user),
      el('div', { class: 'field' }, el('label', { for: 'password', text: '密码' }), pass),
      error,
      button,
      el(
        'div',
        { class: 'login-foot' },
        el('span', { text: 'admin/admin · operator/operator' }),
        el('span', { text: 'EP-H205' }),
      ),
    ),
  );

  return {
    el: el('div', { class: 'login-page' }, brand, panel),
    focus: () => user.focus(),
  };
}
