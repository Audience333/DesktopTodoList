# DesktopTodoList Complete PRD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完整交付 DesktopTodoList PRD v1.2，同时把前端和 Windows 宿主渐进迁移为可独立测试的模块化架构。

**Architecture:** 保留现有零构建 IIFE 全局入口和 `desktopTodoList.v1` 数据键，通过兼容适配器逐步抽取核心状态、持久化、备份、导入导出、提醒、UI 控制器及宿主模块。每个任务先固定或新增行为测试，再迁移实现；任何阶段结束时应用都可独立运行。

**Tech Stack:** 原生 HTML/CSS/JavaScript、Node.js 内置模块测试工具、PowerShell 5.1、Win32 P/Invoke、Microsoft Edge `--app` 模式。

**Spec:** `docs/superpowers/specs/2026-09-25-complete-prd-architecture-design.md`

## Global Constraints

- 保持纯 HTML、CSS、JavaScript 与 PowerShell 5.1，不添加 npm、CDN、第三方运行时或网络请求。
- 主数据继续使用 localStorage 键 `desktopTodoList.v1`，现有 `schemaVersion: 1` 数据必须无损加载。
- 支持 Windows 10 1809+、Windows 11、x64、ARM64，以及 125%/150%/200% DPI。
- 浏览器模式必须保留核心任务功能；宿主能力缺失时禁用对应设置并给出可见说明。
- 所有产品行为变更执行红—绿—重构；每个任务结束时运行该任务测试和完整自动测试。
- 不覆盖或删除用户现有未提交更改；每次只暂存任务列出的文件。

## Review Focus

- localStorage 主数据和一个或多个备份同时损坏时，不覆盖原始字符串，应用进入可导出的安全空状态；由 Task 2 的损坏链测试覆盖。
- 导入文件含重复 id、非法任务和旧 schema 时，预览计数准确且提交失败可以回滚；由 Task 4 的事务导入测试覆盖。
- 提醒时间被修改、任务完成后恢复、后台计时器被节流时，不漏提醒也不重复提醒；由 Task 5 的时间边界测试覆盖。
- 快捷键新绑定冲突或穿透恢复键失效时，旧绑定保持有效且用户不会被锁死；由 Task 6 和 Task 10 的冲突回滚测试覆盖。
- 超过 200 条且行高变化、焦点移动或跨视图选择时，虚拟列表不丢选择、不跳焦点；由 Task 9 的范围与焦点测试覆盖。

---

## 阶段 A：基线与数据可靠性

### Task 1: 建立可提交基线与统一测试入口

**Requirements:** NFR-08、NFR-09。

**Files:**
- Modify: `tests/运行全部测试.bat`
- Create: `tests/run-all.ps1`
- Modify: `tests/test-window.ps1`
- Track unchanged baseline: `src/**`, `host/**`, `tests/**`, `docs/需求文档.md`, `启动待办.bat`

**Interfaces:**
- Consumes: 现有 `node tests/run-tests.js`、`tests/test-server.ps1`、`tests/test-window.ps1`。
- Produces: `powershell -File tests/run-all.ps1`，返回 0 表示全部必需测试通过；系统环境不支持的检查输出 `[SKIP]`，但测试进程返回 0。

- [ ] **Step 1: 写失败的测试入口自检**

在 `tests/run-all.ps1` 中先实现入口契约检查：执行三组测试、汇总 PASS/FAIL/SKIP，并以失败数决定退出码；此时保留对窗口测试当前异常退出的断言，使运行结果失败。

- [ ] **Step 2: 运行入口并确认失败原因**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: FAIL，明确指出真实窗口句柄为 0 后仍访问边界对象，或批处理编码入口未可靠转发。

- [ ] **Step 3: 修正测试环境跳过与批处理转发**

让 `test-window.ps1` 在无交互桌面、无法启动靶窗口或注册表被拒绝时输出具体 `[SKIP]` 并继续可执行的纯函数测试。将批处理改为仅调用 ASCII 路径安全的 PowerShell 入口，文件保存为 UTF-8 with BOM 或仅含 ASCII 命令。

- [ ] **Step 4: 运行完整基线**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: JavaScript 126 项、服务器 27 项通过；窗口纯函数测试通过，环境相关项目明确 PASS 或 SKIP；总退出码 0。

