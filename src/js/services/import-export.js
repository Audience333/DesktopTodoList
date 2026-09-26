/* =============================================================================
 * import-export.js — 导入预览、合并/覆盖与失败回滚
 * ========================================================================== */
(function (global) {
  'use strict';
  var App = global.TodoApp = global.TodoApp || {};
  var S = App.storage;

  function parse(text) {
    var raw;
    try { raw = JSON.parse(text); }
    catch (error) { return { ok: false, errors: ['文件不是合法的 JSON 格式'] }; }
    if (Array.isArray(raw)) raw = { schemaVersion: 1, tasks: raw };
    if (!raw || typeof raw !== 'object' || !Array.isArray(raw.tasks)) {
      return { ok: false, errors: ['文件缺少 tasks 字段'] };
    }

    var valid = 0, invalid = 0, duplicates = 0;
    var byId = Object.create(null);
    var order = [];
    raw.tasks.forEach(function (item, index) {
      var task = S.sanitizeTask(item, index + 1);
      if (!task) { invalid++; return; }
      valid++;
      if (byId[task.id]) duplicates++;
      else order.push(task.id);
      byId[task.id] = task;
    });
    var cleanRaw = { schemaVersion: S.SCHEMA_VERSION, tasks: order.map(function (id) { return byId[id]; }), settings: raw.settings };
    var clean = S.sanitizeState(cleanRaw).state;
    return { ok: true, valid: valid, invalid: invalid, duplicates: duplicates, total: raw.tasks.length, state: clean, errors: [] };
  }

  function create(deps) {
    deps = deps || {};
    function commit(preview, mode, currentState) {
      if (!preview || !preview.ok) return { ok: false, state: currentState, error: '导入预览无效' };
      if (mode !== 'merge' && mode !== 'replace') return { ok: false, state: currentState, error: '导入模式无效' };
      var next;
      if (mode === 'replace') {
        next = JSON.parse(JSON.stringify(preview.state));
      } else {
        next = JSON.parse(JSON.stringify(currentState));
        var positions = Object.create(null);
        next.tasks.forEach(function (task, index) { positions[task.id] = index; });
        preview.state.tasks.forEach(function (task) {
          if (positions[task.id] !== undefined) next.tasks[positions[task.id]] = task;
          else { positions[task.id] = next.tasks.length; next.tasks.push(task); }
        });
      }
      if (typeof deps.backup === 'function') {
        var backupResult = deps.backup(currentState);
        if (backupResult && backupResult.ok === false) return { ok: false, state: currentState, error: backupResult.error || '无法创建导入前备份' };
      }
      var saveResult = deps.save ? deps.save(next) : { ok: true };
      if (!saveResult || saveResult.ok === false) return { ok: false, state: currentState, error: saveResult && saveResult.error ? saveResult.error : '导入保存失败' };
      return { ok: true, state: next, count: preview.state.tasks.length };
    }
    return { commit: commit };
  }

  var defaultService = create({
    save: function (state) { return S.flush(state); },
    backup: function (state) { return App.backups.defaultService.createDaily(state); }
  });
  App.importExport = {
    parse: parse,
    create: create,
    commit: defaultService.commit,
    export: function (state) { return JSON.stringify(state, null, 2); }
  };
})(window);
