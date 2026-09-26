/* =============================================================================
 * tests/run-tests.js — 业务逻辑测试（Node 运行，无需浏览器）
 * -----------------------------------------------------------------------------
 * 目的：验证 store/storage 的纯逻辑正确性。UI 与宿主层不在此范围内。
 * 运行：node tests/run-tests.js
 * ========================================================================== */
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');

/* ------------------------------------------------------------ 断言与报告 */
let passed = 0;
const failures = [];
let currentSuite = '';

function suite(name) {
  currentSuite = name;
  console.log('\n\x1b[1m' + name + '\x1b[0m');
}

function check(label, cond, detail) {
  if (cond) {
    passed++;
    console.log('  \x1b[32m✓\x1b[0m ' + label);
  } else {
    failures.push(currentSuite + ' → ' + label + (detail ? '  [' + detail + ']' : ''));
    console.log('  \x1b[31m✗\x1b[0m ' + label + (detail ? '  \x1b[90m' + detail + '\x1b[0m' : ''));
  }
}

function eq(label, actual, expected) {
  check(label, actual === expected, 'actual=' + JSON.stringify(actual) + ' expected=' + JSON.stringify(expected));
}

/* ------------------------------------------------------- 浏览器环境沙箱 */
function makeLocalStorage(initial) {
  const map = new Map(Object.entries(initial || {}));
  let quotaBytes = Infinity;
  return {
    getItem: k => (map.has(k) ? map.get(k) : null),
    setItem: (k, v) => {
      const s = String(v);
      if (s.length > quotaBytes) {
        const err = new Error('quota exceeded');
        err.name = 'QuotaExceededError';
        throw err;
      }
      map.set(k, s);
    },
    removeItem: k => { map.delete(k); },
    clear: () => map.clear(),
    _setQuota: n => { quotaBytes = n; },
    _keys: () => Array.from(map.keys()),
    _dump: () => Object.fromEntries(map)
  };
}

/** 加载 storage.js + store.js 到隔离沙箱 */
function createApp(seed) {
  const localStorage = makeLocalStorage(seed);
  const sandbox = {
    localStorage,
    console,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    Promise,
    Math,
    Date,
    JSON,
    Object,
    Array,
    Set,
    Map,
    String,
    Number,
    Boolean,
    isNaN,
    isFinite,
    parseInt,
    parseFloat,
    Error
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);

  for (const f of ['src/js/storage.js', 'src/js/store.js']) {
    const code = fs.readFileSync(path.join(ROOT, f), 'utf8');
    vm.runInContext(code, sandbox, { filename: f });
  }
  return { App: sandbox.TodoApp, localStorage, sandbox };
}

/** 新建一个已初始化的 store；fixedNow 用于固定"当前时间" */
function newStore(opts) {
  opts = opts || {};
  const ctx = createApp(opts.seed);
  const store = new ctx.App.Store().init(opts.state);
  store._enabled = false;                       // 测试中不写 localStorage
  if (opts.now) store.now = () => new Date(opts.now);
  return { store, App: ctx.App, localStorage: ctx.localStorage, sandbox: ctx.sandbox };
}

/** 直接构造一个合法 state（绕过 localStorage） */
function stateWith(tasks, settings) {
  const { App } = createApp();
  const s = App.storage.createDefault();
  s.tasks = tasks.map((t, i) => App.storage.sanitizeTask(t, i + 1)).filter(Boolean);
  Object.assign(s.settings, settings || {});
  return s;
}

const D = iso => new Date(iso).toISOString();

/* =========================================================================
 * 1. storage.sanitizeTask — 数据完整性关口
 * ====================================================================== */
suite('storage.sanitizeTask 数据校验');

