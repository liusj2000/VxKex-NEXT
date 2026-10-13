# Cline CLI 在 Win7 上使用 VxKex 启动失败排查

## 结论

2026-10-12 检查确认：本机 Cline CLI 3.0.70 的 Windows x64 可执行文件静态导入了 `ClosePseudoConsole`，但当前 VxKex NEXT 1.2.3.2463 的 64 位 `KxBase.dll` 没有该导出，导致加载阶段报 `STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139)`。

这是当前已确认的第一个启动阻断点，不代表补齐该接口后就能完整运行 Cline。未验证其他 VxKex 版本是否能解决问题。

## 检查对象与环境

- 系统：Windows 7 SP1，版本 `6.1.7601`；现有日志确认系统与程序均为 64 位。
- Cline：两个本地 npm 包的 `package.json` 均显示版本 `3.0.70`。
- 程序：`D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\cline\node_modules\@cline\cli-windows-x64\bin\cline.exe`。
- VxKex：日志显示 `VxKex NEXT 1.2.3.2463 (Release)`。
- 兼容库：`C:\Program Files\VxKex\Kex64\KxBase.dll`。
- 注册表核查：该程序完整路径已有 VxKex IFEO 配置，`KEX_WinVerSpoof=4`；日志确认已应用 Windows 10 版本伪装。

## 证据一：已有失败日志

原始日志：

```text
C:\Users\liusj\AppData\Local\VxKex\Logs\cline-00134362879912470793-324.vxl
```

该日志记录的是目标程序带 `--version` 参数启动，而不是进入交互会话后的失败。关键内容：

```text
Successfully initialized DLL rewrite subsystem.
Applying Windows version spoof: Windows 10
Rewrote DLL import: KERNEL32.dll -> kxbase
ErrorStatus: STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139)
StringParameter1: "ClosePseudoConsole"
StringParameter2: "kxbase"
```

因此 VxKex 已成功介入启动流程；故障不能归因为“没有启用 VxKex”或“没有设置 Windows 10 伪装”。

## 证据二：静态 PE 导入与导出核验

直接读取 EXE 与 DLL 的 PE 表，得到：

```text
CLINE IMPORTS
ntdll.dll : RtlWaitOnAddress
api-ms-win-core-synch-l1-2-0.dll : WaitOnAddress
KERNEL32.dll : ClosePseudoConsole
KERNEL32.dll : CreatePseudoConsole
KERNEL32.dll : ResizePseudoConsole
KXBASE PSEUDOCONSOLE EXPORTS
EXPORT TOTAL: 1925
CLOSE EXPORTED: False
```

本机 `KxBase.dll` 的导出名称中未匹配到 `PseudoConsole`。其中 `ClosePseudoConsole` 缺失既有静态核验，也有运行失败日志佐证；另外两个接口的运行时失败尚未逐一验证。`WaitOnAddress` 等导入只作记录，本次没有把它们判定为故障原因。

## 为什么 Node 能运行不代表 Cline 能运行

本地启动脚本：

```text
D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\cline\bin\cline
```

脚本说明平台专用二进制内嵌 Bun，并通过 `childProcess.spawnSync()` 启动该二进制。Node.js 在这里负责定位和启动 EXE，不是该 EXE 内部的执行引擎。因此，Node 能通过兼容层运行，不能证明独立的 Cline EXE 也满足兼容条件。

当前已确认的失败链：

```text
启动 cline.exe
  → VxKex 注入并重写 Kernel32 导入为 KxBase
  → 加载器解析 ClosePseudoConsole
  → 本机 KxBase 缺少该导出
  → 0xc0000139，CLI 业务逻辑尚未开始
```

## 处理方向与边界

1. 仅修改 Windows 版本伪装不能补齐缺失导出；当前日志已经确认 Windows 10 伪装成功。
2. 重装 Node/npm 不针对已确认的独立 EXE 加载故障，不作为本次问题的直接修复方案。
3. 如果必须留在 Win7，应进一步评估支持这组伪控制台接口的兼容层或程序构建方案。当前没有已验证可用的替代 VxKex/Cline 版本。
4. 可考虑在较新的 Windows 环境运行，但本次未做该环境下的启动或功能验收。
5. 不建议替换系统 `kernel32.dll` 或用空实现掩盖缺失接口；即使通过加载，伪控制台相关功能仍可能失败。

## 本次操作与证据保存

本次检查未修改 Cline 二进制、VxKex DLL、注册表兼容设置或项目代码；未重新启动交互式 CLI。结论来自已有日志和静态 PE 核验，不宣称已经修复或通过运行验收。

提取结果已保存并回读确认：

```text
D:\project\VxKex-NEXT\docx\cline-vxkex-log-extract.txt
D:\project\VxKex-NEXT\docx\cline-vxkex-pe-check.txt
```

上述提取结果及源码、环境核查输出已迁入本仓库的 `docx` 目录，本文件同时保留关键内容。后续若更换兼容层或程序版本，需重新核验导出并执行 `--version`、`--help` 和实际功能测试，不能沿用本次版本的结论宣称新版本通过。

## Fork 源码核查与后续方案

用户提供的 fork 已克隆至 `D:\project\VxKex-NEXT`。本地 `git rev-parse HEAD` 返回 `f98d85759c0e5c40051d5a28339fee1a2bfb2e8d`，同次 `git status --short` 没有改动输出。尚未执行 Git 全对象完整性检查。

已检查以下文件：

- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\console.c`
- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbase.def`
- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbase.vcxproj`
- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbasep.h`
- `D:\project\VxKex-NEXT\00-Common Headers\KxBase.h`
- `D:\project\VxKex-NEXT\.github\workflows\build.yml`

在公共头文件、扩展 DLL 和测试目录的 `.c`、`.h`、`.def` 文件中搜索 `PseudoConsole`、`HPCON`、`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`，没有匹配。现有 `console.c` 提供 ANSI 输出与控制台模式、标题兼容，不能据此宣称已经存在可复用的完整 ConPTY 后端。

### 最小启动验证补丁的边界

可先设计补齐三个接口的导出和正确 ABI，以解除加载器的当前缺失入口阻断；没有真实后端时创建接口必须明确返回失败，不返回成功或假句柄。之后验证 `--version`、`--help` 是否不依赖伪控制台，以及实际调用方能否处理失败或回退。

该方案仅为待实施实验，不是完整 ConPTY 修复。完整兼容还要覆盖伪控制台进程属性、子进程创建、输入输出及生命周期。接口失败码、参数检查、句柄语义应在实现前核对官方契约与调用方源码。

### 本机构建环境阻断

当前 Win7 主机的路径核验结果：

```text
不存在：C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd
不存在：C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe
存在：C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe
存在：C:\Qt\Qt5.12.12\Tools\mingw730_64\bin\gcc.exe
```

这只是指定路径检查，不等于排除其他位置的工具链。仓库 CI 明确准备 Windows 7 SDK，并设置 `VxKex_NEXT_Platform_Toolset=Windows7.1SDK` 后运行构建脚本；仅存在 .NET MSBuild 或 MinGW 不代表具备该构建链。

下一步：核查本机其他工具链路径或准备独立构建环境，再核对接口契约与调用方行为，实施最小补丁并补测试。本轮未修改 fork 源码、未编译产物、未运行新 DLL，也未更改本机 VxKex 安装。不得宣称已修复。