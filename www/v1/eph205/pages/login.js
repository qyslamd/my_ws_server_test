import { el } from '../lib/ui.js';

export function createLoginPage({ api, onLogin }) {
  const error = el('div', { class: 'error' });
  const user = el('input', { id: 'username', type: 'text', value: 'admin', autocomplete: 'username' });
  const pass = el('input', { id: 'password', type: 'password', value: 'admin', autocomplete: 'current-password' });
  const button = el('button', { text: '登录' });

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

  const root = el(
    'div',
    { class: 'login-page' },
    el(
      'div',
      { class: 'login-card' },
      el('h1', { text: 'EP-H205 综合测试系统' }),
      el('div', { class: 'sub', text: '请登录后使用' }),
      el('div', { class: 'field-row' }, el('label', { text: '用户名' }), user),
      el('div', { class: 'field-row' }, el('label', { text: '密码' }), pass),
      error,
      button,
      el(
        'div',
        { class: 'accounts' },
        el('div', {}, '内置账号：'),
        el('span', { class: 'tag info', text: 'admin / admin　管理员' }),
        ' ',
        el('span', { class: 'tag', text: 'operator / operator　操作员' }),
      ),
    ),
  );

  return { el: root, focus: () => user.focus() };
}
