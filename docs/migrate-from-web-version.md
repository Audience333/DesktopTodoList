# 从旧网页版本迁移

原生桌面插件不会自动读取或提取旧版 Edge 的 `localStorage`（automatic extraction is not supported; manual export required）。请在旧版本仍能打开时手动导出 JSON (export JSON)，再在原生版导入 (import JSON)；两边的数据格式和存储位置不同。

## 迁移步骤

1. 打开旧网页版本，使用其中的“导出 JSON”功能，将文件保存到容易找到的位置。保留这份原始导出文件，直到确认迁移成功。
2. 安装或解压原生 DesktopTodoList 并启动。
3. 打开应用“设置”，选择“导入 JSON…”，然后选中刚才导出的文件。
4. 在导入提示中选择：点“是”将导入任务与现有任务合并、不覆盖同 ID 的较新任务；点“否”替换全部当前数据。替换前应用会创建备份；点“取消”则不导入。
5. 检查任务标题、备注、完成状态和顺序。确认无误后再决定是否清理旧浏览器数据或原始 JSON。

导入只接受 `.json` 文件并会校验内容。导入失败时当前数据保持不变；如果发生替换，原有状态会先备份到 `%LOCALAPPDATA%\DesktopTodoList\backups`。

## 数据位置

原生版数据文件为 `%LOCALAPPDATA%\DesktopTodoList\data.json`，每日备份在同目录的 `backups` 文件夹。若旧原生测试版本曾使用 `state.json`，首次启动会将其迁移为 `data.json`，并保留旧文件作为恢复副本。

完整的数据路径和隐私说明见[隐私说明](privacy.md)。
