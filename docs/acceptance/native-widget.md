# Native desktop widget acceptance report

- Run: 2026-09-30 14:23:01 +08:00
- OS: Windows 10 Pro build 26200.9457
- Architecture: X64
- System DPI: 96
- Monitors: 2
- Configuration: Debug
- Build directory: E:\DesktopTodoList\out\build\release-2.0.2-x64
- Existing DesktopTodoList process: none
- Result: 18 PASS, 0 FAIL, 8 SKIP

| Status | Capability | Evidence / reason |
|---|---|---|
| PASS | 完整原生自动化测试 | CTest 全量套件通过。 |
| PASS | 单实例及二次启动协议 | 覆盖测试：native.single_instance |
| PASS | 任务增删改、撤销与四种视图 | 覆盖测试：native.task_store, native.task_query, native.app_service |
| PASS | 编辑、搜索和原生输入 | 覆盖测试：native.editing, native.editor_host_smoke |
| PASS | 多选、框选和排序交互 | 覆盖测试：native.pointer_controller, native.selection_toolbar |
| PASS | 导入、导出和重置 | 覆盖测试：native.import_export, native.data_transfer_dialog |
| PASS | 设置与事务回滚 | 覆盖测试：native.settings_panel |
| PASS | 通知栏和托盘菜单模型 | 覆盖测试：native.tray_menu |
| PASS | 全局快捷键 | 覆盖测试：native.hotkey_service |
| PASS | 窗口层级与点击穿透恢复 | 覆盖测试：native.window_behavior |
| PASS | 提醒通知及失败重试 | 覆盖测试：native.notification_service |
| PASS | 当前用户自启动注册逻辑 | 覆盖测试：native.autostart_service |
| PASS | 无障碍语义和窗口 UIA 接口 | 覆盖测试：native.accessibility, native.widget_window_smoke |
| PASS | 布局、DPI 缩放和屏幕边界模型 | 覆盖测试：native.layout, native.renderer_state |
| PASS | 持久化、退出刷新逻辑 | 覆盖测试：native.state_repository, native.app_service |
| PASS | 1000 项任务虚拟化与渲染性能 | 覆盖测试：native.task_list_view, native.renderer_smoke |
| PASS | 本机状态载入、导入和强制落盘 | --verify-core 返回 0，用时 1,024 ms。 |
| PASS | 运行过程未创建浏览器、WebView、localhost 或 PowerShell 宿主 | 核心验收前后未出现这些新进程，且原生运行路径未调用 WebView、浏览器/脚本子进程或 localhost 服务入口。 |
| SKIP | 冷启动耗时（≤1 秒）及空闲内存（<120 MB） | 本次自动验收不启动交互式 GUI；需在专用桌面会话手动执行相应检查。 |
| SKIP | 真实双开时只保留一个窗口并激活既有实例 | 本次自动验收不启动交互式 GUI；需在专用桌面会话手动执行相应检查。 |
| SKIP | 真实托盘图标、快捷键、Explorer 重启及点击穿透恢复 | 本次自动验收不启动交互式 GUI；需在专用桌面会话手动执行相应检查。 |
| SKIP | Windows 通知中心 Toast 实际呈现与托盘气泡回退 | 本次自动验收不启动交互式 GUI；需在专用桌面会话手动执行相应检查。 |
| SKIP | HKCU\Run 实际写入/删除 | 自动测试使用注入接口，不改动当前用户的启动项；需要专用账户做交互验证。 |
| SKIP | Narrator / Inspect 屏幕阅读器实测 | 本次自动验收不启动交互式 GUI；需在专用桌面会话手动执行相应检查。 |
| SKIP | 真实 DPI、多显示器拔插与窗口恢复 | 布局和缩放模型有自动测试；本次未改变显示设置或拔插显示器。 |
| SKIP | 真实文件对话框、拖放、关窗留托盘和退出落盘 | 本次自动验收不启动交互式 GUI；需在专用桌面会话手动执行相应检查。 |

SKIP items are environment-limited or intentionally avoid modifying the active user session; they are not counted as passes.