/* =============================================================================
 * persistence.js — 主数据读取、迁移与可靠写入
 * ========================================================================== */
(function (global) {
  'use strict';

  var App = global.TodoApp = global.TodoApp || {};
  var legacy = App.storage;
  if (!legacy) throw new Error('storage.js must be loaded before persistence.js');

  var KEY = legacy.KEY;
  var BACKUP_KEY = legacy.BACKUP_KEY;
  var SAVE_DEBOUNCE_MS = 300;

  function appError(code, message, recoverable, detail) {
    return {
      code: code,
      message: message,
      source: 'storage',
      recoverable: recoverable !== false,
      detail: detail || ''
    };
  }

  function create(storage) {
    var saveTimer = null;
    var lastError = null;

    function rawGet(key) {
      try {
        return storage.getItem(key);
      } catch (error) {
        lastError = appError('STORAGE_READ', '无法读取本机数据', true, error && error.message);
        return null;
      }
    }

    function migrate(raw) {
      var parsed = raw;
      if (typeof raw === 'string') {
        try {
          parsed = JSON.parse(raw);
        } catch (error) {
          return { ok: false, error: appError('STORAGE_INVALID_JSON', '数据格式已损坏', true, error.message) };
        }
      }
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
        return { ok: false, error: appError('STORAGE_INVALID_SCHEMA', '数据结构无效', true) };
      }
      var result = legacy.sanitizeState(parsed);
      return {
        ok: true,
        state: result.state,
        repaired: result.repaired,
        dropped: result.dropped || 0
      };
    }

    function load() {
      var rawPrimary = rawGet(KEY);
      if (rawPrimary === null) {
        return { state: legacy.createDefault(), status: 'fresh', dropped: 0, rawPrimary: null };
      }

      var primary = migrate(rawPrimary);
      if (primary.ok) {
        return {
          state: primary.state,
          status: primary.repaired ? 'repaired' : 'ok',
          dropped: primary.dropped,
          rawPrimary: rawPrimary
        };
      }

      var rawBackup = rawGet(BACKUP_KEY);
      if (rawBackup !== null) {
        var backup = migrate(rawBackup);
        if (backup.ok && backup.state.tasks.length > 0) {
          return {
            state: backup.state,
            status: 'restored',
            dropped: backup.dropped,
            rawPrimary: rawPrimary
          };
        }
      }

      return {
        state: legacy.createDefault(),
        status: 'reset',
        dropped: 0,
        rawPrimary: rawPrimary,
        error: primary.error
      };
    }

    function writeNow(state) {
      var payload;
      try {
        payload = JSON.stringify(state);
      } catch (error) {
        lastError = appError('STORAGE_SERIALIZE', '数据无法序列化，请先导出当前内容', false, error.message);
        return { ok: false, error: lastError };
      }
      try {
        storage.setItem(KEY, payload);
        lastError = null;
        return { ok: true };
      } catch (error) {
        lastError = error && error.name === 'QuotaExceededError'
          ? appError('STORAGE_QUOTA', '存储空间不足，请导出数据后清理', true, error.message)
          : appError('STORAGE_WRITE', '写入失败：' + (error && error.message ? error.message : '未知错误'), true, error && error.message);
        return { ok: false, error: lastError, quota: lastError.code === 'STORAGE_QUOTA' };
      }
    }

    function save(state, immediate) {
      if (immediate) {
        if (saveTimer) global.clearTimeout(saveTimer);
        saveTimer = null;
        return writeNow(state);
      }
      if (saveTimer) global.clearTimeout(saveTimer);
      saveTimer = global.setTimeout(function () {
        saveTimer = null;
        var result = writeNow(state);
        if (!result.ok && typeof save.onError === 'function') save.onError(result);
      }, SAVE_DEBOUNCE_MS);
      return { ok: true, deferred: true };
    }

    function flush(state) {
      if (saveTimer) global.clearTimeout(saveTimer);
      saveTimer = null;
      return writeNow(state);
    }

    return {
      load: load,
      save: save,
      flush: flush,
      migrate: migrate,
      getLastError: function () { return lastError; }
    };
  }

  App.persistence = { create: create };

  var defaultService = create(global.localStorage);
  legacy.load = defaultService.load;
  function legacyResult(result) {
    if (!result || result.ok !== false || !result.error || typeof result.error === 'string') return result;
    return {
      ok: false,
      error: result.error.message,
      errorInfo: result.error,
      quota: result.quota === true
    };
  }
  function legacySave(state, immediate) {
    defaultService.save.onError = function (result) {
      if (typeof legacySave.onError === 'function') legacySave.onError(legacyResult(result));
    };
    return legacyResult(defaultService.save(state, immediate));
  }
  legacy.save = legacySave;
  legacy.flush = function (state) { return legacyResult(defaultService.flush(state)); };
  legacy.migrate = defaultService.migrate;
  legacy.getLastError = function () {
    var error = defaultService.getLastError();
    return error ? error.message : null;
  };
})(window);