{
  const { App } = createApp();
  const S = App.storage;

  check('拒绝 null', S.sanitizeTask(null, 1) === null);
  check('拒绝纯空白标题', S.sanitizeTask({ title: '   ' }, 1) === null);
  check('拒绝缺标题', S.sanitizeTask({ note: 'x' }, 1) === null);

  const t = S.sanitizeTask({ title: '  提交周报  ' }, 5);
  eq('标题被 trim', t.title, '提交周报');
  eq('非法 priority 回退为 medium', t.priority, 'medium');
  eq('缺失 status 回退为 todo', t.status, 'todo');
  eq('order 取 fallbackOrder', t.order, 5);
  check('自动生成 id', typeof t.id === 'string' && t.id.length > 0);
  eq('tags 缺失时为空数组', Array.isArray(t.tags) && t.tags.length, 0);
  eq('todo 任务的 completedAt 为 null', t.completedAt, null);

  const long = S.sanitizeTask({ title: 'x'.repeat(500) }, 1);
  eq('标题截断到 200', long.title.length, S.MAX_TITLE);

  const tags = S.sanitizeTask({ title: 'a', tags: ['x', 'x', 'y', 'z', 'w', 'v', 'u'] }, 1);
  eq('tags 去重', tags.tags.join(','), 'x,y,z,w,v');
  check('tags 数量不超过 5', tags.tags.length <= 5, 'len=' + tags.tags.length);

  const done = S.sanitizeTask({ title: 'a', status: 'done' }, 1);
  check('done 任务自动补 completedAt', !!done.completedAt);

  const bad = S.sanitizeTask({ title: 'a', status: 'todo', completedAt: D('2026-01-01T00:00:00') }, 1);
  eq('todo 任务清空 completedAt', bad.completedAt, null);

  const badDate = S.sanitizeTask({ title: 'a', dueAt: 'not-a-date' }, 1);
  eq('非法日期归为 null', badDate.dueAt, null);

  const inf = S.sanitizeTask({ title: 'a', order: Infinity }, 7);
  eq('Infinity order 回退为 fallback', inf.order, 7);
}

/* =========================================================================
 * 2. storage.load — 损坏恢复
 * ====================================================================== */
suite('storage.load 损坏与备份恢复');

{
  // 正常数据
  const { App } = createApp();
  const good = App.storage.createDefault();
  good.tasks = [App.storage.sanitizeTask({ title: '任务A' }, 1)];
  const ctx = createApp({ 'desktopTodoList.v1': JSON.stringify(good) });
  const r1 = ctx.App.storage.load();
  eq('完整数据 → status=ok', r1.status, 'ok');
  eq('任务数量正确', r1.state.tasks.length, 1);

  // 无数据
  const ctx2 = createApp();
  eq('无数据 → status=fresh', ctx2.App.storage.load().status, 'fresh');

  // 完全损坏的 JSON + 有可用备份
  const backup = App.storage.createDefault();
  backup.tasks = [App.storage.sanitizeTask({ title: '备份任务' }, 1), App.storage.sanitizeTask({ title: '备份任务2' }, 2)];
  const ctx3 = createApp({
    'desktopTodoList.v1': '{ this is not json',
    'desktopTodoList.v1.backup': JSON.stringify(backup)
  });
  const r3 = ctx3.App.storage.load();
  eq('主数据损坏 → status=restored', r3.status, 'restored');
  eq('从备份取回任务', r3.state.tasks.length, 2);

  // 损坏且无备份
  const ctx4 = createApp({ 'desktopTodoList.v1': 'null' });
  const r4 = ctx4.App.storage.load();
  check('损坏且无备份 → reset/fresh 之一', r4.status === 'reset' || r4.status === 'fresh', 'got=' + r4.status);
  eq('重置后任务为空', r4.state.tasks.length, 0);

  // 部分字段非法 → repaired
  const ctx5 = createApp({
    'desktopTodoList.v1': JSON.stringify({
      schemaVersion: 99,
      tasks: [{ title: 'ok' }, { title: '' }, null, 'garbage'],
      settings: { theme: 'rainbow', windowLayer: 'sideways', weekStartsOn: 9 }
    })
  });
  const r5 = ctx5.App.storage.load();
  check('非法字段被修复 (status=' + r5.status + ')', r5.status === 'repaired');
  eq('丢弃无效任务后剩 1 条', r5.state.tasks.length, 1);
  eq('非法 theme 回退 system', r5.state.settings.theme, 'system');
  eq('非法 windowLayer 回退 normal', r5.state.settings.windowLayer, 'normal');
  eq('非法 weekStartsOn 回退 1', r5.state.settings.weekStartsOn, 1);
}

/* =========================================================================
 * 3. store CRUD
 * ====================================================================== */
suite('store 任务增删改');

