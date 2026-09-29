# 隐私说明

DesktopTodoList 是离线运行（offline）的 Windows 原生桌面应用。它不要求账户、不包含遥测（telemetry），不会上传任务（never upload task data），也不在后台检查更新。应用运行无需浏览器、网络服务或 PowerShell。应用不会自动联网；网络活动可能来自用户主动选择的导入/导出位置、Windows 本身或其他软件，不属于应用遥测。

## 本地保存的数据

当前 Windows 用户的数据目录是：

`%LOCALAPPDATA%\DesktopTodoList`

其中 `data.json` 保存待办和应用设置；`backups\YYYY-MM-DD.json` 是自动每日备份，最多保留最近 7 份。导入或重置前创建的保护副本也放在 `backups` 中。通过应用主动导出的 JSON 由用户选择保存位置。

旧原生版本若有 `state.json`，应用首次启动时会迁移其内容到 `data.json`，并保留旧文件作为恢复副本。安装版和便携版共用该用户数据目录。

## 数据控制

- 用户可通过设置中的“导出 JSON…”自行备份或迁移数据；分享导出文件会由用户主动决定。
- 卸载默认保留数据。只有在卸载时明确选择删除用户数据，才会删除上述 DesktopTodoList 数据目录。
- 要删除所有本地数据，可先退出应用，然后按需自行备份，再删除 `%LOCALAPPDATA%\DesktopTodoList`。此操作不可由应用撤销。

许可证：本项目采用 MIT License，完整条款见发行包中的 `LICENSE.txt`。
