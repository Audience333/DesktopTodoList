/* =============================================================================
 * backups.js — 每日备份、七份轮换与旧备份兼容
 * ========================================================================== */
(function (global) {
  'use strict';

  var App = global.TodoApp = global.TodoApp || {};
  var S = App.storage;
  if (!S) throw new Error('storage.js must be loaded before backups.js');

  var INDEX_KEY = 'desktopTodoList.v1.backups.index';
  var ITEM_PREFIX = 'desktopTodoList.v1.backups.';
  var LEGACY_KEY = S.BACKUP_KEY;
  var MAX_BACKUPS = 7;

  function pad(value) { return value < 10 ? '0' + value : String(value); }
  function dateKey(date) {
    return date.getFullYear() + '-' + pad(date.getMonth() + 1) + '-' + pad(date.getDate());
  }

  function create(storage, now) {
    now = now || function () { return new Date(); };

    function discoverDates() {
      var dates = [];
      try {
        for (var i = 0; i < storage.length; i++) {
          var key = storage.key(i);
          if (key && key.indexOf(ITEM_PREFIX) === 0) {
            var suffix = key.slice(ITEM_PREFIX.length);
            if (/^\d{4}-\d{2}-\d{2}$/.test(suffix) && dates.indexOf(suffix) < 0) dates.push(suffix);
          }
        }
      } catch (error) { /* fallback to any valid index entries */ }
      return dates.sort().reverse();
    }

    function readIndex() {
      try {
        var raw = storage.getItem(INDEX_KEY);
        if (raw) {
          var parsed = JSON.parse(raw);
          if (Array.isArray(parsed)) {
            return parsed.filter(function (value, index, all) {
              return /^\d{4}-\d{2}-\d{2}$/.test(value) && all.indexOf(value) === index;
            }).sort().reverse();
          }
        }
      } catch (error) { /* rebuild below */ }
      return discoverDates();
    }

    function list() {
      return readIndex().filter(function (date) {
        try { return storage.getItem(ITEM_PREFIX + date) !== null; } catch (error) { return false; }
      }).map(function (date) { return { date: date, key: ITEM_PREFIX + date }; });
    }

    function parseKey(key) {
      var raw;
      try { raw = storage.getItem(key); } catch (error) { return null; }
      if (!raw) return null;
      var migrated = App.persistence.create(storage).migrate(raw);
      if (!migrated.ok) return null;
      return { state: migrated.state, status: 'backup', key: key, dropped: migrated.dropped || 0 };
    }

    function restore(key) { return parseKey(key); }

    function latestValid() {
      var items = list();
      for (var i = 0; i < items.length; i++) {
        var restored = parseKey(items[i].key);
        if (restored) {
          restored.date = items[i].date;
          return restored;
        }
      }
      var legacy = parseKey(LEGACY_KEY);
      if (legacy) legacy.date = 'legacy';
      return legacy;
    }

    function createDaily(state) {
      var date = dateKey(now());
      var key = ITEM_PREFIX + date;
      var existing = list();
      if (existing.some(function (item) { return item.date === date; })) {
        return { ok: true, created: false, date: date };
      }

      var sanitized = S.sanitizeState(state);
      var payload;
      try { payload = JSON.stringify(sanitized.state); }
      catch (error) { return { ok: false, created: false, error: '备份序列化失败：' + error.message }; }

      try {
        storage.setItem(key, payload);
        var dates = [date].concat(existing.map(function (item) { return item.date; }))
          .filter(function (value, index, all) { return all.indexOf(value) === index; })
          .sort().reverse();
        var keep = dates.slice(0, MAX_BACKUPS);
        storage.setItem(INDEX_KEY, JSON.stringify(keep));

        var warnings = [];
        dates.slice(MAX_BACKUPS).forEach(function (oldDate) {
          try { storage.removeItem(ITEM_PREFIX + oldDate); }
          catch (error) { warnings.push('无法清理 ' + oldDate + ' 的旧备份'); }
        });
        return { ok: true, created: true, date: date, warnings: warnings };
      } catch (error) {
        return { ok: false, created: false, error: '创建自动备份失败：' + error.message };
      }
    }

    return { createDaily: createDaily, list: list, restore: restore, latestValid: latestValid };
  }

  var defaultService = create(global.localStorage);
  App.backups = { create: create, defaultService: defaultService };

  S.hasBackup = function () { return defaultService.latestValid() !== null; };
  S.readBackup = function () {
    var result = defaultService.latestValid();
    return result ? result.state : null;
  };
  S.writeBackup = function (state) { return defaultService.createDaily(state).ok; };
})(window);
