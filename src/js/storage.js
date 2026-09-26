/* =============================================================================
 * storage.js — 持久化层
 * 职责：localStorage 读写、schema 校验与迁移、备份恢复、宿主指令队列
 * 约束：本文件不包含任何业务规则；UI 与 store 均不得直接触碰 localStorage
 * ========================================================================== */
(function (global) {
  'use strict';

  var App = global.TodoApp = global.TodoApp || {};

  var KEY        = 'desktopTodoList.v1';
  var BACKUP_KEY = 'desktopTodoList.v1.backup';
  var HOSTQ_KEY  = 'desktopTodoList.hostQ';
  var SCHEMA_VERSION = 1;
  var SAVE_DEBOUNCE_MS = 300;

  var PRIORITIES = ['high', 'medium', 'low'];
  var MAX_TITLE = 200;
  var MAX_NOTE = 2000;
  var MAX_TAGS = 5;

  /* ---------------------------------------------------------------- defaults */

  function defaultSettings() {
    return {
      theme: 'system',
      defaultFilter: 'today',
      hotkey: 'Ctrl+Alt+T',
      hotkeySelectable: 'Ctrl+Alt+L',
      weekStartsOn: 1,
      remindAdvanceMinutes: 0,

      // 窗口行为（v1.1）
      autoStart: false,
      startMinimized: false,
      windowMode: 'normal',        // normal | floating
      windowLayer: 'normal',       // top | normal | bottom
      selectable: true,
      floatingGeometry: { x: null, y: null, w: 360, h: 480 },

      // 列表级选择（v1.2）
      multiSelectEnabled: true,
      rubberBandSelect: true,
      keepSelectionAcrossViews: true
    };
  }

  function createDefault() {
    return {
      schemaVersion: SCHEMA_VERSION,
      tasks: [],
      settings: defaultSettings()
    };
  }

  /* ------------------------------------------------------------------ utils */

  function uid() {
    var rand = Math.random().toString(36).slice(2, 6);
    return 't_' + Date.now().toString(36) + '_' + rand;
  }

  function isObj(v) {
    return v !== null && typeof v === 'object' && !Array.isArray(v);
  }

  function clampStr(v, max) {
    if (typeof v !== 'string') return '';
    return v.length > max ? v.slice(0, max) : v;
  }

  /** 解析为 Date；无法解析或空值时返回 null */
  function toDate(v) {
    if (!v) return null;
    var d = new Date(v);
    return isNaN(d.getTime()) ? null : d;
  }

  /* ----------------------------------------------------------- sanitization */

  /**
   * 将任意输入规整为合法的 Task 对象。
   * 这是数据完整性的唯一关口：所有外部输入（导入文件、可能损坏的 localStorage）
   * 都必须经过此函数，保证 store 拿到的数据一定是干净的。
   */
  function sanitizeTask(raw, fallbackOrder) {
    if (!isObj(raw)) return null;

    var title = clampStr(raw.title, MAX_TITLE).trim();
    if (!title) return null; // 无标题的任务无意义，直接丢弃

    var priority = PRIORITIES.indexOf(raw.priority) >= 0 ? raw.priority : 'medium';
    var status = raw.status === 'done' ? 'done' : 'todo';

    var tags = [];
    if (Array.isArray(raw.tags)) {
      for (var i = 0; i < raw.tags.length && tags.length < MAX_TAGS; i++) {
        var t = clampStr(raw.tags[i], 24).trim();
        if (t && tags.indexOf(t) < 0) tags.push(t);
      }
    }

    var dueAt = toDate(raw.dueAt);
    var completedAt = toDate(raw.completedAt);
    var createdAt = toDate(raw.createdAt) || new Date();

    // status 与 completedAt 必须自洽，避免出现"已完成但没有完成时间"
    if (status === 'done' && !completedAt) completedAt = createdAt;
    if (status === 'todo') completedAt = null;

    return {
      id: typeof raw.id === 'string' && raw.id ? raw.id : uid(),
      title: title,
      note: clampStr(raw.note || '', MAX_NOTE),
      priority: priority,
      status: status,
      dueAt: dueAt ? dueAt.toISOString() : null,
      remind: raw.remind !== false,
      remindedAt: toDate(raw.remindedAt) ? toDate(raw.remindedAt).toISOString() : null,
      tags: tags,
      order: typeof raw.order === 'number' && isFinite(raw.order)
        ? raw.order
        : (typeof fallbackOrder === 'number' ? fallbackOrder : 0),
      createdAt: createdAt.toISOString(),
      updatedAt: toDate(raw.updatedAt) ? toDate(raw.updatedAt).toISOString() : createdAt.toISOString(),
      completedAt: completedAt ? completedAt.toISOString() : null
    };
  }

  /** 校验并修复整个 state；返回 { state, repaired, dropped } */
  function sanitizeState(raw) {
    var def = createDefault();
    if (!isObj(raw)) return { state: def, repaired: true, dropped: 0 };

    var out = def;
    var dropped = 0;
    var repaired = false;

    if (raw.schemaVersion !== SCHEMA_VERSION) repaired = true;

    // tasks
    if (Array.isArray(raw.tasks)) {
      var seenIds = Object.create(null);
      var nextOrder = 1;
      for (var i = 0; i < raw.tasks.length; i++) {
        var t = sanitizeTask(raw.tasks[i], nextOrder);
        if (!t) { dropped++; continue; }
        if (seenIds[t.id]) { t.id = uid(); repaired = true; } // id 冲突则重新分配
        seenIds[t.id] = true;
        nextOrder = t.order + 1;
        out.tasks.push(t);
      }
    } else if (raw.tasks !== undefined) {
      repaired = true;
    }

    // settings：逐字段合并，任何非法值回退到默认值
    if (isObj(raw.settings)) {
      var s = raw.settings;
      var d = def.settings;

      if (['system', 'light', 'dark'].indexOf(s.theme) >= 0) out.settings.theme = s.theme;
      if (['today', 'week', 'all', 'done'].indexOf(s.defaultFilter) >= 0) out.settings.defaultFilter = s.defaultFilter;
      if (typeof s.hotkey === 'string' && s.hotkey) out.settings.hotkey = clampStr(s.hotkey, 40);
      if (typeof s.hotkeySelectable === 'string' && s.hotkeySelectable) out.settings.hotkeySelectable = clampStr(s.hotkeySelectable, 40);
      if (s.weekStartsOn === 0 || s.weekStartsOn === 1) out.settings.weekStartsOn = s.weekStartsOn;
      if (typeof s.remindAdvanceMinutes === 'number' && s.remindAdvanceMinutes >= 0) out.settings.remindAdvanceMinutes = s.remindAdvanceMinutes;

      if (typeof s.autoStart === 'boolean') out.settings.autoStart = s.autoStart;
      if (typeof s.startMinimized === 'boolean') out.settings.startMinimized = s.startMinimized;
      if (['normal', 'floating'].indexOf(s.windowMode) >= 0) out.settings.windowMode = s.windowMode;
      if (['top', 'normal', 'bottom'].indexOf(s.windowLayer) >= 0) out.settings.windowLayer = s.windowLayer;
      if (typeof s.selectable === 'boolean') out.settings.selectable = s.selectable;

      if (isObj(s.floatingGeometry)) {
        var g = s.floatingGeometry;
        var num = function (v, lo, hi, dflt) {
          return (typeof v === 'number' && isFinite(v) && v >= lo && v <= hi) ? v : dflt;
        };
        out.settings.floatingGeometry = {
          x: (typeof g.x === 'number' && isFinite(g.x)) ? g.x : null,
          y: (typeof g.y === 'number' && isFinite(g.y)) ? g.y : null,
          w: num(g.w, 260, 2000, d.w),
          h: num(g.h, 240, 2000, d.h)
        };
      }

      if (typeof s.multiSelectEnabled === 'boolean') out.settings.multiSelectEnabled = s.multiSelectEnabled;
      if (typeof s.rubberBandSelect === 'boolean') out.settings.rubberBandSelect = s.rubberBandSelect;
      if (typeof s.keepSelectionAcrossViews === 'boolean') out.settings.keepSelectionAcrossViews = s.keepSelectionAcrossViews;
    } else if (raw.settings !== undefined) {
      repaired = true;
    }

    return { state: out, repaired: repaired, dropped: dropped };
  }

  /* ------------------------------------------------------------------- load */

  function rawGet(key) {
    try {
      return global.localStorage.getItem(key);
    } catch (e) {
      return null;
    }
  }

  /**
   * 读取数据。返回 { state, status }，status 取值：
   *   'fresh'      — 首次运行，无任何数据
   *   'ok'         — 正常读取，数据完整
   *   'repaired'   — 数据存在但被修复（丢失部分字段）
   *   'restored'   — 主数据损坏，已从备份恢复
   *   'reset'      — 主数据与备份均损坏，已重置为空
   */
  function load() {
    var raw = rawGet(KEY);

    if (raw === null) {
      return { state: createDefault(), status: 'fresh' };
    }

    var parsed = null;
    try {
      parsed = JSON.parse(raw);
    } catch (e) {
      parsed = null;
    }

    if (parsed !== null) {
      var r = sanitizeState(parsed);
      if (!r.repaired) return { state: r.state, status: 'ok' };
      // 修复成功且确实拿到了任务数据，视为可用
      if (r.state.tasks.length > 0 || !isObj(parsed)) {
        return { state: r.state, status: 'repaired', dropped: r.dropped };
      }
    }

    // 主数据不可用 → 尝试备份
    var braw = rawGet(BACKUP_KEY);
    if (braw) {
      try {
        var bparsed = JSON.parse(braw);
        var br = sanitizeState(bparsed);
        // 只有当备份能拿回任务时才算恢复成功
        if (br.state.tasks.length > 0) {
          return { state: br.state, status: 'restored' };
        }
      } catch (e2) { /* 备份也不可用，继续往下 */ }
    }

    return { state: createDefault(), status: 'reset' };
  }

  /* ------------------------------------------------------------------- save */

  var saveTimer = null;
  var lastSaveError = null;

  function writeNow(state) {
    var payload = JSON.stringify(state);
    try {
      global.localStorage.setItem(KEY, payload);
      lastSaveError = null;
      return { ok: true };
    } catch (e) {
      // QuotaExceededError 或其他写入失败：必须让调用方知道，不允许静默失败
      lastSaveError = e && e.name === 'QuotaExceededError'
        ? '存储空间不足，请导出数据后清理'
        : '写入失败：' + (e && e.message ? e.message : '未知错误');
      return { ok: false, error: lastSaveError, quota: e && e.name === 'QuotaExceededError' };
    }
  }

  /** 防抖保存（300ms）。返回 Promise 不便，故通过 onSaveError 回调上报失败 */
  function save(state, immediate) {
    if (immediate) {
      if (saveTimer) { global.clearTimeout(saveTimer); saveTimer = null; }
      return writeNow(state);
    }
    if (saveTimer) global.clearTimeout(saveTimer);
    saveTimer = global.setTimeout(function () {
      saveTimer = null;
      var res = writeNow(state);
      if (!res.ok && typeof save.onError === 'function') save.onError(res);
    }, SAVE_DEBOUNCE_MS);
    return { ok: true, deferred: true };
  }

  /** 强制立即落盘，用于 beforeunload */
  function flush(state) {
    if (saveTimer) { global.clearTimeout(saveTimer); saveTimer = null; }
    return writeNow(state);
  }

  function getLastError() { return lastSaveError; }

  /* ----------------------------------------------------------------- backup */

  function writeBackup(state) {
    try {
      global.localStorage.setItem(BACKUP_KEY, JSON.stringify(state));
      return true;
    } catch (e) {
      return false;
    }
  }

  function hasBackup() {
    return rawGet(BACKUP_KEY) !== null;
  }

  function readBackup() {
    var braw = rawGet(BACKUP_KEY);
    if (!braw) return null;
    try {
      var r = sanitizeState(JSON.parse(braw));
      return r.state;
    } catch (e) {
      return null;
    }
  }

  /* --------------------------------------------------------- host command Q */

  /**
   * 宿主指令队列：页面写入，宿主轮询消费。
   * 用 localStorage 而非 WebSocket，避免端口占用与防火墙提示。
   */
  function pushHostCommand(cmd, payload) {
    try {
      var q = [];
      var raw = rawGet(HOSTQ_KEY);
      if (raw) {
        try { q = JSON.parse(raw) || []; } catch (e) { q = []; }
        if (!Array.isArray(q)) q = [];
      }
      // 队列积压说明宿主没在消费（例如纯浏览器模式），直接丢弃旧指令防止无限增长
      if (q.length > 60) q = q.slice(-20);
      q.push({ cmd: cmd, payload: payload || null, at: Date.now() });
      global.localStorage.setItem(HOSTQ_KEY, JSON.stringify(q));
      return true;
    } catch (e) {
      return false;
    }
  }

  function clearHostQueue() {
    try { global.localStorage.removeItem(HOSTQ_KEY); } catch (e) { /* ignore */ }
  }

  /* ------------------------------------------------------ import / export */

  function exportJson(state) {
    return JSON.stringify(state, null, 2);
  }

  /** 解析导入文件；返回 { ok, state } 或 { ok:false, error } */
  function parseImport(text) {
    var parsed;
    try {
      parsed = JSON.parse(text);
    } catch (e) {
      return { ok: false, error: '文件不是合法的 JSON 格式' };
    }
    // 兼容裸数组格式
    if (Array.isArray(parsed)) parsed = { schemaVersion: SCHEMA_VERSION, tasks: parsed };
    if (!isObj(parsed) || !Array.isArray(parsed.tasks)) {
      return { ok: false, error: '文件缺少 tasks 字段，可能不是本工具的导出文件' };
    }
    var r = sanitizeState(parsed);
    if (r.state.tasks.length === 0 && parsed.tasks.length > 0) {
      return { ok: false, error: '文件中的任务全部无效（缺少标题）' };
    }
    return { ok: true, state: r.state, dropped: r.dropped };
  }

  function reset() {
    try {
      global.localStorage.removeItem(KEY);
      global.localStorage.removeItem(BACKUP_KEY);
      global.localStorage.removeItem(HOSTQ_KEY);
    } catch (e) { /* ignore */ }
  }

  /* ------------------------------------------------------------------ export */

  App.storage = {
    KEY: KEY,
    BACKUP_KEY: BACKUP_KEY,
    HOSTQ_KEY: HOSTQ_KEY,
    SCHEMA_VERSION: SCHEMA_VERSION,
    PRIORITIES: PRIORITIES,
    MAX_TITLE: MAX_TITLE,

    createDefault: createDefault,
    defaultSettings: defaultSettings,
    uid: uid,
    sanitizeTask: sanitizeTask,
    sanitizeState: sanitizeState,

    load: load,
    save: save,
    flush: flush,
    getLastError: getLastError,

    writeBackup: writeBackup,
    hasBackup: hasBackup,
    readBackup: readBackup,

    pushHostCommand: pushHostCommand,
    clearHostQueue: clearHostQueue,

    exportJson: exportJson,
    parseImport: parseImport,
    reset: reset
  };
})(window);
