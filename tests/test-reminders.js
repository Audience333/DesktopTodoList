'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const ROOT=path.join(__dirname,'..');
const box={window:null,console,Date,JSON,Object,Array,Set,Map,String,Number,Boolean,Math,Promise,Error};box.window=box;vm.createContext(box);
const p=path.join(ROOT,'src/js/services/reminders.js');if(fs.existsSync(p))vm.runInContext(fs.readFileSync(p,'utf8'),box,{filename:p});
assert.ok(box.TodoApp&&box.TodoApp.reminders,'reminders service should exist');
let now=new Date('2026-09-26T10:00:00Z'), notified=[], marked=[];
const service=box.TodoApp.reminders.create({now:()=>now,notify:b=>notified.push(b),markReminded:id=>marked.push(id)});
const task=(id,due,extra)=>Object.assign({id,title:id,status:'todo',dueAt:due,remind:true,remindedAt:null},extra||{});
for(const mins of [0,5,10,30]){
  const due=new Date(now.getTime()+mins*60000).toISOString();
  assert.equal(service.collect([task('m'+mins,due)],{remindAdvanceMinutes:mins},false).tasks.length,1);
}
assert.equal(service.collect([task('late','2026-09-26T09:00:00Z',{remindedAt:'x'})],{remindAdvanceMinutes:0},false).tasks.length,0);
let batch=service.collect([task('a','2026-09-26T09:00:00Z'),task('b','2026-09-26T09:30:00Z')],{},true);
assert.equal(batch.grouped,true);assert.equal(batch.tasks.length,2);
batch=service.tick([task('a','2026-09-26T09:00:00Z')],{},false);assert.equal(notified.length,1);assert.deepEqual(marked,['a']);
const before=task('x','2026-09-26T11:00:00Z',{remindedAt:'old'});
assert.equal(service.resetIfScheduleChanged(before,Object.assign({},before,{dueAt:'2026-09-26T12:00:00Z'})).remindedAt,null);
assert.equal(service.resetIfScheduleChanged(before,Object.assign({},before,{status:'done'})).remindedAt,'old');
now=new Date('2026-09-26T13:00:00Z');assert.equal(service.collect([task('bg','2026-09-26T12:30:00Z')],{},false).tasks.length,1);
console.log('reminders: 13 assertions passed');
