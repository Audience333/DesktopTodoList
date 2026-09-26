/* =============================================================================
 * store.js — 应用层：状态与业务规则
 * 职责：任务 CRUD、排序权重、视图筛选、统计、选择模型、撤销、设置
 * 约定：所有状态变更必须走本文件的方法；变更后统一 notify 订阅者
 * ========================================================================== */
(function (global) {
  'use strict';

  var App = global.TodoApp = global.TodoApp || {};
  var S = App.storage;

  var UNDO_WINDOW_MS = 5000;   // 删除后的撤销窗口（FR-06 / FR-90）
  var ORDER_MIN_GAP = 1e-6;    // 权重过密时触发紧凑化
  var ORDER_COMPACT_AT = 1e6;  // 权重超过该值也触发紧凑化，避免浮点精度退化

  /* ---------------------------------------------------------- date helpers */

  function startOfDay(d) {
    var x = new Date(d.getTime());
    x.setHours(0, 0, 0, 0);
    return x;
  }

  function addDays(d, n) {
    var x = new Date(d.getTime());
    x.setDate(x.getDate() + n);
    return x;
  }

  function isSameDay(a, b) {
    return a.getFullYear() === b.getFullYear()
        && a.getMonth() === b.getMonth()
        && a.getDate() === b.getDate();
  }

  /** 本周区间 [start, end)。周一或周日开始由设置决定 */
  function weekRange(now, weekStartsOn) {
    var day = now.getDay();                       // 0=周日
    var offset = (day - weekStartsOn + 7) % 7;    // 距离本周起始的天数
    var start = addDays(startOfDay(now), -offset);
    return { start: start, end: addDays(start, 7) };
  }

  /* ----------------------------------------------------------------- Store */

  function Store() {
    this.state = null;
    this.selectedIds = new Set();
    this.anchorId = null;
    this.focusId = null;
    this.filter = 'all';
    this.query = '';
    this.undoStack = [];        // { label, snapshot, expiresAt }
    this.loadStatus = 'ok';
    this._subs = [];
    this._notifyTimer = null;
    this._enabled = true;       // 测试时可关闭持久化
  }

  Store.prototype = {

    /* --------------------------------------------------------- lifecycle */

    init: function (injectedState) {
      if (injectedState) {
        this.state = injectedState;
        this.loadStatus = 'ok';
      } else {
        var res = S.load();
        this.state = res.state;
        this.loadStatus = res.status;
        this.loadDropped = res.dropped || 0;
      }
      this.filter = this.state.settings.defaultFilter || 'all';
      return this;
    },

    subscribe: function (fn) {
      this._subs.push(fn);
      return function () {
        var i = this._subs.indexOf(fn);
        if (i >= 0) this._subs.splice(i, 1);
      }.bind(this);
    },

    /** 合并同一轮内的多次变更，避免一次用户操作触发多次重绘 */
    notify: function () {
      if (this._notifyTimer) return;
      var self = this;
      this._notifyTimer = global.setTimeout(function () {
        self._notifyTimer = null;
        for (var i = 0; i < self._subs.length; i++) {
          try { self._subs[i](self); } catch (e) {
            if (global.console) console.error('[store] 订阅者异常', e);
          }
        }
      }, 0);
    },

    persist: function (immediate) {
      if (!this._enabled) return { ok: true };
      return S.save(this.state, immediate);
    },

    /* ------------------------------------------------------------- query */

    getTask: function (id) {
      for (var i = 0; i < this.state.tasks.length; i++) {
        if (this.state.tasks[i].id === id) return this.state.tasks[i];
      }
      return null;
    },

    allTasks: function () { return this.state.tasks; },

    /* -------------------------------------------------------------- CRUD */

    /**
     * 新增任务。order 取当前最大权重 + 1 之外，改为插入到列表顶部
     * （最新记录的最想被看到），故取最小权重 - 1。
     */
    addTask: function (data) {
      var now = new Date().toISOString();
      var minOrder = 0;
      if (this.state.tasks.length) {
        minOrder = Math.min.apply(null, this.state.tasks.map(function (t) { return t.order; }));
      }
      var task = S.sanitizeTask({
        id: S.uid(),
        title: data.title,
        note: data.note || '',
        priority: data.priority || 'medium',
        status: 'todo',
        dueAt: data.dueAt || null,
        remind: data.remind !== false,
        tags: data.tags || [],
        order: minOrder - 1,
        createdAt: now,
        updatedAt: now
      }, minOrder - 1);
      if (!task) return null;

      this.state.tasks.push(task);
      this.persist();
      this.notify();
      return task;
    },

    /** 局部更新；返回是否发生变化 */
    updateTask: function (id, patch) {
      var t = this.getTask(id);
      if (!t) return false;

      this.pushUndo('编辑任务：' + t.title, [id]);

      for (var k in patch) {
        if (!Object.prototype.hasOwnProperty.call(patch, k)) continue;
        if (k === 'id' || k === 'createdAt') continue; // 不可变字段
        t[k] = patch[k];
      }

      // 用 sanitize 重新规整，保证任何单字段修改都不会破坏整体一致性
      var clean = S.sanitizeTask(t, t.order);
      if (!clean) return false;
      clean.id = t.id;                 // sanitize 可能改 id（冲突时），此处强制保留
      clean.createdAt = t.createdAt;
      clean.updatedAt = new Date().toISOString();
      if (patch.status === undefined && clean.status === 'done' && !t.completedAt) {
        clean.completedAt = new Date().toISOString();
      }

      var idx = this.state.tasks.indexOf(t);
      this.state.tasks[idx] = clean;
      this.persist();
      this.notify();
      return true;
    },

    toggleDone: function (id, done) {
      var t = this.getTask(id);
      if (!t) return false;
      var next = (done === undefined) ? (t.status !== 'done') : !!done;
      if (next === (t.status === 'done')) return false;

      this.pushUndo((next ? '完成任务：' : '取消完成：') + t.title, [id]);

      t.status = next ? 'done' : 'todo';
      t.completedAt = next ? new Date().toISOString() : null;
      t.updatedAt = new Date().toISOString();
      if (next) t.remindedAt = t.remindedAt || null;
      this.persist();
      this.notify();
      return true;
    },

    /* -------------------------------------------------------------- undo */

    /**
     * 压入一次可撤销快照。
     * ids 为 null 时快照全部任务；否则只快照指定任务。
     *
     * 注意：任何会改写任务的入口都必须先调用它，否则撤销会退回到"上一次
     * 有快照的操作"的状态，把中间未记录的操作一起抹掉（这是曾经的真实 bug）。
     */
    pushUndo: function (label, ids, skipNotify) {
      var snapshot = [];
      for (var i = 0; i < this.state.tasks.length; i++) {
        var t = this.state.tasks[i];
        if (ids && ids.indexOf(t.id) < 0) continue;
        snapshot.push(JSON.parse(JSON.stringify(t)));
      }
      if (!snapshot.length) return;

      this.undoStack.push({
        label: label,
        tasks: snapshot,
        expiresAt: Date.now() + UNDO_WINDOW_MS
      });
      // 只保留最近一条：用户按"撤销"期望的是回退最后一步，栈过深反而语义含混
      if (this.undoStack.length > 1) this.undoStack.shift();
      this._scheduleUndoExpiry();
      // 内部调用方随后还会自己 notify 一次，避免同一次操作触发两轮重绘
      if (!skipNotify) this.notify();
    },

    _scheduleUndoExpiry: function () {
      var self = this;
      if (this._undoTimer) global.clearTimeout(this._undoTimer);
      var top = this.undoStack[this.undoStack.length - 1];
      if (!top) return;
      this._undoTimer = global.setTimeout(function () {
        self.pruneUndo();
        self.notify();
      }, Math.max(0, top.expiresAt - Date.now()) + 30);
    },

    pruneUndo: function () {
      var now = Date.now();
      this.undoStack = this.undoStack.filter(function (u) { return u.expiresAt > now; });
      return this.undoStack;
    },

    getUndo: function () {
      this.pruneUndo();
      return this.undoStack[this.undoStack.length - 1] || null;
    },

    /** 执行撤销：把快照里的任务重新放回（已存在的按覆盖处理） */
    undo: function () {
      var top = this.getUndo();
      if (!top) return false;

      var byId = Object.create(null);
      for (var i = 0; i < this.state.tasks.length; i++) byId[this.state.tasks[i].id] = i;

      for (var j = 0; j < top.tasks.length; j++) {
        var snap = top.tasks[j];
        if (byId[snap.id] !== undefined) {
          this.state.tasks[byId[snap.id]] = snap;    // 覆盖（取消完成、恢复优先级等）
        } else {
          this.state.tasks.push(snap);               // 重新插入（删除撤销）
          this.selectedIds.add(snap.id);             // 恢复选中，便于用户确认恢复结果
        }
      }

      this.undoStack.pop();
      this.persist();
      this.notify();
      return true;
    },

    /* ------------------------------------------------------------ delete */

    removeTask: function (id) {
      var t = this.getTask(id);
      if (!t) return false;
      this.pushUndo('删除任务：' + t.title, [id]);
      this.state.tasks = this.state.tasks.filter(function (x) { return x.id !== id; });
      this.selectedIds.delete(id);                  // FR-96：清理失效选中
      this.persist();
      this.notify();
      return true;
    },

    removeTasks: function (ids) {
      var self = this;
      var valid = ids.filter(function (id) { return !!self.getTask(id); });
      if (!valid.length) return 0;

      this.pushUndo('删除 ' + valid.length + ' 个任务', valid);
      this.state.tasks = this.state.tasks.filter(function (x) { return valid.indexOf(x.id) < 0; });
      for (var i = 0; i < valid.length; i++) this.selectedIds.delete(valid[i]);
      this.persist();
      this.notify();
      return valid.length;
    },

    /** 清理已完成任务（FR-07），可撤销 */
    clearCompleted: function () {
      var ids = this.state.tasks.filter(function (t) { return t.status === 'done'; })
                               .map(function (t) { return t.id; });
      if (!ids.length) return 0;
      return this.removeTasks(ids);
    },

    /* ------------------------------------------------------ batch update */

    setDoneFor: function (ids, done) {
      var n = 0;
      var changed = [];
      for (var i = 0; i < ids.length; i++) {
        var t = this.getTask(ids[i]);
        if (!t) continue;
        if ((t.status === 'done') === !!done) continue;
        changed.push(t.id);
        n++;
      }
      if (!n) return 0;

      this.pushUndo((done ? '批量完成 ' : '批量恢复 ') + n + ' 项', changed);

      for (var j = 0; j < changed.length; j++) {
        var task = this.getTask(changed[j]);
        task.status = done ? 'done' : 'todo';
        task.completedAt = done ? new Date().toISOString() : null;
        task.updatedAt = new Date().toISOString();
      }
      this.persist();
      this.notify();
      return n;
    },

    setPriorityFor: function (ids, priority) {
      if (S.PRIORITIES.indexOf(priority) < 0) return 0;
      var n = 0;
      var changed = [];
      for (var i = 0; i < ids.length; i++) {
        var t = this.getTask(ids[i]);
        if (!t || t.priority === priority) continue;
        changed.push(t.id);
        n++;
      }
      if (!n) return 0;

      this.pushUndo('批量设置优先级：' + n + ' 项', changed);

      for (var j = 0; j < changed.length; j++) {
        var task = this.getTask(changed[j]);
        task.priority = priority;
        task.updatedAt = new Date().toISOString();
      }
      this.persist();
      this.notify();
      return n;
    },

    /* ------------------------------------------------------------ ordering */

    getOrdered: function () {
      return this.state.tasks.slice().sort(function (a, b) { return a.order - b.order; });
    },

    /**
     * 拖拽重排。把 dragId 移动到 targetId 之前或之后。
     * 权重取相邻两值中点，避免每次拖拽都全量重排。
     */
    moveTask: function (dragId, targetId, placeAfter) {
      if (dragId === targetId) return false;
      var drag = this.getTask(dragId);
      if (!drag) return false;

      var rest = this.state.tasks.filter(function (t) { return t.id !== dragId; })
                                 .sort(function (a, b) { return a.order - b.order; });

      var idx = -1;
      for (var i = 0; i < rest.length; i++) { if (rest[i].id === targetId) { idx = i; break; } }
      if (idx < 0) return false;

      // 重排会改动所有任务的 order，因此快照全量（任务数量在个人工具量级下可忽略）
      this.pushUndo('调整顺序：' + drag.title);

      var insertAt = placeAfter ? idx + 1 : idx;
      var prev = insertAt > 0 ? rest[insertAt - 1].order : null;
      var next = insertAt < rest.length ? rest[insertAt].order : null;
      var newOrder;

      if (prev === null && next === null) newOrder = 1;
      else if (prev === null) newOrder = next - 1;
      else if (next === null) newOrder = prev + 1;
      else newOrder = (prev + next) / 2;

      drag.order = newOrder;
      drag.updatedAt = new Date().toISOString();
      this.state.tasks = rest;
      this.state.tasks.push(drag);
      this.normalizeOrdersIfNeeded();
      this.persist();
      this.notify();
      return true;
    },

    /** 上下移动（键盘 Alt+↑/↓，FR-09 的无障碍替代） */
    moveByOffset: function (id, offset) {
      var ordered = this.getOrdered();
      var idx = -1;
      for (var i = 0; i < ordered.length; i++) { if (ordered[i].id === id) { idx = i; break; } }
      if (idx < 0) return false;
      var tgt = idx + offset;
      if (tgt < 0 || tgt >= ordered.length) return false;
      return this.moveTask(id, ordered[tgt].id, offset > 0);
    },

    /** 权重过密或过大时重排为 1,2,3…，防止浮点精度退化 */
    normalizeOrdersIfNeeded: function () {
      var ordered = this.getOrdered();
      var need = false;
      for (var i = 0; i < ordered.length; i++) {
        if (Math.abs(ordered[i].order) > ORDER_COMPACT_AT) { need = true; break; }
        if (i > 0 && Math.abs(ordered[i].order - ordered[i - 1].order) < ORDER_MIN_GAP) { need = true; break; }
      }
      if (!need) return false;
      for (var j = 0; j < ordered.length; j++) ordered[j].order = j + 1;
      return true;
    },

    /* ------------------------------------------------------------ filtering */

    /** 当前时间基准。抽出成函数便于测试注入固定时间 */
    now: function () { return new Date(); },

    matchFilter: function (task, filter, now) {
      now = now || this.now();
      if (filter === 'done') return task.status === 'done';
      if (task.status === 'done') return false;

      if (filter === 'all') return true;

      if (filter === 'today') {
        if (!task.dueAt) return false;
        var d = new Date(task.dueAt);
        if (isSameDay(d, now)) return true;
        return d.getTime() < now.getTime();       // 逾期的也算今天要处理的
      }

      if (filter === 'week') {
        if (!task.dueAt) return false;
        var due = new Date(task.dueAt);
        if (due.getTime() < now.getTime()) return true;             // 逾期
        var wr = weekRange(now, this.state.settings.weekStartsOn);
        return due.getTime() >= wr.start.getTime() && due.getTime() < wr.end.getTime();
      }

      return true;
    },

    matchQuery: function (task, query) {
      if (!query) return true;
      var q = query.toLowerCase();
      if (task.title.toLowerCase().indexOf(q) >= 0) return true;
      if (task.note && task.note.toLowerCase().indexOf(q) >= 0) return true;
      for (var i = 0; i < task.tags.length; i++) {
        if (task.tags[i].toLowerCase().indexOf(q) >= 0) return true;
      }
      return false;
    },

    /**
     * 当前视图下的可见任务，按 FR-22 排序：
     * 未完成在前 → 逾期靠前 → 优先级高 → 截止时间近 → 手动顺序
     */
    visibleTasks: function () {
      var self = this;
      var now = this.now();
      var prioRank = { high: 0, medium: 1, low: 2 };

      return this.state.tasks
        .filter(function (t) {
          return self.matchFilter(t, self.filter, now) && self.matchQuery(t, self.query);
        })
        .sort(function (a, b) {
          if (a.status !== b.status) return a.status === 'done' ? 1 : -1;

          if (self.filter !== 'done') {
            var ao = isOverdue(a, now) ? 0 : 1;
            var bo = isOverdue(b, now) ? 0 : 1;
            if (ao !== bo) return ao - bo;
          }

          var pa = prioRank[a.priority], pb = prioRank[b.priority];
          if (pa !== pb) return pa - pb;

          var ad = a.dueAt ? new Date(a.dueAt).getTime() : Infinity;
          var bd = b.dueAt ? new Date(b.dueAt).getTime() : Infinity;
          if (ad !== bd) return ad - bd;

          if (a.status === 'done') {
            var ac = a.completedAt ? new Date(a.completedAt).getTime() : 0;
            var bc = b.completedAt ? new Date(b.completedAt).getTime() : 0;
            if (ac !== bc) return bc - ac;   // 最近完成的排前面（S4）
          }

          return a.order - b.order;
        });
    },

    counts: function () {
      var now = this.now();
      var c = { today: 0, week: 0, all: 0, done: 0, overdue: 0 };
      for (var i = 0; i < this.state.tasks.length; i++) {
        var t = this.state.tasks[i];
        if (this.matchFilter(t, 'today', now)) c.today++;
        if (this.matchFilter(t, 'week', now)) c.week++;
        if (this.matchFilter(t, 'all', now)) c.all++;
        if (this.matchFilter(t, 'done', now)) c.done++;
        if (t.status !== 'done' && isOverdue(t, now)) c.overdue++;
      }
      return c;
    },

    setFilter: function (f) {
      if (['today', 'week', 'all', 'done'].indexOf(f) < 0) return;
      this.filter = f;
      this.notify();
    },

    setQuery: function (q) {
      this.query = (q || '').trim();
      this.notify();
    },

    /* ------------------------------------------------------------ selection */

    /** 当前视图中被选中的任务 id 集合 */
    selectedVisible: function () {
      var visible = this.visibleTasks();
      var sel = this.selectedIds;
      return visible.filter(function (t) { return sel.has(t.id); });
    },

    /** 选中但不在当前视图中的数量（FR-95 提示用） */
    selectedHiddenCount: function () {
      var visibleIds = Object.create(null);
      this.visibleTasks().forEach(function (t) { visibleIds[t.id] = true; });
      var n = 0;
      this.selectedIds.forEach(function (id) { if (!visibleIds[id]) n++; });
      return n;
    },

    isSelected: function (id) { return this.selectedIds.has(id); },

    clearSelection: function () {
      if (!this.selectedIds.size) return;
      this.selectedIds.clear();
      this.anchorId = null;
      this.focusId = null;
      this.notify();
    },

    setSelection: function (ids) {
      this.selectedIds = new Set(ids || []);
      this.notify();
    },

    /**
     * 点击选择。modifiers: { ctrl, shift, meta }
     * Shift 使用 anchorId 作为锚点；Ctrl 切换单项并重设锚点。
     */
    selectClick: function (id, modifiers) {
      modifiers = modifiers || {};
      var multi = modifiers.ctrl || modifiers.meta;

      if (!this.state.settings.multiSelectEnabled) {
        this.selectedIds = new Set([id]);
        this.anchorId = id;
        this.focusId = id;
        this.notify();
        return;
      }

      if (modifiers.shift && this.anchorId) {
        var visible = this.visibleTasks().map(function (t) { return t.id; });
        var a = visible.indexOf(this.anchorId);
        var b = visible.indexOf(id);
        if (a >= 0 && b >= 0) {
          var lo = Math.min(a, b), hi = Math.max(a, b);
          var range = visible.slice(lo, hi + 1);
          if (!multi) this.selectedIds = new Set(range);
          else range.forEach(function (x) { this.selectedIds.add(x); }, this);
          this.focusId = id;
          this.notify();
          return;
        }
        // 锚点不在当前视图，退化为普通单选
      }

      if (multi) {
        if (this.selectedIds.has(id)) this.selectedIds.delete(id);
        else this.selectedIds.add(id);
        this.anchorId = id;
      } else {
        // 再点已选中项：若无其他选中项则取消选择，否则收敛为单选该项
        if (this.selectedIds.size === 1 && this.selectedIds.has(id)) {
          this.selectedIds.clear();
          this.anchorId = null;
        } else {
          this.selectedIds = new Set([id]);
          this.anchorId = id;
        }
      }
      this.focusId = id;
      this.notify();
    },

    selectAllVisible: function () {
      var ids = this.visibleTasks().map(function (t) { return t.id; });
      var allSelected = ids.length > 0 && ids.every(function (id) { return this.selectedIds.has(id); }, this);
      if (allSelected) {
        this.selectedIds.clear();
        this.anchorId = null;
      } else {
        this.selectedIds = new Set(ids);
        if (ids.length) this.anchorId = ids[0];
      }
      this.notify();
    },

    /** 缩到只保留当前视图内的选中项（FR-95 的"仅选择当前视图"） */
    narrowSelectionToVisible: function () {
      var visible = Object.create(null);
      this.visibleTasks().forEach(function (t) { visible[t.id] = true; });
      var next = new Set();
      this.selectedIds.forEach(function (id) { if (visible[id]) next.add(id); });
      this.selectedIds = next;
      this.notify();
    },

    /**
     * 直接设置选择集合（框选用）。接受数组或 Set。
     * 集合未变化时不触发通知，避免框选过程中每帧重绘整个列表。
     */
    selectIds: function (ids, skipNotify) {
      var next = (ids instanceof Set) ? ids : new Set(ids || []);
      var same = next.size === this.selectedIds.size;
      if (same) {
        var self = this;
        same = true;
        next.forEach(function (id) { if (!self.selectedIds.has(id)) same = false; });
      }
      if (same) return false;

      this.selectedIds = next;
      if (next.size) this.anchorId = next.values().next().value;
      if (!skipNotify) this.notify();
      return true;
    },

    /* ------------------------------------------------------------- settings */

    getSettings: function () { return this.state.settings; },

    setSettings: function (patch, immediate) {
      var s = this.state.settings;
      for (var k in patch) {
        if (!Object.prototype.hasOwnProperty.call(patch, k)) continue;
        if (k === 'floatingGeometry' && patch[k] && typeof patch[k] === 'object') {
          s.floatingGeometry = Object.assign({}, s.floatingGeometry, patch[k]);
        } else {
          s[k] = patch[k];
        }
      }
      this.persist(immediate);
      this.notify();
      return s;
    },

    /* ------------------------------------------------------------ import */

    /** merge=true 时按 id 去重合并，否则整表覆盖 */
    importState: function (incoming, merge) {
      if (!merge) {
        this.state.tasks = incoming.tasks;
        this.persist(true);
        this.selectedIds.clear();
        this.notify();
        return incoming.tasks.length;
      }

      var existing = Object.create(null);
      for (var i = 0; i < this.state.tasks.length; i++) existing[this.state.tasks[i].id] = i;

      var added = 0;
      for (var j = 0; j < incoming.tasks.length; j++) {
        var t = incoming.tasks[j];
        if (existing[t.id] !== undefined) {
          this.state.tasks[existing[t.id]] = t;   // 同 id 视为同一条，以导入的为准
        } else {
          this.state.tasks.push(t);
          added++;
        }
      }
      this.persist(true);
      this.notify();
      return added;
    },

    /** 供提醒模块标记"已提醒"，避免重复通知（FR-30） */
    markReminded: function (id) {
      var t = this.getTask(id);
      if (!t || t.remindedAt) return false;
      t.remindedAt = new Date().toISOString();
      this.persist();
      return true;
    }
  };

  function isOverdue(task, now) {
    if (task.status === 'done') return false;
    if (!task.dueAt) return false;
    return new Date(task.dueAt).getTime() < (now || new Date()).getTime();
  }

  App.Store = Store;
  App.dateUtil = {
    startOfDay: startOfDay,
    addDays: addDays,
    isSameDay: isSameDay,
    weekRange: weekRange,
    isOverdue: isOverdue
  };
  App.UNDO_WINDOW_MS = UNDO_WINDOW_MS;
})(window);