- [ ] **Step 5: 记录当前源码基线并提交**

```powershell
git add -- docs/需求文档.md src host tests 启动待办.bat
git commit -m "chore: establish DesktopTodoList source baseline"
```

### Task 2: 抽取持久化与迁移模块

**Requirements:** FR-40、FR-41、FR-44、NFR-05。

**Files:**
- Create: `src/js/services/persistence.js`
- Modify: `src/js/storage.js`
- Modify: `src/index.html`
- Create: `tests/test-persistence.js`
- Modify: `tests/run-tests.js`

**Interfaces:**
- Consumes: Web Storage 风格对象 `{getItem,setItem,removeItem}` 和现有任务清洗规则。
- Produces: `App.persistence.create(storage)`；实例方法 `load(): LoadResult`、`save(state, immediate): SaveResult`、`flush(state): SaveResult`、`migrate(raw): MigrationResult`、`getLastError(): AppError|null`。
- `LoadResult` 包含 `{state,status,dropped,rawPrimary}`；关键错误包含 `{code,message,source:'storage',recoverable,detail}`。

- [ ] **Step 1: 写迁移和损坏链失败测试**

在 `tests/test-persistence.js` 断言：schema 1 无损读取；主数据损坏时保留 `rawPrimary`；主数据及备份均损坏时不调用 `setItem(KEY, ...)`；QuotaExceeded 返回 `STORAGE_QUOTA`。

- [ ] **Step 2: 运行新测试确认失败**

Run: `node tests/test-persistence.js`

Expected: FAIL，`App.persistence` 未定义。

- [ ] **Step 3: 实现持久化服务与旧入口适配器**

实现 `App.persistence.create(storage)`；`storage.js` 保留 `App.storage` 公共方法并转发给默认实例。保持 300ms 防抖，迁移和校验成功前不得覆盖主键。

- [ ] **Step 4: 运行模块和完整测试**

Run: `node tests/test-persistence.js`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/services/persistence.js src/js/storage.js src/index.html tests/test-persistence.js tests/run-tests.js
git commit -m "refactor: extract persistence and migration service"
```

### Task 3: 实现每日七份自动备份

**Requirements:** FR-41、FR-43。

**Files:**
- Create: `src/js/services/backups.js`
- Modify: `src/js/services/persistence.js`
- Modify: `src/js/storage.js`
- Modify: `src/js/app.js`
- Create: `tests/test-backups.js`
- Modify: `src/index.html`

**Interfaces:**
- Consumes: storage、已校验 state、`now(): Date`。
- Produces: `App.backups.create(storage, now)`；方法 `createDaily(state): BackupResult`、`list(): BackupMeta[]`、`restore(key): LoadResult`、`latestValid(): LoadResult|null`。
- 键：`desktopTodoList.v1.backups.index`、`desktopTodoList.v1.backups.YYYY-MM-DD`；最多七个日期；兼容读取 `desktopTodoList.v1.backup`。

- [ ] **Step 1: 写备份轮换失败测试**

断言同日只写一次、连续八天后仅保留最近七份、损坏索引可从分项键恢复、最新备份损坏时回退到下一有效备份、旧备份键仍可恢复。

- [ ] **Step 2: 运行确认缺失功能失败**

Run: `node tests/test-backups.js`

Expected: FAIL，`App.backups` 未定义。

- [ ] **Step 3: 实现备份服务并接入启动流程**

加载主数据成功后调用 `createDaily`。先写备份分项，再原子更新索引；清理失败返回警告但不阻止主数据使用。恢复设置页展示可用备份日期。

- [ ] **Step 4: 运行备份与完整测试**

Run: `node tests/test-backups.js`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/services/backups.js src/js/services/persistence.js src/js/storage.js src/js/app.js src/index.html tests/test-backups.js
git commit -m "feat: add rotating daily backups"
```

### Task 4: 抽取事务式导入导出

**Requirements:** FR-42、FR-56。

**Files:**
- Create: `src/js/services/import-export.js`
- Modify: `src/js/storage.js`
- Modify: `src/js/store.js`
- Modify: `src/js/app.js`
- Modify: `src/index.html`
- Create: `tests/test-import-export.js`

