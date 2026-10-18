# VxKex NEXT：AI 助手工作约定
## ⚠️ 下一次会话任务2026-10-18，开始前必读
2026-10-13记录是针对于cline的，已经完成。
当前工程最新工程是基于antigravity插件完成，codex插件也可以使用。
##  下一次会话的首要任务（2026-10-13 记录，开始前必读）

- **下一次工作 = 验证 KxBase 伪控制台（ConPTY）实现的正确性**；不是继续下载/安装工具链（那部分已完成）。
- 测试计划已写好：[ConPTY 正确性测试计划](docx/ConPTY正确性测试计划-20261013.md)。开始动手前先读它，再读 `docx\当前工作.md`。
- 下一次会话很可能在 **VS Code + Cline 插件**中启动（不是 DSH Desktop）。本文件与仓库根 `.clinerules` 就是入口；请先把本节任务复述给用户确认，再开始执行。
- 前置阻断（都需要用户明确安排）：新 `KxBase.dll` 尚未部署，已安装的 `C:\Program Files\VxKex\Kex64\KxBase.dll` 仍是缺 `ClosePseudoConsole` 的旧版；部署前必须备份 + 记录 SHA256 + 准备回滚。
- 编译任何测试程序都要加 `/p:TrackFileAccess=false`（串行 `/m:1`），否则会被本机 VxKex 的导入重写缺陷误伤，见下文「已知阻断」。
- 提交状态：上一轮改动已入库——`a38b938 feat: KxBase 增加伪控制台 API 与 CreateProcess 桥接`、`9dc7fb2 chore: 忽略各工程 x64/Win32 构建输出`，本文件 / `.clinerules` / `docx/` 随后以 `docs:` 提交。提交身份仅配置在本仓库：`liusj <liusj2000@users.noreply.github.com>`。

## 工作范围与会话恢复

- 本仓库根目录：`D:\project\VxKex-NEXT`。Cline/VxKex 的分析、设计、测试和交接资料统一保存在 `D:\project\VxKex-NEXT\docx`。
- 开始新会话先阅读 `D:\project\VxKex-NEXT\docx\当前工作.md`，再阅读其链接的排查文档。
- 用户说“继续上次工作”时从该固定入口恢复，不要求用户记住具体交接文件名。
- 恢复前核查 Git 状态、HEAD 和实际工具链；区分历史证据、未提交内容和本次实际测试，不仅凭旧文档宣称通过。
- 暂停或结束前更新恢复入口，记录完成状态、下一步、风险及未完成测试。
- 不修改、构建或提交 `D:\project\<external-project>` 的文件，不把本任务资料写入那个项目，也不影响其虚拟串口、驱动或验收工作。

## 从原项目 AGENTS.md 迁入的通用规则

- 先设计再编码，设计资料保存在本仓库 `docx` 下；修改前先阅读相关源码、工程文件和构建脚本。
- 遵守当前仓库的 C 代码、命名、ABI、导出及旧 SDK 约定，不套用原项目 Qt/CMake、分层架构、gtest 或驱动验收规则。
- 新增文件必须同步相关工程文件；测试使用本仓库已确认的方式，不凭空引入框架。
- 修改后回读核验文件，尽可能实际构建并测试；无法执行时明确记录阻断，不能把静态检查写成运行通过。
- Git 提交前检查差异，不覆盖用户改动、不使用破坏性清理、不提交无关文件。提交前缀可使用 `feat:`、`fix:`、`refactor:`、`docs:`。
- 所有文件定位和执行说明优先使用绝对路径。

## 构建工具链与编译方式

本机即目标环境：Windows 7 专业版 SP1 x64（6.1.7601）。工具链已按项目 CI（`.github/workflows/build.yml`）流程安装并实测可用，**不需要 Visual Studio**：工程的 `.vcxproj` 都是 `ToolsVersion="4.0"`，构建靠 MSBuild 4.0 + Microsoft.Cpp targets + VC10 编译器 + Windows SDK 7.1。

### 工具与安装路径

- 下载留存（均已校验哈希，未运行任何 SDK 安装器）：
  - 官方介质：`D:\tools\Win7SDK\GRMSDKX_EN_DVD.iso`（SHA1 `9203529F5F70D556A60C37F118A95214E6D10B5A`），解压媒体 `D:\tools\Win7SDK\media`。
  - 项目 CI 用包：`D:\tools\Win7SDK\Win7SDK-260730.zip`（SHA256 `8d90703130c4bc735fa215ee14cddf868e27c156b72065c434ec747d5f120d44`，与 build.yml 一致），解压目录 `D:\tools\Win7SDK\Win7SDK-260730`。
- 已安装的 C 盘关键路径：
  - SDK 环境脚本：`C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd`
  - MSBuild C++ targets：`C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\Microsoft.Cpp.Default.props`（可用 toolsets：`v90`、`v100`、`Windows7.1SDK`）
  - VC10 编译器：`C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\cl.exe`、`...\VC\bin\amd64\cl.exe`、`...\VC\bin\x86_amd64\cl.exe`
  - GAC 工具：`C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\NETFX 4.0 Tools\x64\gacutil.exe`
  - MSBuild 引擎：`C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe`（4.0.30319.34209）
