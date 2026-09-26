'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

const ROOT = path.join(__dirname, '..');

function memoryStorage(seed) {
  const values = new Map(Object.entries(seed || {}));
  return {
    get length() { return values.size; },
    key(index) { return Array.from(values.keys())[index] || null; },
    getItem(key) { return values.has(key) ? values.get(key) : null; },
    setItem(key, value) { values.set(key, String(value)); },
    removeItem(key) { values.delete(key); },
    dump() { return Object.fromEntries(values); }
  };
}

function loadApp(storage) {
  const sandbox = { localStorage: storage, console, setTimeout, clearTimeout, Date, Math, JSON, Object, Array, Set, Map, String, Number, Boolean, isNaN, isFinite, Error };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  for (const file of ['src/js/storage.js', 'src/js/services/persistence.js', 'src/js/services/backups.js']) {
    const full = path.join(ROOT, file);
    if (fs.existsSync(full)) vm.runInContext(fs.readFileSync(full, 'utf8'), sandbox, { filename: file });
  }
  return sandbox.TodoApp;
}

function state(title) {
  return {
    schemaVersion: 1,
    tasks: [{ title, id: 'id-' + title, priority: 'medium', status: 'todo', order: 1, createdAt: '2026-09-01T00:00:00.000Z' }],
    settings: {}
  };
}

let current = new Date(2026, 8, 1, 9, 0, 0);
const storage = memoryStorage();
const App = loadApp(storage);
assert.ok(App.backups, 'App.backups should be defined');
const backups = App.backups.create(storage, () => new Date(current.getTime()));

assert.equal(backups.createDaily(state('day1')).created, true);
assert.equal(backups.createDaily(state('changed-same-day')).created, false);
assert.equal(backups.list().length, 1);
assert.equal(backups.latestValid().state.tasks[0].title, 'day1');

for (let day = 2; day <= 8; day++) {
  current = new Date(2026, 8, day, 9, 0, 0);
  assert.equal(backups.createDaily(state('day' + day)).created, true);
}
assert.equal(backups.list().length, 7);
assert.equal(backups.list()[0].date, '2026-09-08');
assert.equal(backups.list()[6].date, '2026-09-02');
assert.equal(storage.getItem('desktopTodoList.v1.backups.2026-09-01'), null);

storage.setItem('desktopTodoList.v1.backups.index', '{broken');
assert.equal(backups.list().length, 7, 'corrupt index should be rebuilt from item keys');

storage.setItem('desktopTodoList.v1.backups.2026-09-08', '{broken');
assert.equal(backups.latestValid().state.tasks[0].title, 'day7');

const legacyStorage = memoryStorage({ 'desktopTodoList.v1.backup': JSON.stringify(state('legacy')) });
const legacyApp = loadApp(legacyStorage);
const legacyBackups = legacyApp.backups.create(legacyStorage, () => current);
assert.equal(legacyBackups.latestValid().state.tasks[0].title, 'legacy');

console.log('backups: 22 assertions passed');