{
  const { store } = newStore();

  const t1 = store.addTask({ title: '第一个任务' });
  check('addTask 返回任务对象', !!t1 && t1.id);
  eq('任务数量为 1', store.allTasks().length, 1);

  const t2 = store.addTask({ title: '第二个任务' });
  check('新任务排在最前（order 更小）', t2.order < t1.order, 't2.order=' + t2.order + ' t1.order=' + t1.order);
  eq('getOrdered 顺序：新任务在前', store.getOrdered()[0].id, t2.id);

  check('addTask 拒绝空标题', store.addTask({ title: '   ' }) === null);
  eq('空标题未进入列表', store.allTasks().length, 2);

  // 更新
  check('updateTask 成功', store.updateTask(t1.id, { title: '改过的标题', priority: 'high' }));
  eq('标题已更新', store.getTask(t1.id).title, '改过的标题');
  eq('优先级已更新', store.getTask(t1.id).priority, 'high');
  check('updatedAt 已变新', new Date(store.getTask(t1.id).updatedAt).getTime() >= new Date(t1.createdAt).getTime());

  check('updateTask 对不存在 id 返回 false', store.updateTask('nope', { title: 'x' }) === false);

  // 完成 / 取消完成
  eq('初始为 todo', store.getTask(t1.id).status, 'todo');
  store.toggleDone(t1.id);
  eq('切换后为 done', store.getTask(t1.id).status, 'done');
  check('completedAt 已设置', !!store.getTask(t1.id).completedAt);
  store.toggleDone(t1.id);
  eq('再切换回 todo', store.getTask(t1.id).status, 'todo');
  eq('completedAt 被清空', store.getTask(t1.id).completedAt, null);
}

/* =========================================================================
 * 4. 排序权重与拖拽
 * ====================================================================== */
suite('store 排序与拖拽重排');

{
  const { store } = newStore();
  const a = store.addTask({ title: 'A' });
  const b = store.addTask({ title: 'B' });
  const c = store.addTask({ title: 'C' });
  // 注意：order 升序 = 列表从上到下。后添加的任务 order 更小，因此排在最前。
  eq('添加顺序（新的在最前）', store.getOrdered().map(t => t.title).join(''), 'CBA');
  check('order 单调递减', c.order < b.order && b.order < a.order,
        [c.order, b.order, a.order].join(' < '));

  store.moveTask(c.id, a.id, false);   // 把 C 插到 A 之前
  eq('C 插到 A 之前 → B,C,A', store.getOrdered().map(t => t.title).join(''), 'BCA');

  store.moveTask(c.id, a.id, true);    // 把 C 插到 A 之后
  eq('C 插到 A 之后 → B,A,C', store.getOrdered().map(t => t.title).join(''), 'BAC');

  store.moveTask(b.id, c.id, true);    // 把 B 移到最后
  eq('B 移到最后 → A,C,B', store.getOrdered().map(t => t.title).join(''), 'ACB');

  const before = store.getOrdered().map(t => t.title).join('');
  store.moveTask(a.id, a.id, false);
  eq('拖到自己身上顺序不变', store.getOrdered().map(t => t.title).join(''), before);

  // 拖到不存在的目标应静默失败而不是破坏顺序
  store.moveTask(a.id, 'no-such-id', false);
  eq('目标不存在时顺序不变', store.getOrdered().map(t => t.title).join(''), before);

  // 连续插入到同一位置，权重会被夹逼到极窄区间 → 应触发紧凑化
  for (let i = 0; i < 40; i++) {
    const t = store.addTask({ title: 'X' + i });
    store.moveTask(t.id, b.id, false);
  }
  const orders = store.getOrdered().map(t => t.order);
  const minGap = Math.min(...orders.slice(1).map((o, i) => Math.abs(o - orders[i])));
  check('权重被紧凑化，未退化为 0 间距', minGap > 1e-6, 'minGap=' + minGap);
  check('任务数量正确', store.allTasks().length, 3 + 40, 'len=' + store.allTasks().length);

  // moveByOffset：向下移动一位 = 在列表中往后排
  const ordered = store.getOrdered();
  const firstId = ordered[0].id, secondId = ordered[1].id;
  store.moveByOffset(firstId, 1);
  eq('下移一位后与第二位交换', store.getOrdered()[0].id, secondId);
  check('边界上移不越界', store.moveByOffset(store.getOrdered()[0].id, -1) === false);
}

