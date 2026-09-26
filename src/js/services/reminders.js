(function(global){'use strict';var App=global.TodoApp=global.TodoApp||{};
function create(deps){deps=deps||{};var now=deps.now||function(){return new Date();};
function collect(tasks,settings,startup){var at=now().getTime(),advance=((settings&&settings.remindAdvanceMinutes)||0)*60000;var due=(tasks||[]).filter(function(t){return t.status!=='done'&&t.remind&&t.dueAt&&!t.remindedAt&&new Date(t.dueAt).getTime()-advance<=at;});return{tasks:due,grouped:!!startup&&due.length>1,startup:!!startup};}
function tick(tasks,settings,startup){var batch=collect(tasks,settings,startup);if(!batch.tasks.length)return batch;if(deps.notify)deps.notify(batch);batch.tasks.forEach(function(t){if(deps.markReminded)deps.markReminded(t.id);});return batch;}
function resetIfScheduleChanged(before,after){var out=Object.assign({},after);if(before&&after&&(before.dueAt!==after.dueAt||before.remind!==after.remind))out.remindedAt=null;return out;}
return{collect:collect,tick:tick,resetIfScheduleChanged:resetIfScheduleChanged};}
App.reminders={create:create};})(window);