- 安装/回滚脚本（均在 `D:\tools\Win7SDK`）：`install-cdrive.ps1`（管理员；复制 + `reg import Registry\W7SDK.reg` + `gacutil /i` 4 个 CPPTasks）；`rollback-cdrive-install.ps1 -DryRun`（正式回滚需管理员）。备份与清单：`D:\tools\Win7SDK\cdrive-backup-20261013-225229\install-plan.json`。
- 旧路径说明：上一轮记录的 `prepare-win7-sdk.ps1`、`install-win7-sdk.ps1`（官方 setup 路线）仍在 `D:\tools\Win7SDK`，本轮未运行。

### 编译命令

必须显式加 `/p:TrackFileAccess=false`（原因见下），建议串行 `/m:1`：

```bat
call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x64
set VxKex_NEXT_Platform_Toolset=Windows7.1SDK
MSBuild.exe "D:\project\VxKex-NEXT\VxKex.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1
```

- 可直接复用的脚本：`D:\tools\Win7SDK\build-notrack-serial.cmd`（x64 Release）；改 `SetEnv.cmd /x86` 与 `/p:Platform=Win32` 即 Win32 构建。
- 项目自带 `01-Development Utilities\Build_Stable_Version_And_Clean.bat` 未传 `TrackFileAccess`，当前在本机会被 VxKex 打断；使用它之前需先处理该阻断或改造调用方式。
- 构建产物在 `D:\project\VxKex-NEXT\x64\Release\`、各工程 `x64\` 下，属生成物，不提交。

### 已知阻断：VxKex 与 MSBuild file tracker 冲突

- 本机安装的 VxKex NEXT 1.2.3.2463 会在 MSBuild 经 file tracker（Detours 注入）启动 `CL.exe` 时导入表重写失败：`STATUS_ACCESS_VIOLATION (0xc0000005)`，表现为模态框 "VxKex NEXT could not start because the DLL imports of the main process image could not be rewritten" 与 `MSB6006: "CL.exe" exited with code -536870909`。
- 观测：MSBuild 启动的 `CL.exe` 6/6 失败，手动启动的 9/9 成功；`cmd.exe`/`MSBuild.exe`/`CL.exe` 本身无 IFEO 配置，是被全局 `{VxKexPropagationVirtualKey}` 传播注入的。
- 绕开：`/p:TrackFileAccess=false`；并行 `/m` 仍会撞 `LNK1104: cannot open file 'KexDll.lib'` 的链接竞争，用 `/m:1` 或重跑。
- 细节与证据：`docx\编译工具下载落地-20261013.md`。修复 VxKex 该缺陷属独立任务，未开始。

## Shell、编码与工具约定

- 默认使用 PowerShell 7：`pwsh -NoProfile [-ExecutionPolicy Bypass] -File <脚本>`。本机本次终端显示 7.2.24；不沿用另一台环境的 7.6.6 或 VS2019 已安装声明。
- `.ps1` 尽量保持 Windows PowerShell 5.1 兜底兼容；含中文使用 UTF-8 with BOM，无 BOM 脚本保持纯 ASCII。
- 原生程序参数中的表达式先求值再双引号插值；复杂执行逻辑写入脚本，不用多层嵌套 `-Command`。
- 源文件编码、缩进与声明风格遵循本仓库现有文件，不批量转换既有文件。
- 历史 Python 路径：`D:\project\claude-code-engineering\python-3.11.9-embed-amd64`；需要使用时先确认存在和可运行。
- 终端工具多次报告无法观察完成或捕获 shell integration 输出；其 `Command exited with code 1` 不一定是子命令真实退出码。执行需记录真实退出码、输出到文件并回读验证。不能据此归因于 OpenAI API 故障。

## 项目与当前任务

- 用户 fork 的本地仓库在本目录；历史检查 HEAD：`f98d85759c0e5c40051d5a28339fee1a2bfb2e8d`。
- 解决目标：Win7 SP1 x64 上 Cline CLI 3.0.70 在当前 VxKex NEXT 1.2.3.2463 下的加载失败。
- 已确认首个阻断：Cline 静态导入 `ClosePseudoConsole`，当前安装的 `KxBase.dll` 缺少导出，已有 `--version` 失败日志报 `0xc0000139`。
- 相关源码位于 `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase`，公共头文件在 `D:\project\VxKex-NEXT\00-Common Headers`。
- CI 文件为 `D:\project\VxKex-NEXT\.github\workflows\build.yml`，其中准备 Windows 7 SDK 并使用 `Windows7.1SDK` 工具链。不能假定只安装 .NET MSBuild 或 MinGW 即可构建。
- 工具链已按上述路径装好并实测编译通过；x64 Release 版 `KxBase.dll` 已产出（含 `ClosePseudoConsole` 等导出），但**未部署**，已安装的 `C:\Program Files\VxKex\Kex64\KxBase.dll` 仍是缺导出的旧版。

## 安全与验收边界

- 未经用户明确安排，不替换系统 DLL，不覆盖当前安装的 VxKex DLL，不修改 IFEO/版本伪装设置，不运行安装器或改变系统策略。
- 新产物使用独立输出目录，部署前准备备份、哈希核验和回滚方案；提权须由用户处理。
- 补齐导出不等于完整 ConPTY 实现；没有后端时不能返回成功或假句柄。
- 实现前核对官方接口契约和实际调用方行为；之后分阶段验证 ABI/导出、构建、`--version`、`--help`、交互及子进程功能。
- 现状：KxBase 伪控制台补丁源码已实施并编译通过（x64 Release），**未部署、未做 Cline 运行验证**；后续恢复须再次核实。