/* =========================================================================
 * 5. 视图筛选与排序规则
 * ====================================================================== */
suite('视图筛选、排序与统计');

{
  const NOW = '2026-02-14T10:00:00';   // 周六
  const { store, App } = newStore({
    now: NOW,
    state: stateWith([
      { title: '今天到期', dueAt: D('2026-02-14T17:00:00'), priority: 'medium' },
      { title: '已逾期', dueAt: D('2026-02-13T09:00:00'), priority: 'low' },
      { title: '明天到期', dueAt: D('2026-02-15T09:00:00'), priority: 'medium' },
      { title: '本周内', dueAt: D('2026-02-17T09:00:00'), priority: 'medium' },
      { title: '下周', dueAt: D('2026-02-24T09:00:00'), priority: 'medium' },
      { title: '无截止' },
      { title: '已完成', status: 'done', completedAt: D('2026-02-14T08:00:00') }
    ])
  });

  const today = (store.filter = 'today', store.visibleTasks().map(t => t.title));
  check('今天视图含今天到期与逾期', today.includes('今天到期') && today.includes('已逾期'), today.join('|'));
  check('今天视图不含明天到期', !today.includes('明天到期'), today.join('|'));
  check('今天视图不含无截止任务', !today.includes('无截止'), today.join('|'));

  store.filter = 'week';
  const week = store.visibleTasks().map(t => t.title);
  check('本周视图含明天到期', week.includes('明天到期'), week.join('|'));
  check('本周视图含逾期任务', week.includes('已逾期'), week.join('|'));
  check('本周视图不含下周任务', !week.includes('下周'), week.join('|'));

  store.filter = 'all';
  const all = store.visibleTasks().map(t => t.title);
  eq('全部视图为未完成任务数', all.length, 6);
  check('全部视图不含已完成', !all.includes('已完成'), all.join('|'));
  eq('逾期任务排在最前', all[0], '已逾期');

  store.filter = 'done';
  eq('已完成视图只有 1 条', store.visibleTasks().length, 1);

  // 统计
  const counts = store.counts();
  eq('overdue 计数', counts.overdue, 1);
  eq('done 计数', counts.done, 1);
  eq('all 计数', counts.all, 6);
  eq('today 计数（今天到期 + 逾期）', counts.today, 2);

  // 优先级排序（同状态、同逾期性时高优先级在前）
  const { store: s2 } = newStore({
    now: NOW,
    state: stateWith([
      { title: '低', priority: 'low' },
      { title: '高', priority: 'high' },
      { title: '中', priority: 'medium' }
    ])
  });
  s2.filter = 'all';
  eq('按优先级降序排列', s2.visibleTasks().map(t => t.title).join(''), '高中低');

  // 搜索
  const { store: s3 } = newStore({
    state: stateWith([
      { title: '写需求文档', note: '包含窗口行为' },
      { title: '买牛奶', tags: ['生活'] },
      { title: '看技术书', tags: ['学习'] }
    ])
  });
  s3.filter = 'all';
  s3.setQuery('需求');
  eq('按标题命中', s3.visibleTasks().length, 1);
  s3.setQuery('窗口');
  eq('按备注命中', s3.visibleTasks().length, 1);
  s3.setQuery('学习');
  eq('按标签命中', s3.visibleTasks().length, 1);
  s3.setQuery('不存在的东西');
  eq('无命中时为空', s3.visibleTasks().length, 0);
}

/* =========================================================================
 * 6. 选择模型（列表级"可选中"）
 * ====================================================================== */
suite('列表多选：单选 / Ctrl / Shift / 全选');

function ids(store) { return Array.from(store.selectedIds); }

