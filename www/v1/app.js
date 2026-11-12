'use strict';

const $ = (id) => document.getElementById(id);
const sid = 'sid-' + Math.random().toString(36).slice(2, 10);
let ws = null;

function log(text) {
  const el = $('log');
  el.textContent = `[${new Date().toLocaleTimeString()}] ${text}\n` + el.textContent;
  if (el.textContent.length > 12000) {
    el.textContent = el.textContent.slice(0, 12000);
  }
}

async function api(path) {
  const res = await fetch('/api/v1' + path, {
    headers: { 'X-Session-Id': sid },
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    throw new Error(data.msg || `${res.status} ${res.statusText}`);
  }
  return data;
}

async function refresh() {
  try {
    $('capabilities').textContent =
      JSON.stringify(await api('/capabilities'), null, 2);
    $('modules').textContent = JSON.stringify(await api('/modules'), null, 2);
    $('streams').textContent = JSON.stringify(await api('/streams'), null, 2);
  } catch (e) {
    log('刷新失败: ' + e.message);
  }
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(`${proto}://${location.host}/ws?sid=${sid}`);
  ws.onopen = () => {
    $('status').textContent = sid;
    $('status').className = 'status on';
    log('WS 已连接');
    refresh();
  };
  ws.onmessage = (ev) => {
    if (typeof ev.data === 'string') {
      log(ev.data);
    }
  };
  ws.onclose = () => {
    $('status').textContent = '已断开';
    $('status').className = 'status';
    setTimeout(connect, 1500);
  };
}

if (!new URLSearchParams(location.search).has('noconnect')) {
  connect();
} else {
  $('status').textContent = '未连接（noconnect）';
}
