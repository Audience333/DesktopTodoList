/* =============================================================================
 * app.js — 表现层与交互编排
 * 职责：渲染任务列表、快速录入、行内编辑、详情面板、多选与框选、拖拽排序、
 *       快捷键、撤销提示、设置面板、窗口控制按钮
 * 约定：不直接读写 localStorage；所有状态变更调用 store 方法
 * ========================================================================== */
(function (global) {
  'use strict';

  var App = global.TodoApp;
  var S = App.storage;

  var PRIO_TEXT = { high: '高', medium: '中', low: '低' };
  var FILTER_EMPTY = {
    today: ['今天没有安排', '给自己留点余地，或者把想到的事先记下来。'],
    week:  ['本周没有安排', '计划一下这周要推进的事情吧。'],
    all:   ['暂无任务', '在上方输入框写下第一件事，按回车即可添加。'],
    done:  ['还没有完成的任务', '完成任务后会出现在这里。']
  };

  /* ------------------------------------------------------------ DOM 简写 */
  function $(id) { return document.getElementById(id); }

  var store, host;
  var els = {};
  var undoTimer = null;
  var flashTimer = null;
  var editingId = null;
  var drawerTaskId = null;

  /* =========================================================== 启动流程 */
  function boot() {
    cacheEls();
    bindStaticEvents();

    store = new App.Store().init();
    host = new App.Host().init();

    if (App.backups && store.loadStatus !== 'reset') {
      var backupResult = App.backups.defaultService.createDaily(store.state);
      if (!backupResult.ok && global.console) console.warn(backupResult.error);
    }

    applyTheme();
    store.subscribe(render);
    host.onUpdate(onHostUpdate);

    // 数据异常必须让用户看见，而不是默默修复（NFR-05）
    reportLoadStatus();

    render();
    applyWindowPreferences();
    startReminderLoop();

    // 关闭前强制落盘，避免防抖窗口内丢数据
    global.addEventListener('beforeunload', function () { S.flush(store.state); });
    document.addEventListener('visibilitychange', function () {
      if (document.visibilityState === 'hidden') S.flush(store.state);
    });

    if (!host.available) {
      els.degradeBanner.hidden = false;
      els.statusMeta.textContent = '浏览器模式';
    } else {
      document.body.classList.add('host-mode');
    }
    els.taskInput.focus();
  }

  function cacheEls() {
    [
      'titlebar', 'dragRegion', 'overdueBadge',
      'btnSearch', 'btnLayerTop', 'btnLayerBottom', 'btnSelectable', 'btnSettings',
      'btnMinimize', 'btnClose',
      'passthroughBanner', 'pbHotkey', 'degradeBanner', 'degradeClose',
      'btnQuickAdd', 'taskInput', 'btnDetailAdd', 'draftPanel', 'draftDue', 'draftPriority',
      'draftTags', 'draftRemind', 'draftCancel', 'draftConfirm',
      'searchbar', 'searchInput', 'searchClear',
      'bulkbar', 'bulkCount', 'bulkHiddenHint', 'bulkDone', 'bulkUndone', 'bulkPrio',
      'bulkDelete', 'bulkClear',
      'listWrap', 'listHead', 'selectAllBox', 'listHeadLabel', 'taskList', 'emptyState',
      'emptyTitle', 'emptyDesc', 'rubberBand',
      'statusText', 'btnClearCompleted', 'statusMeta',
      'resizeGrip', 'toast', 'toastText', 'toastAction', 'toastProgress',
      'flash', 'drawer', 'drawerClose', 'drawerOverlay',
      'dtTitle', 'dtNote', 'dtDue', 'dtPriority', 'dtTags', 'dtRemind', 'dtMeta',
      'dtDelete', 'dtCancel', 'dtSave',
      'settingsDrawer', 'settingsClose', 'stTheme', 'stDefaultFilter', 'stWeekStart',
      'stFloating', 'stLayer', 'layerHint', 'stSelectable', 'stHotkeySelectable',
      'stAutoStart', 'stStartMinimized', 'stHotkey',
      'stMultiSelect', 'stRubberBand', 'stKeepSelection',
      'stExport', 'stImport', 'stImportFile', 'stRestoreBackup', 'stReset', 'stDataHint',
      'aboutText', 'confirmBox', 'confirmTitle', 'confirmMsg', 'confirmCancel', 'confirmMerge', 'confirmOk'
    ].forEach(function (id) { els[id] = $(id); });
  }

  /* ============================================================ 渲染入口 */
  function render() {
    var tasks = store.visibleTasks();

    renderTabs();
    renderList(tasks);
    renderBulkBar();
    renderStatus();
    renderWindowButtons();
    renderUndoToast();
    applyTheme();

    // 把待办数量同步给宿主，用于托盘提示与角标
    if (host && host.available) host.reportCounts(store.counts());
  }

  function renderTabs() {
    var counts = store.counts();
    var tabs = document.querySelectorAll('.view-tab');
    for (var i = 0; i < tabs.length; i++) {
      var f = tabs[i].getAttribute('data-filter');
      var on = f === store.filter;
      tabs[i].setAttribute('aria-selected', on ? 'true' : 'false');
      var badge = tabs[i].querySelector('.tab-count');
      if (badge) {
        badge.textContent = counts[f] || 0;
        badge.setAttribute('data-zero', (counts[f] || 0) === 0 ? '1' : '0');
      }
    }
    els.overdueBadge.hidden = counts.overdue === 0;
    els.overdueBadge.textContent = counts.overdue > 99 ? '99+' : String(counts.overdue);
    els.btnClearCompleted.hidden = !(store.filter === 'done' && counts.done > 0);
  }

  /* ------------------------------------------------------------ 列表渲染 */

  function renderList(tasks) {
    // 重绘会丢弃正在编辑的 input：必须同步清掉 editingId，否则后续编辑被永久阻塞
    editingId = null;
    els.taskList.textContent = '';

    if (!tasks.length) {
      els.emptyState.hidden = false;
      els.listHead.hidden = true;
      var msg = FILTER_EMPTY[store.filter] || FILTER_EMPTY.all;
      if (store.query) {
        els.emptyTitle.textContent = '没有匹配的任务';
        els.emptyDesc.textContent = '换个关键词试试，或清空搜索框。';
      } else {
        els.emptyTitle.textContent = msg[0];
        els.emptyDesc.textContent = msg[1];
      }
      els.rubberBand.hidden = true;
      return;
    }

    els.emptyState.hidden = true;
    els.listHead.hidden = false;
    els.listHeadLabel.textContent = '共 ' + tasks.length + ' 项';

    var frag = document.createDocumentFragment();
    for (var i = 0; i < tasks.length; i++) {
      frag.appendChild(buildTaskEl(tasks[i]));
    }
    els.taskList.appendChild(frag);

    syncSelectAllBox(tasks);
  }

  function buildTaskEl(task) {
    var now = store.now();
    var overdue = App.dateUtil.isOverdue(task, now);

    var li = document.createElement('li');
    li.className = 'task';
    li.setAttribute('role', 'option');
    li.setAttribute('data-id', task.id);
    li.setAttribute('aria-selected', store.isSelected(task.id) ? 'true' : 'false');
    li.setAttribute('tabindex', '-1');
    if (task.status === 'done') li.classList.add('is-done');
    if (overdue) li.classList.add('is-overdue');
    if (store.isSelected(task.id)) li.classList.add('is-selected');
    if (store.focusId === task.id) li.classList.add('is-focused');
    var draggable = !store.query;   // 搜索状态下排序无意义，禁用拖拽
    if (draggable) li.setAttribute('draggable', 'true');

    /* 拖拽手柄 */
    var handle = document.createElement('span');
    handle.className = 'task__handle';
    handle.textContent = '⠿';
    handle.title = '拖动排序';
    handle.setAttribute('aria-hidden', 'true');
    li.appendChild(handle);

    /* 完成复选框（FR-94：独立于选中状态） */
    var check = document.createElement('label');
    check.className = 'task__check';
    check.title = task.status === 'done' ? '标记为待办' : '标记为完成';
    var cb = document.createElement('input');
    cb.type = 'checkbox';
    cb.checked = task.status === 'done';
    cb.setAttribute('data-action', 'toggle-done');
    cb.setAttribute('aria-label', '完成 ' + task.title);
    var box = document.createElement('span');
    box.className = 'task__box';
    check.appendChild(cb);
    check.appendChild(box);
    li.appendChild(check);

    /* 主体 */
    var body = document.createElement('div');
    body.className = 'task__body';

    var row1 = document.createElement('div');
    row1.className = 'task__row1';
    var title = document.createElement('span');
    title.className = 'task__title';
    title.setAttribute('data-action', 'edit-title');
    title.title = '双击编辑标题';
    if (store.query) renderHighlighted(title, task.title, store.query);
    else title.textContent = task.title;
    row1.appendChild(title);
    body.appendChild(row1);

    if (task.note) {
      var note = document.createElement('div');
      note.className = 'task__note';
      note.textContent = task.note;
      note.title = task.note;
      body.appendChild(note);
    }

    var row2 = document.createElement('div');
    row2.className = 'task__row2';

    var prio = document.createElement('span');
    prio.className = 'prio-dot prio-' + task.priority;
    prio.textContent = PRIO_TEXT[task.priority];
    row2.appendChild(prio);

    if (task.dueAt) {
      var due = document.createElement('span');
      due.className = 'task__due';
      due.textContent = formatDue(task.dueAt, now, overdue);
      row2.appendChild(due);
    }

    for (var t = 0; t < task.tags.length; t++) {
      var tag = document.createElement('span');
      tag.className = 'tag';
      tag.textContent = task.tags[t];
      row2.appendChild(tag);
    }

    if (task.status === 'done' && task.completedAt) {
      var done = document.createElement('span');
      done.textContent = '完成于 ' + formatShort(task.completedAt);
      row2.appendChild(done);
    }

    body.appendChild(row2);
    li.appendChild(body);

    /* 删除按钮 */
    var del = document.createElement('button');
    del.type = 'button';
    del.className = 'task__delete';
    del.setAttribute('data-action', 'delete');
    del.setAttribute('aria-label', '删除 ' + task.title);
    del.title = '删除';
    del.textContent = '🗑';
    li.appendChild(del);

    return li;
  }

  /** 用 <mark> 高亮搜索命中，避免 innerHTML 注入 */
  function renderHighlighted(container, text, query) {
    var lower = text.toLowerCase();
    var q = query.toLowerCase();
    var from = 0, idx;
    while ((idx = lower.indexOf(q, from)) >= 0 && q) {
      if (idx > from) container.appendChild(document.createTextNode(text.slice(from, idx)));
      var m = document.createElement('mark');
      m.textContent = text.slice(idx, idx + q.length);
      container.appendChild(m);
      from = idx + q.length;
    }
    if (from < text.length) container.appendChild(document.createTextNode(text.slice(from)));
  }

  /* --------------------------------------------------------- 时间格式化 */

  function pad(n) { return n < 10 ? '0' + n : String(n); }

  function formatShort(iso) {
    var d = new Date(iso);
    return (d.getMonth() + 1) + '月' + d.getDate() + '日 ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
  }

  function formatDue(iso, now, overdue) {
    var d = new Date(iso);
    var hm = pad(d.getHours()) + ':' + pad(d.getMinutes());
    var day = App.dateUtil.startOfDay(d);
    var today = App.dateUtil.startOfDay(now);
    var diffDays = Math.round((day - today) / 86400000);
    var prefix;

    if (diffDays === 0) prefix = '今天';
    else if (diffDays === 1) prefix = '明天';
    else if (diffDays === -1) prefix = '昨天';
    else prefix = (d.getMonth() + 1) + '/' + d.getDate();

    var text = prefix + ' ' + hm;
    if (overdue) text = '已逾期 · ' + text;
    return text;
  }

  /* ------------------------------------------------------------ 批量工具栏 */

  function renderBulkBar() {
    var n = store.selectedIds.size;
    if (!n) {
      els.bulkbar.hidden = true;
      return;
    }
    els.bulkbar.hidden = false;
    els.bulkCount.textContent = '已选 ' + n + ' 项';

    var hiddenCount = store.selectedHiddenCount();
    if (hiddenCount > 0) {
      els.bulkHiddenHint.hidden = false;
      els.bulkHiddenHint.textContent = '（其中 ' + hiddenCount + ' 项不在当前视图）';
    } else {
      els.bulkHiddenHint.hidden = true;
    }
  }

  function syncSelectAllBox(tasks) {
    var ids = tasks.map(function (t) { return t.id; });
    var selCount = 0;
    for (var i = 0; i < ids.length; i++) if (store.selectedIds.has(ids[i])) selCount++;

    els.selectAllBox.checked = ids.length > 0 && selCount === ids.length;
    els.selectAllBox.indeterminate = selCount > 0 && selCount < ids.length;
  }

  function renderStatus() {
    var counts = store.counts();
    var text = counts.all + ' 项待办';
    if (counts.overdue) text += ' · ' + counts.overdue + ' 项逾期';
    els.statusText.textContent = text;

    if (host && host.available) {
      var layer = store.getSettings().windowLayer;
      var sel = store.getSettings().selectable;
      var bits = ['桌面版'];
      bits.push(layer === 'top' ? '置顶' : layer === 'bottom' ? '置底' : '普通');
      bits.push(sel ? '可选中' : '不可选中');
      els.statusMeta.textContent = bits.join(' · ');
    } else {
      els.statusMeta.textContent = '浏览器模式';
    }
  }

  function renderWindowButtons() {
    var s = store.getSettings();
    els.btnLayerTop.setAttribute('aria-pressed', s.windowLayer === 'top' ? 'true' : 'false');
    els.btnLayerBottom.setAttribute('aria-pressed', s.windowLayer === 'bottom' ? 'true' : 'false');
    els.btnSelectable.setAttribute('aria-pressed', s.selectable ? 'true' : 'false');
    els.btnSelectable.setAttribute('aria-label', s.selectable ? '窗口可选中' : '窗口不可选中（鼠标穿透）');
    els.passthroughBanner.hidden = s.selectable;
    els.pbHotkey.textContent = s.hotkeySelectable;

    document.body.classList.toggle('mode-floating', s.windowMode === 'floating');
    els.resizeGrip.hidden = !(host && host.available && s.windowMode === 'floating');
  }

  /* ============================================================ 主题 */
  function applyTheme() {
    var theme = store.getSettings().theme;
    var resolved = theme;
    if (theme === 'system') {
      resolved = global.matchMedia && global.matchMedia('(prefers-color-scheme: dark)').matches
        ? 'dark' : 'light';
    }
    document.documentElement.setAttribute('data-theme', resolved);
  }

  function reportLoadStatus() {
    var st = store.loadStatus;
    if (st === 'ok' || st === 'fresh') return;
    if (st === 'repaired') {
      flash('数据格式已自动修复' + (store.loadDropped ? '，丢弃 ' + store.loadDropped + ' 条无效任务' : ''),
            'error', 6000);
    } else if (st === 'restored') {
      flash('主数据损坏，已从自动备份恢复', 'success', 8000);
    } else if (st === 'reset') {
      flash('数据与备份均损坏，已重置为空。如有导出文件可手动导入。', 'error', 10000);
    }
  }

  /* ============================================================ 宿主联动 */
  function applyWindowPreferences() {
    if (!host.available) {
      // 浏览器模式：把依赖宿主的能力在设置里标灰，避免用户以为已生效
      els.stAutoStart.disabled = true;
      els.stFloating.disabled = true;
      els.stSelectable.disabled = true;
      els.stLayer.querySelectorAll('button').forEach(function (b) { b.disabled = true; });
      els.layerHint.textContent = '浏览器模式不支持窗口层级与鼠标穿透';
      return;
    }
    // 把全部窗口偏好交给宿主：窗口在页面加载前就已存在，必须由宿主来应用
    host.hello(store.getSettings());
  }

  function onHostUpdate() {
    if (!host.available) {
      els.degradeBanner.hidden = false;
      els.resizeGrip.hidden = true;
      els.btnSelectable.setAttribute('aria-pressed', 'true');
      els.passthroughBanner.hidden = true;
      renderStatus();
      flash('宿主连接已断开：' + (host.degradedReason || '未知原因') +
            '。窗口相关功能已停用，任务功能不受影响。', 'error', 8000);
      return;
    }

    // 宿主侧状态变更（例如用户从托盘菜单改了层级或穿透）需要回灌到页面 UI
    var inbox = host.takeInbox();
    if (inbox) {
      var patch = {};
      if (inbox.layer !== undefined) patch.windowLayer = inbox.layer;
      if (inbox.selectable !== undefined) patch.selectable = inbox.selectable;
      if (inbox.mode !== undefined) patch.windowMode = inbox.mode;
      if (inbox.autoStart !== undefined) patch.autoStart = inbox.autoStart;
      if (Object.keys(patch).length) store.setSettings(patch, true);

      syncLayerButtons(store.getSettings().windowLayer);
      els.stSelectable.checked = store.getSettings().selectable;
      renderWindowButtons();
    }

    // 宿主报告能力缺失时明确提示，不静默失效
    var missing = [];
    if (!host.caps.topmost && !host.caps.bottom) missing.push('窗口层级');
    if (!host.caps.clickThrough) missing.push('鼠标穿透');
    if (missing.length && !host._warnedCaps) {
      host._warnedCaps = true;
      flash('宿主不支持：' + missing.join('、'), 'error', 6000);
    }
    if (host.lastError && host.lastError !== host._lastErrShown) {
      host._lastErrShown = host.lastError;
      flash('窗口操作失败：' + host.lastError, 'error', 5000);
    }
    renderStatus();
  }

  /* ============================================================ 事件绑定 */
  function bindStaticEvents() {

    /* ---- 快速录入 ---- */
    els.taskInput.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' && !e.isComposing) {
        e.preventDefault();
        commitQuickAdd();
      } else if (e.key === 'Escape') {
        els.taskInput.value = '';
        hideDraft();
      }
    });
    els.btnQuickAdd.addEventListener('click', function () { commitQuickAdd(); });
    els.btnDetailAdd.addEventListener('click', function () {
      var show = els.draftPanel.hidden;
      els.draftPanel.hidden = !show;
      if (show) els.draftDue.focus();
    });
    els.draftCancel.addEventListener('click', hideDraft);
    els.draftConfirm.addEventListener('click', function () { commitQuickAdd(); });
    els.draftPanel.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' && !e.isComposing && e.target.tagName !== 'TEXTAREA') {
        e.preventDefault();
        commitQuickAdd();
      } else if (e.key === 'Escape') {
        hideDraft();
      }
    });

    /* ---- 视图切换 ---- */
    document.querySelectorAll('.view-tab').forEach(function (tab) {
      tab.addEventListener('click', function () {
        store.setFilter(tab.getAttribute('data-filter'));
        afterFilterChange();
      });
    });

    /* ---- 搜索 ---- */
    els.btnSearch.addEventListener('click', toggleSearch);
    els.searchInput.addEventListener('input', function () {
      store.setQuery(els.searchInput.value);
    });
    els.searchInput.addEventListener('keydown', function (e) {
      if (e.key === 'Escape') { toggleSearch(false); }
    });
    els.searchClear.addEventListener('click', function () {
      els.searchInput.value = '';
      store.setQuery('');
      els.searchInput.focus();
    });

    /* ---- 列表：事件委托 ---- */
    els.taskList.addEventListener('click', onListClick);
    els.taskList.addEventListener('dblclick', onListDblClick);
    els.taskList.addEventListener('keydown', onListKeydown);

    /* ---- 拖拽排序 ---- */
    els.taskList.addEventListener('dragstart', onDragStart);
    els.taskList.addEventListener('dragover', onDragOver);
    els.taskList.addEventListener('dragleave', onDragLeave);
    els.taskList.addEventListener('drop', onDrop);
    els.taskList.addEventListener('dragend', onDragEnd);

    /* ---- 框选 ---- */
    els.listWrap.addEventListener('mousedown', onListMouseDown);

    /* ---- 全选 ---- */
    els.selectAllBox.addEventListener('change', function () {
      store.selectAllVisible();
    });

    /* ---- 批量操作 ---- */
    els.bulkDone.addEventListener('click', function () { batchSetDone(true); });
    els.bulkUndone.addEventListener('click', function () { batchSetDone(false); });
    els.bulkPrio.addEventListener('click', cycleBatchPriority);
    els.bulkDelete.addEventListener('click', batchDelete);
    els.bulkClear.addEventListener('click', function () { store.clearSelection(); });
    els.btnClearCompleted.addEventListener('click', function () {
      var n = store.clearCompleted();
      if (n) flash('已清理 ' + n + ' 项已完成任务', 'success');
    });

    /* ---- 撤销 ---- */
    els.toastAction.addEventListener('click', doUndo);

    /* ---- 窗口按钮 ---- */
    els.btnLayerTop.addEventListener('click', function () {
      var next = store.getSettings().windowLayer === 'top' ? 'normal' : 'top';
      applyLayer(next);
    });
    els.btnLayerBottom.addEventListener('click', function () {
      var next = store.getSettings().windowLayer === 'bottom' ? 'normal' : 'bottom';
      applyLayer(next);
    });
    els.btnSelectable.addEventListener('click', function () {
      applySelectable(!store.getSettings().selectable, true);
    });
    els.btnMinimize.addEventListener('click', function () {
      if (host.available) host.minimize();
    });
    els.btnClose.addEventListener('click', onCloseClick);
    els.btnSettings.addEventListener('click', openSettings);
    els.degradeClose.addEventListener('click', function () { els.degradeBanner.hidden = true; });

    /* ---- 详情面板 ---- */
    els.drawerClose.addEventListener('click', closeDrawer);
    els.drawerOverlay.addEventListener('click', closeDrawer);
    els.dtCancel.addEventListener('click', closeDrawer);
    els.dtSave.addEventListener('click', saveDrawer);
    els.dtDelete.addEventListener('click', deleteFromDrawer);

    /* ---- 设置面板 ---- */
    els.settingsClose.addEventListener('click', closeSettings);
    els.stTheme.addEventListener('change', function () {
      store.setSettings({ theme: els.stTheme.value }, true); applyTheme();
    });
    els.stDefaultFilter.addEventListener('change', function () {
      store.setSettings({ defaultFilter: els.stDefaultFilter.value }, true);
    });
    els.stWeekStart.addEventListener('change', function () {
      store.setSettings({ weekStartsOn: parseInt(els.stWeekStart.value, 10) }, true);
    });
    els.stFloating.addEventListener('change', function () {
      var mode = els.stFloating.checked ? 'floating' : 'normal';
      store.setSettings({ windowMode: mode }, true);
      if (host.available) host.setMode(mode);
      renderWindowButtons();
    });
    els.stSelectable.addEventListener('change', function () {
      applySelectable(els.stSelectable.checked, true);
    });
    els.stLayer.addEventListener('click', function (e) {
      var btn = e.target.closest('button[data-layer]');
      if (btn) applyLayer(btn.getAttribute('data-layer'));
    });
    els.stAutoStart.addEventListener('change', function () {
      var want = els.stAutoStart.checked;
      if (!host.available) { els.stAutoStart.checked = false; return; }
      store.setSettings({ autoStart: want }, true);
      host.setAutoStart(want);
      flash(want ? '已开启开机自启（写入 HKCU Run 项）' : '已关闭开机自启');
    });
    els.stStartMinimized.addEventListener('change', function () {
      var v = els.stStartMinimized.checked;
      store.setSettings({ startMinimized: v }, true);
      if (host.available) host.setStartMinimized(v);
    });
    els.stMultiSelect.addEventListener('change', function () {
      store.setSettings({ multiSelectEnabled: els.stMultiSelect.checked }, true);
      if (!els.stMultiSelect.checked) store.clearSelection();
    });
    els.stRubberBand.addEventListener('change', function () {
      store.setSettings({ rubberBandSelect: els.stRubberBand.checked }, true);
    });
    els.stKeepSelection.addEventListener('change', function () {
      store.setSettings({ keepSelectionAcrossViews: els.stKeepSelection.checked }, true);
    });
    els.stExport.addEventListener('click', exportData);
    els.stImport.addEventListener('click', function () { els.stImportFile.click(); });
    els.stImportFile.addEventListener('change', importData);
    els.stRestoreBackup.addEventListener('click', restoreBackup);
    els.stReset.addEventListener('click', resetAll);

    /* ---- 确认框 ---- */
    els.confirmCancel.addEventListener('click', function () { hideConfirm(); });
    els.confirmBox.addEventListener('click', function (e) {
      if (e.target === els.confirmBox) hideConfirm();
    });

    /* ---- 全局快捷键 ---- */
    document.addEventListener('keydown', onGlobalKeydown);
    document.addEventListener('dragover', function (e) { e.preventDefault(); });
    document.addEventListener('drop', function (e) {
      if (!e.dataTransfer || !e.dataTransfer.files || !e.dataTransfer.files.length) return;
      e.preventDefault();
      if (e.dataTransfer.files.length !== 1 || !/\.json$/i.test(e.dataTransfer.files[0].name)) {
        flash('请拖入单个 JSON 文件', 'error');
        return;
      }
      processImportFile(e.dataTransfer.files[0]);
    });

    /* ---- 窗口拖拽 / 缩放（仅宿主浮窗模式） ---- */
    bindWindowDrag();
    bindWindowResize();

    /* ---- 系统主题变化 ---- */
    if (global.matchMedia) {
      var mq = global.matchMedia('(prefers-color-scheme: dark)');
      var onSys = function () { if (store.getSettings().theme === 'system') applyTheme(); };
      if (mq.addEventListener) mq.addEventListener('change', onSys);
      else if (mq.addListener) mq.addListener(onSys);
    }
  }

  /* ============================================================ 快速录入 */

  function commitQuickAdd() {
    var title = els.taskInput.value.trim();
    if (!title) {
      shake(els.taskInput);
      flash('请先输入任务内容', 'error', 2200);
      return;
    }
    var tags = els.draftTags.value.split(/[,，、]/).map(function (s) { return s.trim(); })
                            .filter(Boolean);
    var task = store.addTask({
      title: title,
      dueAt: els.draftDue.value ? new Date(els.draftDue.value).toISOString() : null,
      priority: els.draftPriority.value,
      tags: tags,
      remind: els.draftRemind.checked
    });
    if (!task) {
      flash('任务创建失败，请检查标题', 'error');
      return;
    }
    // FR-02：清空并保持焦点，支持连续录入
    els.taskInput.value = '';
    els.draftTags.value = '';
    els.taskInput.focus();
  }

  function hideDraft() {
    els.draftPanel.hidden = true;
    els.draftDue.value = '';
    els.draftTags.value = '';
    els.draftPriority.value = 'medium';
    els.draftRemind.checked = true;
  }

  /* ============================================================ 列表交互 */

  function taskIdFrom(target) {
    var li = target.closest ? target.closest('.task') : null;
    return li ? li.getAttribute('data-id') : null;
  }

  function onListClick(e) {
    var li = e.target.closest('.task');
    if (!li) {
      // 点击列表空白处清空选择（FR-81）
      if (e.target === els.taskList || e.target === els.listWrap) store.clearSelection();
      return;
    }
    var id = li.getAttribute('data-id');
    var action = e.target.getAttribute && e.target.getAttribute('data-action');

    // FR-94：复选框只切换完成状态，绝不改变选中状态
    if (action === 'toggle-done' || e.target.closest('.task__check')) {
      e.stopPropagation();
      var box = li.querySelector('.task__check input');
      store.toggleDone(id, box.checked);
      return;
    }

    if (action === 'delete') {
      e.stopPropagation();
      store.removeTask(id);
      return;
    }

    if (action === 'edit-title') {
      // 有修饰键时优先当作选择操作，避免误进编辑
      if (e.ctrlKey || e.metaKey || e.shiftKey) {
        store.selectClick(id, { ctrl: e.ctrlKey || e.metaKey, shift: e.shiftKey });
        return;
      }
      // 单击标题：选中（编辑需双击，避免与选择操作打架）
      store.selectClick(id, {});
      return;
    }

    store.selectClick(id, { ctrl: e.ctrlKey || e.metaKey, shift: e.shiftKey });
  }

  function onListDblClick(e) {
    var li = e.target.closest('.task');
    if (!li) return;
    var id = li.getAttribute('data-id');
    var action = e.target.getAttribute && e.target.getAttribute('data-action');

    if (action === 'edit-title' || e.target.classList.contains('task__title')) {
      beginInlineEdit(li, id);
      return;
    }
    if (!e.target.closest('.task__check') && !e.target.closest('.task__delete')) {
      openDrawer(id);
    }
  }

  function beginInlineEdit(li, id) {
    if (editingId) return;
    var task = store.getTask(id);
    if (!task) return;

    editingId = id;
    var titleEl = li.querySelector('.task__title');
    if (!titleEl) { editingId = null; return; }

    var input = document.createElement('input');
    input.type = 'text';
    input.className = 'task__edit';
    input.value = task.title;
    input.maxLength = S.MAX_TITLE;
    titleEl.replaceWith(input);
    input.focus();
    input.select();

    var finished = false;
    function finish(save) {
      if (finished) return;
      finished = true;
      editingId = null;
      var val = input.value.trim();
      if (save && val && val !== task.title) store.updateTask(id, { title: val });
      else render();
    }

    input.addEventListener('keydown', function (e) {
      e.stopPropagation();      // 避免触发全局/列表快捷键
      if (e.key === 'Enter') { e.preventDefault(); finish(true); }
      else if (e.key === 'Escape') { e.preventDefault(); finish(false); }
    });
    input.addEventListener('blur', function () { finish(true); });
  }

  /* ------------------------------------------------------------ 键盘操作 */

  function onListKeydown(e) {
    if (editingId) return;

    // Alt+↑/↓ 是"移动任务"而非"移动焦点"，必须先于导航判断（否则会被吞掉）
    if (e.altKey && (e.key === 'ArrowUp' || e.key === 'ArrowDown')) {
      e.preventDefault();
      var mid = store.focusId || (store.selectedIds.size ? selectedIdsArray()[0] : null);
      if (mid) store.moveByOffset(mid, e.key === 'ArrowDown' ? 1 : -1);
      return;
    }

    if (e.key === ' ' || e.key === 'Spacebar') {
      e.preventDefault();
      if (store.focusId) {
        store.selectClick(store.focusId, { ctrl: true });
      } else if (store.selectedIds.size) {
        store.toggleDone(store.selectedIds.values().next().value);
      }
      return;
    }

    if (e.key === 'ArrowDown' || e.key === 'ArrowUp' || e.key === 'Home' || e.key === 'End') {
      e.preventDefault();
      if (e.key === 'Home') moveFocusToEdge(-1, e.shiftKey);
      else if (e.key === 'End') moveFocusToEdge(1, e.shiftKey);
      else moveFocus(e.key === 'ArrowDown' ? 1 : -1, e.shiftKey);
      return;
    }

    if (e.key === 'Enter' && store.focusId) {
      e.preventDefault();
      openDrawer(store.focusId);
    }
  }

  function moveFocusToEdge(dir, extend) {
    var tasks = store.visibleTasks();
    if (!tasks.length) return;
    var id = dir > 0 ? tasks[tasks.length - 1].id : tasks[0].id;
    if (extend) store.selectClick(id, { shift: true });
    else { store.focusId = id; store.notify(); }
    var el = els.taskList.querySelector('.task[data-id="' + id + '"]');
    if (el) el.scrollIntoView({ block: 'nearest' });
  }

  function moveFocus(delta, extend) {
    var tasks = store.visibleTasks();
    if (!tasks.length) return;
    var idx = -1;
    for (var i = 0; i < tasks.length; i++) { if (tasks[i].id === store.focusId) { idx = i; break; } }
    var next = idx < 0 ? (delta > 0 ? 0 : tasks.length - 1) : idx + delta;
    if (next < 0) next = 0;
    if (next >= tasks.length) next = tasks.length - 1;

    var id = tasks[next].id;
    if (extend) store.selectClick(id, { shift: true });
    else { store.focusId = id; store.notify(); }

    var el = els.taskList.querySelector('.task[data-id="' + id + '"]');
    if (el) el.scrollIntoView({ block: 'nearest' });
  }

  /* -------------------------------------------------------------- 拖拽排序 */

  var dragId = null;

  function onDragStart(e) {
    var li = e.target.closest('.task');
    if (!li) return;
    // 只有从手柄或行本身开始拖拽才生效；正在编辑标题时不拖拽
    if (editingId) { e.preventDefault(); return; }
    dragId = li.getAttribute('data-id');
    li.classList.add('is-dragging');
    try {
      e.dataTransfer.setData('text/plain', dragId);
      e.dataTransfer.effectAllowed = 'move';
    } catch (err) { /* 某些环境限制 dataTransfer，忽略 */ }
  }

  function onDragOver(e) {
    if (!dragId) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    var li = e.target.closest('.task');
    clearDropHints();
    if (!li) return;
    if (li.getAttribute('data-id') === dragId) return;

    var rect = li.getBoundingClientRect();
    var after = (e.clientY - rect.top) > rect.height / 2;
    li.classList.add(after ? 'is-drop-after' : 'is-drop-before');
  }

  function onDragLeave(e) {
    var li = e.target.closest('.task');
    if (li) li.classList.remove('is-drop-before', 'is-drop-after');
  }

  function onDrop(e) {
    if (!dragId) return;
    e.preventDefault();
    var li = e.target.closest('.task');
    if (li) {
      var targetId = li.getAttribute('data-id');
      var rect = li.getBoundingClientRect();
      var after = (e.clientY - rect.top) > rect.height / 2;
      store.moveTask(dragId, targetId, after);
    }
    clearDropHints();
  }

  function onDragEnd() {
    dragId = null;
    clearDropHints();
    var d = els.taskList.querySelector('.is-dragging');
    if (d) d.classList.remove('is-dragging');
  }

  function clearDropHints() {
    els.taskList.querySelectorAll('.is-drop-before, .is-drop-after').forEach(function (el) {
      el.classList.remove('is-drop-before', 'is-drop-after');
    });
  }

  /* ---------------------------------------------------------------- 框选 */

  function onListMouseDown(e) {
    if (e.button !== 0) return;
    if (!store.getSettings().rubberBandSelect) return;
    if (e.target.closest('.task')) return;   // 点在任务上不是框选
    if (e.target.closest('.check--head')) return;

    var wrapRect = els.listWrap.getBoundingClientRect();
    var startX = e.clientX;
    var startY = e.clientY;
    var additive = e.ctrlKey || e.metaKey;
    var moved = false;
    // 快照按下瞬间的选择集合：框选过程中以它为基础做并集，
    // 这样反复划动不会因为"上一次的结果"而抖动
    var base = new Set(store.selectedIds);
    var lastKey = '';

    function onMove(ev) {
      var dx = ev.clientX - startX;
      var dy = ev.clientY - startY;
      if (!moved && Math.abs(dx) < 4 && Math.abs(dy) < 4) return;
      moved = true;

      var x1 = Math.min(startX, ev.clientX) - wrapRect.left + els.listWrap.scrollLeft;
      var y1 = Math.min(startY, ev.clientY) - wrapRect.top + els.listWrap.scrollTop;

      els.rubberBand.hidden = false;
      els.rubberBand.style.left = x1 + 'px';
      els.rubberBand.style.top = y1 + 'px';
      els.rubberBand.style.width = Math.abs(dx) + 'px';
      els.rubberBand.style.height = Math.abs(dy) + 'px';

      var box = {
        left: Math.min(startX, ev.clientX),
        top: Math.min(startY, ev.clientY),
        right: Math.max(startX, ev.clientX),
        bottom: Math.max(startY, ev.clientY)
      };
      var hits = [];
      els.taskList.querySelectorAll('.task').forEach(function (li) {
        var r = li.getBoundingClientRect();
        var overlap = !(r.right < box.left || r.left > box.right ||
                        r.bottom < box.top || r.top > box.bottom);
        if (overlap) hits.push(li.getAttribute('data-id'));
      });

      // 命中集合未变化时跳过重绘，否则每帧都会重建整个列表（NFR-01）
      var key = hits.join(',');
      if (key === lastKey) return;
      lastKey = key;

      var next = additive ? new Set(base) : new Set();
      for (var i = 0; i < hits.length; i++) next.add(hits[i]);
      store.selectIds(next);
    }

    function onUp() {
      document.removeEventListener('mousemove', onMove);
      document.removeEventListener('mouseup', onUp);
      els.rubberBand.hidden = true;
      els.rubberBand.style.width = '0px';
      els.rubberBand.style.height = '0px';
      // 没有拖动 = 普通点击空白，清空选择（FR-81）
      if (!moved && !additive) store.clearSelection();
    }

    document.addEventListener('mousemove', onMove);
    document.addEventListener('mouseup', onUp);
  }

  /* ------------------------------------------------------------ 批量操作 */

  function selectedIdsArray() { return Array.from(store.selectedIds); }

  function batchSetDone(done) {
    var ids = selectedIdsArray();
    if (!ids.length) return;
    var n = store.setDoneFor(ids, done);
    if (n) flash((done ? '已完成 ' : '已恢复 ') + n + ' 项', 'success');
  }

  function cycleBatchPriority() {
    var ids = selectedIdsArray();
    if (!ids.length) return;
    // 依次轮换：高 → 中 → 低 → 高，便于键盘/鼠标快速循环设置
    var order = ['high', 'medium', 'low'];
    var current = store.getTask(ids[0]).priority;
    var next = order[(order.indexOf(current) + 1) % order.length];
    var n = store.setPriorityFor(ids, next);
    if (n) flash('已将 ' + n + ' 项优先级设为「' + PRIO_TEXT[next] + '」', 'success');
  }

  function batchDelete() {
    var ids = selectedIdsArray();
    if (!ids.length) return;
    var n = store.removeTasks(ids);
    if (n) flash('已删除 ' + n + ' 项，可撤销', 'success');
  }

  /** Delete 键：有选中则删选中，否则删焦点项 */
  function doDelete() {
    if (store.selectedIds.size) return batchDelete();
    var tasks = store.visibleTasks();
    if (!tasks.length) return;
    store.removeTask(tasks[0].id);
  }

  /* ---------------------------------------------------------------- 撤销 */

  function renderUndoToast() {
    var undo = store.getUndo();
    if (!undo) {
      els.toast.hidden = true;
      if (undoTimer) { global.clearTimeout(undoTimer); undoTimer = null; }
      return;
    }
    els.toast.hidden = false;
    els.toastText.textContent = undo.label;
    els.toastAction.hidden = false;

    var total = App.UNDO_WINDOW_MS;
    var remain = Math.max(0, undo.expiresAt - Date.now());
    els.toastProgress.style.transition = 'none';
    els.toastProgress.style.transform = 'scaleX(' + (remain / total) + ')';
    // 强制重排后再启用过渡，否则浏览器会合并两次样式写入导致动画不出现
    void els.toastProgress.offsetWidth;
    els.toastProgress.style.transition = 'transform ' + remain + 'ms linear';
    els.toastProgress.style.transform = 'scaleX(0)';

    if (undoTimer) global.clearTimeout(undoTimer);
    undoTimer = global.setTimeout(function () {
      undoTimer = null;
      renderUndoToast();
    }, remain + 40);
  }

  function doUndo() {
    var ok = store.undo();
    if (ok) flash('已撤销', 'success');
  }

  /* ============================================================ 详情面板 */

  function openDrawer(id) {
    var t = store.getTask(id);
    if (!t) return;
    drawerTaskId = id;

    els.dtTitle.value = t.title;
    els.dtNote.value = t.note || '';
    els.dtDue.value = t.dueAt ? toLocalInput(t.dueAt) : '';
    els.dtPriority.value = t.priority;
    els.dtTags.value = t.tags.join(', ');
    els.dtRemind.checked = !!t.remind;
    els.dtMeta.textContent = '创建于 ' + formatShort(t.createdAt) +
      (t.completedAt ? ' · 完成于 ' + formatShort(t.completedAt) : '');

    els.drawer.hidden = false;
    els.drawerOverlay.hidden = false;
    els.dtTitle.focus();
  }

  function closeDrawer() {
    els.drawer.hidden = true;
    els.drawerOverlay.hidden = true;
    drawerTaskId = null;
  }

  function saveDrawer() {
    if (!drawerTaskId) return;
    var title = els.dtTitle.value.trim();
    if (!title) { flash('标题不能为空', 'error'); els.dtTitle.focus(); return; }

    store.updateTask(drawerTaskId, {
      title: title,
      note: els.dtNote.value.trim(),
      dueAt: els.dtDue.value ? new Date(els.dtDue.value).toISOString() : null,
      priority: els.dtPriority.value,
      tags: els.dtTags.value.split(/[,，、]/).map(function (s) { return s.trim(); }).filter(Boolean),
      remind: els.dtRemind.checked
    });
    closeDrawer();
    flash('已保存', 'success');
  }

  function deleteFromDrawer() {
    if (!drawerTaskId) return;
    var id = drawerTaskId;
    closeDrawer();
    store.removeTask(id);
  }

  /** ISO → datetime-local 需要的本地时间字符串（不能直接用 toISOString，会差时区） */
  function toLocalInput(iso) {
    var d = new Date(iso);
    return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) +
           'T' + pad(d.getHours()) + ':' + pad(d.getMinutes());
  }

  /* ============================================================ 设置面板 */

  function openSettings() {
    var s = store.getSettings();
    els.stTheme.value = s.theme;
    els.stDefaultFilter.value = s.defaultFilter;
    els.stWeekStart.value = String(s.weekStartsOn);
    els.stFloating.checked = s.windowMode === 'floating';
    els.stSelectable.checked = s.selectable;
    els.stAutoStart.checked = s.autoStart;
    els.stStartMinimized.checked = s.startMinimized;
    els.stHotkey.value = s.hotkey;
    els.stHotkeySelectable.textContent = s.hotkeySelectable;
    els.stMultiSelect.checked = s.multiSelectEnabled;
    els.stRubberBand.checked = s.rubberBandSelect;
    els.stKeepSelection.checked = s.keepSelectionAcrossViews;

    syncLayerButtons(s.windowLayer);
    updateDataHint();
    updateAbout();

    els.settingsDrawer.hidden = false;
    els.drawerOverlay.hidden = false;
  }

  function closeSettings() {
    els.settingsDrawer.hidden = true;
    els.drawerOverlay.hidden = true;
  }

  function syncLayerButtons(layer) {
    els.stLayer.querySelectorAll('button[data-layer]').forEach(function (b) {
      b.setAttribute('aria-checked', b.getAttribute('data-layer') === layer ? 'true' : 'false');
    });
    els.layerHint.textContent = layer === 'top'
      ? '窗口始终位于其他窗口之上'
      : layer === 'bottom'
        ? '窗口沉到所有普通窗口之下，仅桌面可见。可用托盘菜单或 Ctrl+Alt+L 前的显示快捷键找回。'
        : '跟随系统默认层级（点击其他窗口会被覆盖）';
  }

  function updateDataHint() {
    var n = store.allTasks().length;
    var bytes = 0;
    try { bytes = (global.localStorage.getItem(S.KEY) || '').length; } catch (e) { bytes = 0; }
    var backupList = App.backups ? App.backups.defaultService.list() : [];
    var backupText = backupList.length ? backupList.length + ' 份，最新 ' + backupList[0].date : (S.hasBackup() ? '旧版备份' : '无');
    els.stDataHint.textContent = '共 ' + n + ' 项任务，占用约 ' +
      (bytes / 1024).toFixed(1) + ' KB。备份：' + backupText;
  }

  function updateAbout() {
    els.aboutText.textContent = '待办清单 v1.2 · 离线运行，不联网、不上传任何数据 · ' +
      (host && host.available ? '桌面版' : '浏览器模式');
  }

  /* ------------------------------------------------------- 层级 / 穿透 */

  function applyLayer(layer) {
    if (!host.available) {
      flash('浏览器模式不支持窗口层级控制', 'error');
      syncLayerButtons(store.getSettings().windowLayer);
      return;
    }
    // 层级是持久状态：既写 settings，也立即下发给宿主生效
    store.setSettings({ windowLayer: layer }, true);
    host.setLayer(layer);
    syncLayerButtons(layer);
    renderWindowButtons();
    renderStatus();
  }

  /**
   * 切换可选中。开启穿透前必须给出明确的恢复方式提示（FR-77）。
   */
  function applySelectable(selectable, notify) {
    if (!host.available) {
      store.setSettings({ selectable: true }, true);
      els.stSelectable.checked = true;
      flash('浏览器模式不支持鼠标穿透', 'error');
      return;
    }
    if (!selectable && notify) {
      flash('已进入不可选中：按 ' + store.getSettings().hotkeySelectable + ' 恢复', 'error', 6000);
    }
    store.setSettings({ selectable: selectable }, true);
    host.setSelectable(selectable);
    els.stSelectable.checked = selectable;
    renderWindowButtons();
    renderStatus();
  }

  /* -------------------------------------------------------------- 数据操作 */

  function exportData() {
    var data = S.exportJson(store.state);
    var blob = new Blob([data], { type: 'application/json' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    var stamp = new Date();
    a.href = url;
    a.download = '待办清单-' + stamp.getFullYear() + pad(stamp.getMonth() + 1) + pad(stamp.getDate()) +
                 '-' + pad(stamp.getHours()) + pad(stamp.getMinutes()) + '.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    global.setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
    flash('已导出 ' + store.allTasks().length + ' 项任务', 'success');
  }

  function importData() {
    var file = els.stImportFile.files && els.stImportFile.files[0];
    if (!file) return;
    processImportFile(file);
    els.stImportFile.value = '';
  }

  function processImportFile(file) {
    var reader = new FileReader();
    reader.onload = function () {
      var res = App.importExport.parse(String(reader.result));
      if (!res.ok) {
        flash('导入失败：' + res.errors.join('；'), 'error', 6000);
        return;
      }
      askImport(res);
    };
    reader.onerror = function () { flash('文件读取失败', 'error'); };
    reader.readAsText(file);
  }

  function applyImport(preview, mode) {
    var result = App.importExport.commit(preview, mode, store.state);
    if (!result.ok) { flash('导入失败：' + result.error, 'error', 6000); return; }
    store.state = result.state;
    store.clearSelection();
    store.notify();
    flash((mode === 'merge' ? '已合并导入 ' : '已覆盖导入 ') + result.count + ' 项任务', 'success');
    closeSettings();
  }

  function restoreBackup() {
    if (!S.hasBackup()) { flash('没有可用的自动备份', 'error'); return; }
    var backup = App.backups ? App.backups.defaultService.latestValid() : null;
    var st = backup ? backup.state : S.readBackup();
    if (!st || !st.tasks.length) { flash('备份内容为空或已损坏', 'error'); return; }
    askConfirm('从备份恢复', '备份包含 ' + st.tasks.length + ' 项任务，将覆盖当前数据。是否继续？', function () {
      store.importState(st, false);
      flash('已从备份恢复 ' + st.tasks.length + ' 项任务', 'success');
      closeSettings();
    });
  }

  function resetAll() {
    askConfirm('清空全部数据', '将删除本机所有任务与设置，且无法恢复。建议先导出备份。是否继续？', function () {
      S.reset();
      global.location.reload();
    });
  }

  /* -------------------------------------------------------------- 确认框 */

  var confirmCb = null;
  var confirmAltCb = null;

  function askConfirm(title, msg, onOk) {
    els.confirmTitle.textContent = title;
    els.confirmMsg.textContent = msg;
    els.confirmBox.hidden = false;
    confirmCb = onOk;
    confirmAltCb = null;
    els.confirmMerge.hidden = true;
    els.confirmOk.textContent = '确定';
    els.confirmOk.focus();
  }

  function askImport(preview) {
    els.confirmTitle.textContent = '导入任务';
    els.confirmMsg.textContent = '共 ' + preview.total + ' 条，' + preview.valid + ' 条有效，' + preview.invalid + ' 条无效，' + preview.duplicates + ' 条重复。请选择合并或覆盖。';
    els.confirmBox.hidden = false;
    els.confirmMerge.hidden = false;
    els.confirmOk.textContent = '覆盖';
    confirmAltCb = function () { applyImport(preview, 'merge'); };
    confirmCb = function () { applyImport(preview, 'replace'); };
    els.confirmMerge.focus();
  }

  function hideConfirm() {
    els.confirmBox.hidden = true;
    confirmCb = null;
    confirmAltCb = null;
    els.confirmMerge.hidden = true;
    els.confirmOk.textContent = '确定';
  }

  function bindConfirm() {
    els.confirmMerge.addEventListener('click', function () {
      var cb = confirmAltCb;
      hideConfirm();
      if (cb) cb();
    });
    els.confirmOk.addEventListener('click', function () {
      var cb = confirmCb;
      hideConfirm();
      if (cb) cb();
    });
  }

  /* ============================================================ 搜索面板 */

  function toggleSearch(force) {
    var show = (force === undefined) ? els.searchbar.hidden : force;
    els.searchbar.hidden = !show;
    if (show) {
      els.searchInput.focus();
      els.searchInput.select();
    } else {
      els.searchInput.value = '';
      store.setQuery('');
    }
  }

  /* ============================================================ 全局快捷键 */

  function onGlobalKeydown(e) {
    var tag = (e.target.tagName || '').toLowerCase();
    var typing = tag === 'input' || tag === 'textarea' || tag === 'select' || e.target.isContentEditable;
    var mod = e.ctrlKey || e.metaKey;

    // 确认框打开时只响应 Esc / Enter
    if (!els.confirmBox.hidden) {
      if (e.key === 'Escape') { e.preventDefault(); hideConfirm(); }
      else if (e.key === 'Enter') { e.preventDefault(); els.confirmOk.click(); }
      return;
    }

    if (e.key === 'Escape') {
      if (!els.drawer.hidden) { e.preventDefault(); closeDrawer(); return; }
      if (!els.settingsDrawer.hidden) { e.preventDefault(); closeSettings(); return; }
      if (store.selectedIds.size) { e.preventDefault(); store.clearSelection(); return; }
      if (!els.searchbar.hidden) { e.preventDefault(); toggleSearch(false); return; }
      if (!els.draftPanel.hidden) { e.preventDefault(); hideDraft(); return; }
    }

    if (mod && e.key.toLowerCase() === 'n') {
      e.preventDefault();
      els.taskInput.focus();
      els.taskInput.select();
      return;
    }

    if (mod && e.key.toLowerCase() === 'f') {
      e.preventDefault();
      toggleSearch(true);
      return;
    }

    // 在录入框按 ↓：无内容时把焦点移到列表，符合连续录入的使用习惯
    if (e.key === 'ArrowDown' && !mod && !e.altKey &&
        e.target === els.taskInput && !els.taskInput.value.trim()) {
      e.preventDefault();
      moveFocus(1, false);
      return;
    }

    if (mod && e.key.toLowerCase() === 'a') {
      // 输入框内的 Ctrl+A 是"全选文本"，不能拦截
      if (typing) return;
      e.preventDefault();
      if (store.getSettings().multiSelectEnabled) store.selectAllVisible();
      return;
    }

    if (mod && e.key.toLowerCase() === 'z' && !typing) {
      e.preventDefault();
      doUndo();
      return;
    }

    // 数字键快速切视图
    if (e.altKey && ['1', '2', '3', '4'].indexOf(e.key) >= 0) {
      e.preventDefault();
      var map = { '1': 'today', '2': 'week', '3': 'all', '4': 'done' };
      store.setFilter(map[e.key]);
      afterFilterChange();
      return;
    }

    if (e.key === 'Delete' && !typing) {
      e.preventDefault();
      doDelete();
      return;
    }

    // 方向键导航：正在输入框中打字时不能抢占（否则无法移动光标）
    if (!typing && (e.key === 'ArrowDown' || e.key === 'ArrowUp') && !e.altKey && !mod) {
      e.preventDefault();
      moveFocus(e.key === 'ArrowDown' ? 1 : -1, e.shiftKey);
    }
  }

  function afterFilterChange() {
    // 取消勾选"跨视图保留选择"时，切视图即收敛选择范围（FR-95）
    if (!store.getSettings().keepSelectionAcrossViews) store.narrowSelectionToVisible();
  }

  /* ============================================================ 窗口控制 */

  function onCloseClick() {
    var s = store.getSettings();
    if (!host.available) {
      global.close();
      return;
    }
    // 浮窗模式下"关闭"是收起到托盘，不退出程序（FR-71）
    if (s.windowMode === 'floating') host.hide();
    else host.close();
  }

  /** 通过标题栏拖拽移动窗口 */
  function bindWindowDrag() {
    var dragging = false, lastX = 0, lastY = 0;

    els.dragRegion.addEventListener('mousedown', function (e) {
      if (e.button !== 0) return;
      if (e.target.closest('button')) return;   // 按钮点击不触发拖拽
      if (!host.available) return;
      dragging = true;
      lastX = e.clientX;
      lastY = e.clientY;
      document.body.style.userSelect = 'none';
      e.preventDefault();
    });

    document.addEventListener('mousemove', function (e) {
      if (!dragging) return;
      var dx = e.clientX - lastX;
      var dy = e.clientY - lastY;
      if (!dx && !dy) return;
      lastX = e.clientX;
      lastY = e.clientY;
      host.moveBy(dx, dy);
    });

    document.addEventListener('mouseup', function () {
      if (!dragging) return;
      dragging = false;
      document.body.style.userSelect = '';
      // 拖拽/缩放结束后让宿主把当前位置与尺寸落盘，供下次启动恢复（FR-75）
      host.commitGeometry();
      if (host.bounds) {
        host.persistGeometryToLocal(store);
      }
    });
  }

  /** 右下角热区拖拽缩放窗口 */
  function bindWindowResize() {
    var resizing = false, lastX = 0, lastY = 0;

    els.resizeGrip.addEventListener('mousedown', function (e) {
      if (e.button !== 0 || !host.available) return;
      resizing = true;
      lastX = e.clientX;
      lastY = e.clientY;
      document.body.style.userSelect = 'none';
      e.preventDefault();
      e.stopPropagation();
    });

    document.addEventListener('mousemove', function (e) {
      if (!resizing) return;
      var dw = e.clientX - lastX;
      var dh = e.clientY - lastY;
      if (!dw && !dh) return;
      lastX = e.clientX;
      lastY = e.clientY;
      host.resizeBy(dw, dh);
    });

    document.addEventListener('mouseup', function () {
      if (!resizing) return;
      resizing = false;
      document.body.style.userSelect = '';
      host.commitGeometry();
      if (host.bounds) {
        host.persistGeometryToLocal(store);
      }
    });
  }

  /* ============================================================ 提示组件 */

  function flash(text, kind, ms) {
    els.flash.textContent = text;
    els.flash.className = 'flash' + (kind ? ' is-' + kind : '');
    els.flash.hidden = false;
    if (flashTimer) global.clearTimeout(flashTimer);
    flashTimer = global.setTimeout(function () {
      els.flash.hidden = true;
      flashTimer = null;
    }, ms || 3000);
  }

  function shake(el) {
    el.animate(
      [{ transform: 'translateX(0)' }, { transform: 'translateX(-4px)' },
       { transform: 'translateX(4px)' }, { transform: 'translateX(0)' }],
      { duration: 180, easing: 'ease-in-out' }
    );
  }

  /* ============================================================ 提醒循环 */

  /**
   * 到期提醒（FR-30 / FR-32）。基于绝对时间比较而非累加计时，
   * 避免后台标签页定时器被节流后产生累计漂移（风险 R3）。
   */
  function startReminderLoop() {
    checkReminders(true);                       // 启动时补提醒一次
    global.setInterval(function () { checkReminders(false); }, 20000);
    // 从后台切回前台时立即补检一次
    document.addEventListener('visibilitychange', function () {
      if (document.visibilityState === 'visible') checkReminders(false);
    });
  }

  function checkReminders(isStartup) {
    var now = Date.now();
    var advance = (store.getSettings().remindAdvanceMinutes || 0) * 60000;
    var due = [];

    store.allTasks().forEach(function (t) {
      if (t.status === 'done' || !t.remind || !t.dueAt || t.remindedAt) return;
      if (new Date(t.dueAt).getTime() - advance <= now) due.push(t);
    });

    if (!due.length) return;

    // 启动时批量补提醒只汇总一条，避免通知轰炸（FR-32）
    if (isStartup && due.length > 1) {
      flash('有 ' + due.length + ' 项任务已到期或逾期', 'error', 8000);
      notifySystem('待办清单', '有 ' + due.length + ' 项任务已到期或逾期');
    } else {
      due.slice(0, 1).forEach(function (t) {
        flash('提醒：' + t.title, 'error', 8000);
        notifySystem('待办提醒', t.title);
      });
    }
    due.forEach(function (t) { store.markReminded(t.id); });
  }

  function notifySystem(title, body) {
    if (!('Notification' in global)) return;
    try {
      if (Notification.permission === 'granted') {
        new Notification(title, { body: body });
      } else if (Notification.permission !== 'denied') {
        Notification.requestPermission();
      }
    } catch (e) { /* 通知不可用时静默降级为页面内 flash，已在上面执行 */ }
  }

  /* ============================================================ 导出到全局 */

  // 测试钩子：允许在控制台注入固定时间与纯内存 store
  App.createApp = function (opts) {
    opts = opts || {};
    store = new App.Store().init(opts.state);
    if (opts.now) store.now = function () { return new Date(opts.now); };
    host = opts.host || new App.Host().init();
    return { store: store, host: host };
  };
  App.render = render;
  App.flash = flash;

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () { bindConfirm(); boot(); });
  } else {
    bindConfirm();
    boot();
  }
})(window);