**Interfaces:**
- Consumes: persistence、backups、当前 state、文件文本。
- Produces: `App.importExport.parse(text): ImportPreview`、`commit(preview, mode, currentState): ImportResult`、`export(state): string`。
- `mode` 仅为 `'merge'|'replace'`；预览包含 `{valid,invalid,duplicates,total,state,errors}`。

- [ ] **Step 1: 写事务导入失败测试**

断言非法 JSON 不修改当前状态；旧 schema 可预览；重复 id 计数准确；merge 覆盖同 id 并保留其他任务；replace 完整替换；保存失败恢复原状态和安全备份。

- [ ] **Step 2: 运行确认失败**

Run: `node tests/test-import-export.js`

Expected: FAIL，事务接口未定义。

- [ ] **Step 3: 实现服务和合并/覆盖对话框**

设置页选择文件后展示预览与两个明确按钮。全页 `dragover/drop` 只接受单个 `.json` 文件，并进入同一预览流程。

- [ ] **Step 4: 运行模块和完整测试**

Run: `node tests/test-import-export.js`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/services/import-export.js src/js/storage.js src/js/store.js src/js/app.js src/index.html tests/test-import-export.js
git commit -m "feat: add transactional merge and replace import"
```

## 阶段 B：提醒与可恢复设置

### Task 5: 抽取提醒服务和通知适配器

**Requirements:** FR-30、FR-31、FR-32、FR-33。

**Files:**
- Create: `src/js/services/reminders.js`
- Create: `src/js/ui/notifications.js`
- Modify: `src/js/store.js`
- Modify: `src/js/app.js`
- Modify: `src/index.html`
- Create: `tests/test-reminders.js`
- Modify: `host/launcher.ps1`

**Interfaces:**
- Produces: `App.reminders.create({now,notify,markReminded})`；方法 `collect(tasks, settings, startup): ReminderBatch`、`tick(tasks, settings, startup): ReminderBatch`、`resetIfScheduleChanged(before, after): Task`。
- Produces: `App.notifications.create({host,webNotification,audio,flash})`；方法 `notify(batch): Promise<NotificationResult>`。
- 新宿主命令：`notify`，参数 `{title,body,taskIds,allowComplete}`。

- [ ] **Step 1: 写提醒边界失败测试**

断言 0/5/10/30 分钟提前；启动多项汇总；普通 tick 逐项；`remindedAt` 去重；修改 dueAt/remind 后重置；完成再恢复但时间未改不重复；后台跨过截止时间后下一 tick 触发。

- [ ] **Step 2: 运行确认失败**

Run: `node tests/test-reminders.js`

Expected: FAIL，提醒服务未定义。

- [ ] **Step 3: 实现纯提醒服务和通知降级链**

通知顺序固定为宿主、Web Notification、页面提示加短音。设置页增加 0/5/10/30 分钟选项；宿主暂以托盘通知实现，点击通知显示窗口并通过 inbox 定位任务。

- [ ] **Step 4: 运行提醒和完整测试**

Run: `node tests/test-reminders.js`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/services/reminders.js src/js/ui/notifications.js src/js/store.js src/js/app.js src/index.html tests/test-reminders.js host/launcher.ps1
git commit -m "feat: add reliable advance reminders"
```

### Task 6: 完成设置验证、快捷键编辑和关闭行为

**Requirements:** FR-50、FR-52、FR-53、FR-60 至 FR-69、FR-77。

**Files:**
- Create: `src/js/ui/settings.js`
- Modify: `src/js/app.js`
- Modify: `src/js/storage.js`
- Modify: `src/js/host.js`
- Modify: `src/index.html`
- Modify: `host/launcher.ps1`
- Modify: `tests/test-window.ps1`
- Create: `tests/test-settings.js`

**Interfaces:**
- Produces: `App.settings.validate(settings, capabilities): ValidationResult`、`normalizeHotkey(text): string|null`。
- 新宿主命令 `try-hotkeys` 接收 `{show,selectable}`，返回 `{ok,conflicts,active}`；失败时旧绑定保持注册。
- 设置 `closeBehavior: 'exit'|'tray'`，默认 `'tray'`。

- [ ] **Step 1: 写设置与冲突回滚失败测试**

断言快捷键规范化、相同快捷键被拒绝、宿主缺失时系统设置禁用、冲突返回后页面恢复旧值、穿透恢复键不可用且托盘不可用时禁止穿透、关闭行为持久化。

