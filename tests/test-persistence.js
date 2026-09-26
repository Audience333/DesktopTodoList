'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

const ROOT = path.join(__dirname, '..');

function memoryStorage(seed) {
  const values = new Map(Object.entries(seed || {}));
  const writes = [];
  return {
    getItem(key) { return values.has(key) ? values.get(key) : null; },
    setItem(key, value) { writes.push(key); values.set(key, String(value)); },
    removeItem(key) { values.delete(key); },
    failQuota() {
      this.setItem = function () {
        const error = new Error('full');
        error.name = 'QuotaExceededError';
        throw error;
      };
    },
    writes
  };
}

function loadApp(storage) {
  const sandbox = {
    localStorage: storage,
    console,
    setTimeout,
    clearTimeout,
    Date,
    Math,
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
    Error
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  for (const file of ['src/js/storage.js', 'src/js/services/persistence.js']) {
    const fullPath = path.join(ROOT, file);
    if (fs.existsSync(fullPath)) {
      vm.runInContext(fs.readFileSync(fullPath, 'utf8'), sandbox, { filename: file });
    }
  }
  return sandbox.TodoApp;
}

const validState = {
  schemaVersion: 1,
  tasks: [{
    id: 't_1', title: '保留我', note: '', priority: 'high', status: 'todo',
    dueAt: null, remind: true, remindedAt: null, tags: ['工作'], order: 1,
    createdAt: '2026-09-26T01:00:00.000Z', updatedAt: '2026-09-26T01:00:00.000Z', completedAt: null
  }],
  settings: { theme: 'dark', defaultFilter: 'all' }
};

function run() {
  let storage = memoryStorage({ 'desktopTodoList.v1': JSON.stringify(validState) });
  let App = loadApp(storage);
  assert.ok(App.persistence, 'App.persistence should be defined');
  let service = App.persistence.create(storage);

  const loaded = service.load();
  assert.equal(loaded.status, 'ok');
  assert.equal(loaded.state.tasks[0].id, 't_1');
  assert.equal(loaded.state.tasks[0].title, '保留我');
  assert.equal(loaded.rawPrimary, JSON.stringify(validState));

  storage = memoryStorage({
    'desktopTodoList.v1': '{broken',
    'desktopTodoList.v1.backup': JSON.stringify(validState)
  });
  App = loadApp(storage);
  service = App.persistence.create(storage);
  const restored = service.load();
  assert.equal(restored.status, 'restored');
  assert.equal(restored.rawPrimary, '{broken');

  storage = memoryStorage({
    'desktopTodoList.v1': '{broken',
    'desktopTodoList.v1.backup': '{also broken'
  });
  App = loadApp(storage);
  service = App.persistence.create(storage);
  const reset = service.load();
  assert.equal(reset.status, 'reset');
  assert.equal(reset.rawPrimary, '{broken');
  assert.deepEqual(storage.writes, [], 'loading corrupt data must not overwrite the primary key');

  storage = memoryStorage();
  App = loadApp(storage);
  service = App.persistence.create(storage);
  storage.failQuota();
  const saved = service.save(validState, true);
  assert.equal(saved.ok, false);
  assert.equal(saved.error.code, 'STORAGE_QUOTA');
  assert.equal(saved.error.source, 'storage');
  assert.equal(service.getLastError().code, 'STORAGE_QUOTA');

  console.log('persistence: 12 assertions passed');
}

run();
