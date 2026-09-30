# 安装与首次使用

## 选择下载文件

在项目的 GitHub Releases 页面下载对应 Windows 架构的安装程序或便携压缩包：

| 设备 | 安装版 | 便携版 |
| --- | --- | --- |
| Intel / AMD 64 位 Windows（x64） | `DesktopTodoList-x64-Setup.exe` | `DesktopTodoList-x64-portable.zip` |
| Windows on ARM（ARM64） | `DesktopTodoList-arm64-Setup.exe` | `DesktopTodoList-arm64-portable.zip` |

在 Windows“设置 → 系统 → 系统信息”中查看“系统类型”。普通 Intel/AMD 电脑选 x64；Windows on ARM 设备选 ARM64。不要只按处理器品牌猜测系统架构。

## 安装版

1. 双击对应的 `Setup.exe`。
2. 安装程序只为当前 Windows 用户安装到 `%LOCALAPPDATA%\Programs\DesktopTodoList`，不需要管理员权限。
3. 安装时可选是否创建桌面快捷方式；默认不勾选。完成后可从开始菜单启动。登录 Windows 自动启动默认关闭，可在应用“设置”中开启。

请以该版本发布页 `checksums.txt` 的 `Signature status` 为准：`valid` 表示应用程序和安装程序通过 Authenticode 签名验证；`unsigned` 表示未签名，Windows 可能显示“未知发布者”或 SmartScreen 安全提示。本版本 v2.0.2 因未提供签名证书，按批准以未签名形式发布。即使签名有效，新证书或下载量较少的版本仍可能暂时显示 SmartScreen 提示；签名也不保证信誉立即建立。请确认文件来自项目 GitHub Releases 并核对 SHA-256 校验值；不要运行来源不明的副本。

## 便携版

下载与设备架构匹配的 `portable.zip`，将压缩包完整解压到你有写入权限且希望长期保留的位置（例如个人文件夹），然后运行其中的 `DesktopTodoList.exe`。解压即用 (extract the archive and run)，不会创建开始菜单安装项；删除程序文件不会自动删除用户数据。便携版与安装版共用当前用户的数据目录。

## 卸载与用户数据

通过 Windows“设置 → 应用”或开始菜单中的卸载快捷方式卸载安装版 (uninstall)。卸载默认保留待办、设置和备份 (keep user data by default)，方便之后重新安装或改用便携版。交互式卸载时如明确选择删除用户数据 (remove user data)，才会删除 `%LOCALAPPDATA%\DesktopTodoList`；请先导出或备份重要数据。便携版没有卸载器，删除其解压目录即可，但用户数据仍保留。

数据位置、备份与网络行为见[隐私说明](privacy.md)；从旧网页版本迁移见[迁移指南](migrate-from-web-version.md)。