- [ ] **Step 2: 运行确认失败**

Run: `node tests/test-settings.js`

Expected: FAIL，设置控制器接口未定义。

- [ ] **Step 3: 实现设置控制器和宿主试注册事务**

先注册新组合；全部成功后注销旧组合并交换 id，任何失败则注销新组合并保留旧组合。关闭按钮依照 `closeBehavior` 发出 `close` 或 `hide`。

- [ ] **Step 4: 运行相关及完整测试**

Run: `node tests/test-settings.js`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/ui/settings.js src/js/app.js src/js/storage.js src/js/host.js src/index.html host/launcher.ps1 tests/test-window.ps1 tests/test-settings.js
git commit -m "feat: add recoverable hotkey and close settings"
```

## 阶段 C：列表架构与性能

### Task 7: 抽取选择模型并补齐批量截止时间和标签

**Requirements:** FR-80 至 FR-96。

**Files:**
- Create: `src/js/core/selection-store.js`
- Modify: `src/js/store.js`
- Modify: `src/js/app.js`
- Modify: `src/index.html`
- Create: `tests/test-selection-store.js`
- Modify: `tests/run-tests.js`

**Interfaces:**
- Produces: `App.SelectionStore({multiSelectEnabled,keepAcrossViews})`，方法 `click`、`range`、`toggle`、`selectAll`、`narrowTo`、`removeMissing`、`snapshot`。
- `TaskStore.setDueAtFor(ids, dueAt)`；`TaskStore.updateTagsFor(ids, {add,remove})`，每任务最多五个去重标签。

- [ ] **Step 1: 写独立选择及批量操作失败测试**

迁移现有选择断言并增加：可见列表顺序变化后的锚点行为、删除失效 id、跨视图隐藏计数、批量截止时间、标签添加/移除/去重/五个上限。

- [ ] **Step 2: 运行确认失败**

Run: `node tests/test-selection-store.js`

Expected: FAIL，独立选择模块和批量方法未定义。

- [ ] **Step 3: 实现模块并保留 Store 兼容方法**

`store.js` 的原选择方法转发给 SelectionStore。批量工具栏增加截止时间和标签对话框，完成复选框事件继续阻止选择冒泡。

- [ ] **Step 4: 运行相关及完整测试**

Run: `node tests/test-selection-store.js`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/core/selection-store.js src/js/store.js src/js/app.js src/index.html tests/test-selection-store.js tests/run-tests.js
git commit -m "refactor: extract selection and complete bulk editing"
```

### Task 8: 拆分任务列表、编辑器和键盘控制器

**Requirements:** FR-01 至 FR-10、FR-20 至 FR-25、FR-51、FR-54、FR-55。

**Files:**
- Create: `src/js/ui/task-list.js`
- Create: `src/js/ui/task-editor.js`
- Create: `src/js/ui/keyboard.js`
- Modify: `src/js/app.js`
- Modify: `src/index.html`
- Create: `tests/test-keyboard.js`
- Create: `tests/test-task-list.js`

**Interfaces:**
- `App.TaskList.create({root,store,selection,formatters})`：`render(viewModel)`、`focus(id)`、`destroy()`。
- `App.TaskEditor.create({quickForm,drawer,store})`：`open(id)`、`close()`、`commitQuick()`。
- `App.Keyboard.create({document,store,list,actions})`：`handle(event)`、`destroy()`。

- [ ] **Step 1: 写事件路由和渲染失败测试**

使用最小 DOM stub 断言输入框内 Ctrl+A 不触发任务全选、完成复选框不改变选择、Esc 优先级、Alt+上下移动、搜索高亮不使用 `innerHTML`、空状态和逾期类名正确。

- [ ] **Step 2: 运行确认失败**

Run: `node tests/test-keyboard.js`

Expected: FAIL，新控制器未定义。

- [ ] **Step 3: 抽取控制器并缩减 app.js**

`app.js` 只保留启动、依赖组装、顶层订阅和跨模块协调；删除已迁移的重复函数。保持现有 DOM id 和可访问名称，避免视觉回归。

- [ ] **Step 4: 运行新测试和完整测试**

Run: `node tests/test-keyboard.js`

Expected: PASS。

