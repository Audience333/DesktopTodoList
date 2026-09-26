'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');
const ROOT = path.join(__dirname, '..');

function storage() {
  const m = new Map();
  return { get length(){return m.size;}, key(i){return Array.from(m.keys())[i]||null;}, getItem(k){return m.has(k)?m.get(k):null;}, setItem(k,v){m.set(k,String(v));}, removeItem(k){m.delete(k);} };
}
function load() {
  const localStorage = storage();
  const box = { localStorage, console, setTimeout, clearTimeout, Date, Math, JSON, Object, Array, Set, Map, String, Number, Boolean, isNaN, isFinite, Error };
  box.window = box; vm.createContext(box);
  for (const f of ['src/js/storage.js','src/js/services/persistence.js','src/js/services/backups.js','src/js/services/import-export.js']) {
    const p=path.join(ROOT,f); if(fs.existsSync(p)) vm.runInContext(fs.readFileSync(p,'utf8'),box,{filename:f});
  }
  return box.TodoApp;
}
function current() {
  return { schemaVersion:1, tasks:[{id:'keep',title:'保留',priority:'medium',status:'todo',order:1,createdAt:'2026-01-01T00:00:00Z'}], settings:{theme:'dark'} };
}
const App=load();
assert.ok(App.importExport,'App.importExport should be defined');
const original=current();
const bad=App.importExport.parse('{bad');
assert.equal(bad.ok,false); assert.equal(original.tasks[0].title,'保留');

const preview=App.importExport.parse(JSON.stringify({schemaVersion:0,tasks:[
  {id:'keep',title:'覆盖',createdAt:'2026-01-01T00:00:00Z'},
  {id:'keep',title:'文件内重复',createdAt:'2026-01-01T00:00:00Z'},
  {id:'new',title:'新增',createdAt:'2026-01-01T00:00:00Z'},
  {title:'   '}
]}));
assert.equal(preview.ok,true); assert.equal(preview.total,4); assert.equal(preview.valid,3); assert.equal(preview.invalid,1); assert.equal(preview.duplicates,1);

let saved=null, backed=0;
const service=App.importExport.create({
  save(state){saved=state;return {ok:true};},
  backup(){backed++;return {ok:true};}
});
const merged=service.commit(preview,'merge',original);
assert.equal(merged.ok,true); assert.equal(merged.state.tasks.length,2); assert.equal(merged.state.tasks.find(t=>t.id==='keep').title,'文件内重复'); assert.equal(backed,1); assert.ok(saved);
const replaced=service.commit(preview,'replace',original);
assert.equal(replaced.ok,true); assert.equal(replaced.state.tasks.length,2); assert.equal(replaced.state.settings.theme,'system');

const failing=App.importExport.create({save(){return {ok:false,error:'disk'};},backup(){return {ok:true};}});
const before=JSON.stringify(original);
const failed=failing.commit(preview,'replace',original);
assert.equal(failed.ok,false); assert.equal(JSON.stringify(original),before); assert.equal(failed.state,original);
assert.equal(service.commit(preview,'unknown',original).ok,false);
console.log('import-export: 20 assertions passed');