{
  const { store } = newStore({
    state: stateWith([
      { title: 'A' }, { title: 'B' }, { title: 'C' }, { title: 'D' }, { title: 'E' }
    ])
  });
  store.filter = 'all';
  const list = store.visibleTasks();
  const [a, b, c, d, e] = list.map(t => t.id);

  store.selectClick(a, {});
  eq('普通点击 → 单选', ids(store).join(''), a);

  store.selectClick(c, {});
  eq('再点其他 → 选择转移', ids(store).join(''), c);

  store.selectClick(c, {});
  eq('再点已选中项 → 取消选择', ids(store).length, 0);

  store.selectClick(a, {});
  store.selectClick(c, { ctrl: true });
  eq('Ctrl 追加选择', ids(store).length, 2);
  check('包含 A 与 C', store.isSelected(a) && store.isSelected(c));

  store.selectClick(c, { ctrl: true });
  eq('Ctrl 再点取消该项', ids(store).length, 1);

  // Shift 连选：锚点是最近一次点击的 c
  store.selectClick(b, {});
  store.selectClick(e, { shift: true });
  eq('Shift 连选 B→E 共 4 项', ids(store).length, 4);
  check('范围含 C、D', store.isSelected(c) && store.isSelected(d));
  check('范围不含 A', !store.isSelected(a));

  store.selectClick(a, {});
  store.selectClick(e, { shift: true });
  eq('Shift 反向连选 A→E 共 5 项', ids(store).length, 5);

  // selectAllVisible 是"全选/全不选"切换语义：已全选时再按一次应清空
  store.selectAllVisible();
  eq('已全选时再全选 → 清空', ids(store).length, 0);
  store.selectAllVisible();
  eq('未选中时全选 → 选中全部 5 项', ids(store).length, 5);

  // 关闭多选时退化为单选
  store.setSettings({ multiSelectEnabled: false }, true);
  store.selectClick(a, {});
  store.selectClick(c, { ctrl: true });
  eq('禁用多选后 Ctrl 无效，仍为单选', ids(store).length, 1);

  // 数据变更后清理无效选中（FR-96）
  store.setSettings({ multiSelectEnabled: true }, true);
  store.selectClick(a, {});
  store.selectClick(b, { ctrl: true });
  store.removeTask(a);
  check('删除任务后其 id 不再残留在选中集合', !store.isSelected(a));
  eq('另一项选中状态保留', ids(store).length, 1);

  // 跨视图选择收敛（FR-95）
  const { store: s2 } = newStore({
    now: '2026-02-14T10:00:00',
    state: stateWith([
      { title: '今天的事', dueAt: D('2026-02-14T18:00:00') },
      { title: '没有截止的事' }
    ])
  });
  s2.filter = 'all';
  const allIds = s2.visibleTasks().map(t => t.id);
  s2.selectIds(allIds);
  eq('全部视图中选中 2 项', ids(s2).length, 2);
  s2.filter = 'today';
  eq('切换视图后选中集合保持不变', ids(s2).length, 2);
  eq('当前视图可见选中数为 1', s2.selectedVisible().length, 1);
  eq('视图外选中数为 1', s2.selectedHiddenCount(), 1);
  s2.narrowSelectionToVisible();
  eq('收敛到当前视图后剩 1 项', ids(s2).length, 1);
}

/* =========================================================================
 * 7. 撤销（单条删除 / 批量删除 / 取消完成）
 * ====================================================================== */
suite('撤销机制');

{
  const { store } = newStore({
    state: stateWith([{ title: 'A' }, { title: 'B' }, { title: 'C' }])
  });
  store.filter = 'all';

  const bId = store.visibleTasks().find(t => t.title === 'B').id;
  store.removeTask(bId);
  eq('删除后剩 2 条', store.allTasks().length, 2);
  check('存在可撤销记录', !!store.getUndo());
  check('撤销标签包含任务名', store.getUndo().label.includes('B'), store.getUndo().label);

  store.undo();
  eq('撤销后恢复为 3 条', store.allTasks().length, 3);
  check('恢复的任务内容正确', store.allTasks().some(t => t.title === 'B'));

  // 批量删除整体撤销
  const allIds = store.visibleTasks().map(t => t.id);
  store.selectIds(allIds);
  store.removeTasks(allIds);
  eq('批量删除后为空', store.allTasks().length, 0);
  store.undo();
  eq('整体撤销恢复 3 条', store.allTasks().length, 3);

  // 取消完成也会被记录（快照覆盖）
  const first = store.visibleTasks()[0];
  store.toggleDone(first.id);
  check('已标记完成', store.getTask(first.id).status === 'done');
  store.undo();
  eq('撤销恢复为未完成', store.getTask(first.id).status, 'todo');
}