Run: `node tests/test-task-list.js`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/ui/task-list.js src/js/ui/task-editor.js src/js/ui/keyboard.js src/js/app.js src/index.html tests/test-keyboard.js tests/test-task-list.js
git commit -m "refactor: split task UI controllers"
```

### Task 9: 实现 200 条以上窗口化列表和性能验收

**Requirements:** NFR-01、NFR-02、FR-86、FR-95。

**Files:**
- Create: `src/js/ui/virtual-list.js`
- Modify: `src/js/ui/task-list.js`
- Modify: `src/styles.css`
- Create: `tests/test-virtual-list.js`
- Create: `tests/benchmark-1000.js`

**Interfaces:**
- `App.VirtualList.create({count,estimateHeight,overscan,viewportHeight})`；方法 `range(scrollTop): {start,end,padTop,padBottom}`、`measure(index,height)`、`offsetFor(index)`、`ensureVisible(index,scrollTop)`。
- 200 条及以下禁用窗口化；201 条起启用，默认估算行高由 CSS 自定义属性读取，overscan 至少 5 行。

- [ ] **Step 1: 写范围、变高和焦点失败测试**

断言首屏/中段/末屏范围、变高测量后偏移修正、overscan、201 条阈值、焦点移动到未挂载项时返回正确滚动位置、跨视图选择不受挂载范围影响。

- [ ] **Step 2: 运行确认失败**

Run: `node tests/test-virtual-list.js`

Expected: FAIL，虚拟列表接口未定义。

- [ ] **Step 3: 实现窗口化并接入任务列表**

用上下占位元素保持滚动高度；测量实际行高并缓存。拖拽只允许当前挂载范围，靠近边缘时自动滚动并扩展范围。

- [ ] **Step 4: 运行正确性与性能测试**

Run: `node tests/test-virtual-list.js`

Expected: PASS。

Run: `node tests/benchmark-1000.js`

Expected: 1000 条筛选、排序和虚拟范围计算总计小于 200ms，并打印机器与原始耗时。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- src/js/ui/virtual-list.js src/js/ui/task-list.js src/styles.css tests/test-virtual-list.js tests/benchmark-1000.js
git commit -m "perf: virtualize large task lists"
```

## 阶段 D：宿主重构与发布

### Task 10: 拆分宿主设置、快捷键和命令路由

**Requirements:** FR-53、FR-62、FR-68、FR-70 至 FR-77、FR-79、NFR-03。

**Files:**
- Create: `host/settings.ps1`
- Create: `host/hotkeys.ps1`
- Create: `host/commands.ps1`
- Modify: `host/launcher.ps1`
- Modify: `tests/test-window.ps1`
- Create: `tests/test-host-commands.ps1`

**Interfaces:**
- `Read-HostSettings`、`Save-HostSettingsAtomic`、`Migrate-HostSettings`。
- `New-HotkeyTransaction -MessageWindow -Current -Requested` 返回 `{ok,conflicts,active,commit,rollback}`。
- `Invoke-ValidatedPageCommand -Command -Context` 只接受命令白名单和类型正确的 payload。

- [ ] **Step 1: 写宿主模块失败测试**

断言设置临时文件原子替换、损坏设置恢复、快捷键冲突回滚、未知命令拒绝、非法 layer/mode/geometry 拒绝、置顶置底互斥、穿透逃生能力不足时拒绝。

- [ ] **Step 2: 运行确认失败**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test-host-commands.ps1`

Expected: FAIL，模块函数未定义。

- [ ] **Step 3: 抽取模块并保持 launcher 生命周期职责**

`launcher.ps1` 仅保留启动顺序、依赖组合、消息循环和清理。模块通过返回对象报告错误，由 launcher 统一发布页面状态和托盘提示。

- [ ] **Step 4: 运行宿主及完整测试**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test-host-commands.ps1`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL；受限系统操作只允许明确 SKIP。

- [ ] **Step 5: 提交**

```powershell
git add -- host/settings.ps1 host/hotkeys.ps1 host/commands.ps1 host/launcher.ps1 tests/test-window.ps1 tests/test-host-commands.ps1
git commit -m "refactor: modularize Windows host services"
```

### Task 11: 完成托盘动态图标、通知和窗口状态验收

**Requirements:** FR-31、FR-52、FR-71 至 FR-79。

