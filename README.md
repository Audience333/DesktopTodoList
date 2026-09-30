# DesktopTodoList

DesktopTodoList 是 Windows 原生桌面悬浮待办小插件，不是网页，也不需要浏览器或常驻联网服务。它可以置顶显示、收起到通知区域，并用全局快捷键唤出。

## 下载

在项目 GitHub Releases 页面选择与你的 Windows 设备匹配的文件：

- `DesktopTodoList-x64-Setup.exe`：Intel/AMD 64 位 Windows 安装版。
- `DesktopTodoList-arm64-Setup.exe`：Windows on ARM 设备的原生 ARM64 安装版。
- `DesktopTodoList-x64-portable.zip` 或 `DesktopTodoList-arm64-portable.zip`：对应架构的免安装便携版。

便携版解压后（extract and run）直接运行 `DesktopTodoList.exe`。安装和便携版使用同一份当前 Windows 用户数据。仓库中的 `docs/install.md` 提供安装、SmartScreen 和卸载的详细说明。

## 主要功能

- 原生 Windows 悬浮窗口，可置顶显示、收起到通知区域，并用全局快捷键显示或隐藏。
- 今天、本周、全部和已完成视图；快速添加、行内改名、搜索及批量管理任务。
- 任务详情支持备注、优先级、截止时间、标签和到期提醒。
- 设置可选择登录 Windows 时自动启动；默认关闭。安装程序还提供可选的桌面快捷方式；默认不创建。
- 任务与设置保存在本机，支持 JSON 导入导出和自动备份；离线运行，不上传任务或遥测。

各版本的修复与变更随 GitHub Release 发布；安装包签名与 SmartScreen 提示说明见 `docs/install.md`。

## 数据、隐私与迁移

待办和设置默认保存在 `%LOCALAPPDATA%\DesktopTodoList\data.json`，备份位于其 `backups` 子目录。应用离线运行，不包含遥测，也不会自动上传任务或检查更新。完整说明见仓库中的 `docs/privacy.md`。

旧网页版本的数据不会从 Edge `localStorage` 自动读取；请先在旧版本导出 JSON (export JSON)，再在原生插件的设置中导入 (import JSON)。仓库中的 `docs/migrate-from-web-version.md` 有详细步骤。

## 常用操作

- `Ctrl+Alt+T`：显示或隐藏窗口（可在设置中更改）。
- `Ctrl+Alt+L`：切换鼠标交互状态；鼠标穿透时可用它恢复操作。
- 找不到窗口时，点击通知区域中的 DesktopTodoList 图标或右键选择“显示窗口”。

仓库中的 `docs/troubleshooting.md` 提供更多故障排查步骤。程序以 MIT License 发布，许可证文本随包附带。
