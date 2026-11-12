// 测试项模型与导入导出。CSV / Excel / JSON 都归一成同一套二维表解析。

export const RESULT_TEXT = {
  pass: '通过',
  fail: '失败',
  measure_failed: '测量失败',
  not_measured: '未测',
};

export const REASON_TEXT = {
  over_range: '超量程',
  frame_failed: '整帧失败',
  setup_failed: '组参数设置失败',
  unchecked: '未勾选',
  cancelled: '已取消',
  aborted: '中断',
  disconnected: '连接断开',
};

export const RESULT_CLASS = {
  pass: 'pass',
  fail: 'fail',
  measure_failed: 'warn',
  not_measured: 'muted',
};

let counter = 0;

export function makeItem(p = 1, n = 2) {
  counter += 1;
  return {
    key: `k${counter}`,
    seq: 0,
    p,
    n,
    wire: 0,
    range: 0,
    minOhms: null,
    maxOhms: null,
    measure: true,
  };
}

function pick(row, ...names) {
  for (const name of names) {
    const key = Object.keys(row).find((k) => k.trim().toLowerCase() === name.toLowerCase());
    if (key !== undefined && row[key] !== '') return row[key];
  }
  return '';
}

function toNumber(text) {
  if (text === undefined || text === null || String(text).trim() === '') return null;
  const value = Number(text);
  return Number.isFinite(value) ? value : null;
}

function splitCsvLine(line) {
  const out = [];
  let cell = '';
  let quoted = false;
  for (let i = 0; i < line.length; i += 1) {
    const ch = line[i];
    if (quoted) {
      if (ch === '"' && line[i + 1] === '"') {
        cell += '"';
        i += 1;
      } else if (ch === '"') {
        quoted = false;
      } else {
        cell += ch;
      }
    } else if (ch === '"') {
      quoted = true;
    } else if (ch === ',') {
      out.push(cell);
      cell = '';
    } else {
      cell += ch;
    }
  }
  out.push(cell);
  return out;
}

/** 二维表：有表头按名字取列，没有就按 P,N[,下限,上限] 的位置取 */
export function parseRows(rows) {
  const cleaned = rows.filter((row) => row.some((cell) => String(cell ?? '').trim() !== ''));
  if (cleaned.length === 0) return { items: [], skipped: 0 };

  const asText = (row) => row.map((c) => String(c ?? '').trim());
  const first = asText(cleaned[0]);
  const hasHeader = first.some((c) => /^(p|n|序号|线制|量程|下限|上限|测量)$/i.test(c));
  const header = hasHeader ? first : ['P', 'N', '下限', '上限'];
  const body = hasHeader ? cleaned.slice(1) : cleaned;

  const items = [];
  let skipped = 0;
  body.forEach((cells, index) => {
    const texts = asText(cells);
    const row = {};
    header.forEach((name, i) => {
      row[name] = texts[i] ?? '';
    });
    const p = toNumber(pick(row, 'P', 'p'));
    const n = toNumber(pick(row, 'N', 'n'));
    if (p === null || n === null || p <= 0 || n <= 0) {
      skipped += 1;
      return;
    }
    const item = makeItem();
    item.seq = toNumber(pick(row, '序号', 'seq')) ?? index + 1;
    item.p = p;
    item.n = n;
    const wire = pick(row, '线制', 'wire');
    item.wire = wire === '四线' || wire === '1' || wire.toLowerCase() === '4w' ? 1 : 0;
    item.range = toNumber(pick(row, '量程', 'range')) ?? 0;
    item.minOhms = toNumber(pick(row, '下限', 'minOhms', 'min'));
    item.maxOhms = toNumber(pick(row, '上限', 'maxOhms', 'max'));
    const measure = pick(row, '测量', 'measure');
    if (measure !== '') {
      item.measure = !(
        measure === '0' ||
        measure === '否' ||
        measure.toLowerCase() === 'n' ||
        measure.toLowerCase() === 'false'
      );
    }
    items.push(item);
  });
  return { items, skipped };
}

function parseCsv(text) {
  const lines = text
    .split(/\r?\n/)
    .filter((l) => l.trim() !== '' && !l.trim().startsWith('#') && !l.trim().startsWith(';'));
  if (lines.length === 0) return { items: [], skipped: 0 };
  return parseRows(lines.map(splitCsvLine));
}

function parseJson(text) {
  const data = JSON.parse(text);
  const list = Array.isArray(data) ? data : data.items || [];
  const items = list.map((raw, index) => {
    const item = makeItem();
    item.seq = Number(raw.seq ?? index + 1);
    item.p = Number(raw.p ?? raw.P ?? 0);
    item.n = Number(raw.n ?? raw.N ?? 0);
    const wire = raw.wire ?? raw['线制'] ?? 0;
    item.wire = Number(wire) === 1 || wire === '四线' ? 1 : 0;
    item.range = Number(raw.range ?? raw['量程'] ?? 0);
    item.minOhms = toNumber(raw.minOhms ?? raw.min ?? raw['下限'] ?? '');
    item.maxOhms = toNumber(raw.maxOhms ?? raw.max ?? raw['上限'] ?? '');
    const measure = raw.measure ?? raw['测量'] ?? true;
    item.measure = !(
      measure === false ||
      measure === 0 ||
      measure === '0' ||
      measure === '否' ||
      measure === 'n' ||
      measure === 'N' ||
      measure === 'false'
    );
    return item;
  });
  return { items, skipped: 0, product: Array.isArray(data) ? undefined : data.product };
}

// SheetJS 用 <script> 懒加载：不导入 Excel 就不下载这 950 KB
let xlsxLoading = null;

function loadXlsx() {
  if (window.XLSX) return Promise.resolve(window.XLSX);
  if (!xlsxLoading) {
    xlsxLoading = new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = new URL('../vendor/xlsx.full.min.js', import.meta.url).href;
      script.onload = () => resolve(window.XLSX);
      script.onerror = () => reject(new Error('Excel 解析库加载失败'));
      document.head.appendChild(script);
    });
  }
  return xlsxLoading;
}

export async function parseFile(file) {
  const name = file.name.toLowerCase();
  if (name.endsWith('.xlsx') || name.endsWith('.xls')) {
    const XLSX = await loadXlsx();
    const buffer = await file.arrayBuffer();
    const book = XLSX.read(buffer, { type: 'array' });
    const sheetName = book.SheetNames.find((n) => n === '测试项') || book.SheetNames[0];
    if (!sheetName) return { items: [], skipped: 0 };
    const rows = XLSX.utils.sheet_to_json(book.Sheets[sheetName], {
      header: 1,
      defval: '',
      blankrows: false,
    });
    return parseRows(rows);
  }
  const text = await file.text();
  const trimmed = text.trim();
  if (name.endsWith('.json') || trimmed.startsWith('[') || trimmed.startsWith('{')) {
    return parseJson(trimmed);
  }
  return parseCsv(trimmed);
}

export function itemsToJson(items) {
  return items.map((item, index) => ({
    seq: item.seq || index + 1,
    p: item.p,
    n: item.n,
    wire: item.wire,
    range: item.range,
    minOhms: item.minOhms,
    maxOhms: item.maxOhms,
    measure: item.measure,
  }));
}

export function downloadText(fileName, text) {
  const blob = new Blob([text], { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = fileName;
  a.click();
  URL.revokeObjectURL(url);
}