**Files:**
- Create: `host/tray.ps1`
- Modify: `host/launcher.ps1`
- Modify: `host/window.ps1`
- Modify: `src/js/host.js`
- Create: `tests/test-tray.ps1`
- Modify: `tests/test-window.ps1`

**Interfaces:**
- `New-TodoTrayIcon -PendingCount -Selectable -Dpi` 返回可释放的 HICON 和 tooltip；0–99 显示数字，100+ 显示紧凑上限状态。
- `Show-TodoNotification -Title -Body -TaskIds -AllowComplete`。
- 页面实际状态响应包含 `{layer,selectable,mode,bounds,autoStart,hotkeys,tray}`。

- [ ] **Step 1: 写图标状态和实际状态失败测试**

断言 0/1/9/10/99/100 数量文案、穿透视觉状态、HICON 释放路径、托盘菜单恢复交互、窗口命令失败后响应仍报告真实状态。

- [ ] **Step 2: 运行确认失败**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test-tray.ps1`

Expected: FAIL，托盘模块未定义。

- [ ] **Step 3: 实现托盘模块和通知 inbox 动作**

运行时绘制托盘小图标，替换后释放旧图标；生成失败降级为基础图标与 tooltip。通知点击或“完成”动作写入宿主 inbox，由页面显示并处理对应任务。

- [ ] **Step 4: 运行宿主测试和真实环境冒烟**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/test-tray.ps1`

Expected: PASS。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

在交互式 Windows 会话运行 `启动待办.bat`，记录托盘、置顶、置底、穿透、两条逃生、自启与通知的 PASS/SKIP/FAIL。

- [ ] **Step 5: 提交**

```powershell
git add -- host/tray.ps1 host/launcher.ps1 host/window.ps1 src/js/host.js tests/test-tray.ps1 tests/test-window.ps1
git commit -m "feat: complete tray and window host integration"
```

### Task 12: 发布文档、需求追踪和最终验收

**Requirements:** 全部 FR/NFR，重点 NFR-04、NFR-06 至 NFR-10。

**Files:**
- Create: `README.md`
- Create: `docs/隐私说明.md`
- Create: `docs/验收记录.md`
- Create: `tests/verify-release.ps1`
- Modify: `docs/需求文档.md` only to append implementation evidence links; do not change requirements.

**Interfaces:**
- `tests/verify-release.ps1` 检查分发体积、禁止远程 URL/第三方依赖、必需文件、测试结果和需求证据表完整性；0 表示可自动验证部分全部通过。

- [ ] **Step 1: 写发布检查并确认当前失败**

检查：总分发体积小于 5MB；源码不存在 `http://`/`https://` 运行时请求；README 包含使用、数据、卸载、故障排查；隐私文档包含无遥测/无上传；69 条 FR 和 10 条 NFR 均在验收记录中出现一次。

- [ ] **Step 2: 运行确认文档和证据缺失**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/verify-release.ps1`

Expected: FAIL，指出 README、隐私说明或验收证据缺失。

- [ ] **Step 3: 编写用户文档和需求追踪表**

验收记录每项包含状态、自动测试、人工步骤、环境和证据。无法在当前环境执行的 ARM64、Windows 10、多 DPI 项标记“待对应环境验收”，不得标记通过。

- [ ] **Step 4: 执行最终自动验证**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-all.ps1`

Expected: 0 FAIL。

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/verify-release.ps1`

Expected: 自动可验证项目全部 PASS；环境矩阵缺口明确列出且不伪装为通过。

Run: `git status --short`

Expected: 只有明确记录的用户预存变更或为空。

- [ ] **Step 5: 提交**

```powershell
git add -- README.md docs/隐私说明.md docs/验收记录.md docs/需求文档.md tests/verify-release.ps1
git commit -m "docs: complete release and acceptance documentation"
```

## 最终审查

- [ ] 对照 `docs/需求文档.md` 逐项复核 69 条 FR 与 10 条 NFR。
- [ ] 运行完整自动测试和发布检查，读取完整输出与退出码。
- [ ] 审查从设计提交到当前 HEAD 的完整 diff，修复所有 Critical 和 Important 问题。
- [ ] 对真实 Windows 环境无法执行的验收项给出明确清单，不用自动测试替代系统级结论。
- [ ] 确认没有提交用户无关文件、临时日志、测试数据、注册表导出或本机绝对路径。