/* =========================================================================
 * 8. 存储失败与导入导出
 * ====================================================================== */
suite('存储写入失败 / 导入导出');

{
  // QuotaExceeded 必须被捕获并给出可读错误
  const ctx = createApp();
  const st = ctx.App.storage.createDefault();
  st.tasks = [ctx.App.storage.sanitizeTask({ title: 'x' }, 1)];
  ctx.localStorage._setQuota(10);
  const res = ctx.App.storage.save(st, true);
  check('配额超限时 ok=false', res.ok === false);
  check('配额超限时标记 quota', res.quota === true);
  check('错误信息可读', typeof res.error === 'string' && res.error.length > 0, res.error);

  // 导出 → 解析 → 导入
  const { store, App } = newStore({
    state: stateWith([
      { title: '任务一', priority: 'high', tags: ['工作'] },
      { title: '任务二', note: '备注内容' }
    ])
  });
  const json = App.storage.exportJson(store.state);
  const parsed = App.storage.parseImport(json);
  check('导出后可被解析', parsed.ok === true);
  eq('任务数量一致', parsed.state.tasks.length, 2);
  eq('标签保留', parsed.state.tasks.find(t => t.title === '任务一').tags.join(','), '工作');

  const bad = App.storage.parseImport('{ broken');
  check('非法 JSON 被拒绝', bad.ok === false && !!bad.error, bad.error);

  const noTasks = App.storage.parseImport('{"settings":{}}');
  check('缺少 tasks 被拒绝', noTasks.ok === false, noTasks.error);

  // 裸数组兼容
  const bare = App.storage.parseImport(JSON.stringify([{ title: '裸数组任务' }]));
  check('兼容裸数组格式', bare.ok === true && bare.state.tasks.length === 1);

  // 合并导入按 id 去重
  const { store: s2, App: A2 } = newStore({
    state: stateWith([{ title: '原有' }])
  });
  const incoming = A2.storage.createDefault();
  incoming.tasks = [
    A2.storage.sanitizeTask({ id: 'same', title: '导入覆盖' }, 1),
    A2.storage.sanitizeTask({ id: 'new1', title: '新增' }, 2)
  ];
  s2.state.tasks = [A2.storage.sanitizeTask({ id: 'same', title: '原有' }, 1)];
  const added = s2.importState(incoming, true);
  eq('合并导入新增 1 条', added, 1);
  eq('合并后共 2 条', s2.allTasks().length, 2);
  eq('同 id 被覆盖', s2.getTask('same').title, '导入覆盖');
}

/* =========================================================================
 * 9. 边界与异常
 * ====================================================================== */
suite('边界情况');

{
  const { store } = newStore();
  eq('空列表 counts 全为 0', JSON.stringify(store.counts()), JSON.stringify({ today: 0, week: 0, all: 0, done: 0, overdue: 0 }));
  eq('空列表 visibleTasks 为空', store.visibleTasks().length, 0);
  check('空列表 clearSelection 不报错', (store.clearSelection(), true));
  check('空列表 selectAllVisible 不报错', (store.selectAllVisible(), true));
  check('空列表 undo 返回 false', store.undo() === false);
  eq('空列表 clearCompleted 返回 0', store.clearCompleted(), 0);
  check('空列表 moveByOffset 返回 false', store.moveByOffset('x', 1) === false);

  store.addTask({ title: '唯一任务' });
  const only = store.allTasks()[0];
  check('删除最后一条不报错', store.removeTask(only.id));
  eq('删除后为空', store.allTasks().length, 0);
  store.undo();
  eq('撤销恢复最后一条', store.allTasks().length, 1);
}

/* =========================================================================
 * 结果
 * ====================================================================== */
console.log('\n' + '─'.repeat(58));
if (failures.length === 0) {
  console.log('\x1b[32m\x1b[1m全部通过\x1b[0m  ' + passed + ' 项断言');
  process.exit(0);
} else {
  console.log('\x1b[31m\x1b[1m失败 ' + failures.length + ' 项\x1b[0m（通过 ' + passed + ' 项）');
  failures.forEach(f => console.log('  \x1b[31m•\x1b[0m ' + f));
  process.exit(1);
}
