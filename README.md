# VxKex NEXT (Windows 7 现代化增强分支)

> 本项目基于原作者开源项目 [YuZhouRen86/VxKex-NEXT](https://github.com/YuZhouRen86/VxKex-NEXT)（原项目 [GitHub](https://github.com/YuZhouRen86/VxKex-NEXT) | [Gitee](https://gitee.com/YuZhouRen86/VxKex-NEXT)）进行针对性维护与增强；原版的开发历史背景、多语言支持及通用基础问答请参阅原项目仓库。

---

## 一、本项目关键修改与增强内容总结

针对在 Windows 7 SP1 x64 上运行现代开发工具链（包括 **VS Code 1.107+**、**OpenAI Codex 插件**、**Claude Code CLI**、**Antigravity**、**Bun 运行时**、**Electron 44+**、**现代 PowerShell 7** 等）出现的 API 缺失、伪控制台（ConPTY）失效、VT 乱码与现代网络协议栈不兼容问题，本项目实施了以下核心修复与对齐：

### 1. 伪控制台（ConPTY）完整兼容桥接与进程管道重构
- **补齐 ConPTY 核心导出**：在 `KxBase.dll` 中完整实现并导出 `CreatePseudoConsole`、`ResizePseudoConsole`、`ClosePseudoConsole` 等 API；
- **进程创建与属性表桥接**：拦截 `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 属性列表，自动将伪控制台子进程标准输入/输出/错误句柄重定向至底层管道，深度对齐微软官方 `winconpty` 与 Wine / Rust `portable-pty` 规范；
- **零开销原生直通**：引入 `KxPseudoConsoleInitialized` 标志，未调用伪终端的应用（如 VS Code 渲染主进程、GPU 进程）执行 100% 原生直连，杜绝句柄泄漏与崩溃；
- **Chromium / Electron 句柄保护兼容**：修复 `CloseHandle` 导出拦截破坏 Chromium `ScopedHandleVerifier` 导致的白屏崩溃问题，恢复系统原生转发并采用句柄槽惰性回收机制。

### 2. 现代 AI 插件与 API 集成适配（Codex / Antigravity / Copilot 等）
- **解决 0xc0000139 符号缺失崩溃**：
  - 在 `KxNt` 中实现并导出 `IsApiSetImplemented`，解决现代 Rust / C++ 运行时在初始化阶段查询 ApiSet 导致的启动崩溃；
  - 在 `KxBase` 中实现并导出 `DeriveCapabilitySidsFromName`，支持 Windows 现代应用容器及沙箱权限派生。
- **现代化安装包与无边框 DWM 契约修复**：
  - 排查并优化 32 位（x86）NSIS 安装器向导与现代 DWM 样式（如 `DwmSetWindowAttribute` / `DwmExtendFrameIntoClientArea`）在 Win7 下的降级处理；
  - 修正 VxKex 注入时序对 `IsWindowVisible` 窗口判定造成的冲突，使 DeepSeek Harness 等现代 Electron 44+ 应用及安装引导在 Win7 下平稳运行。

### 3. 控制台虚拟终端（VT）重构与终端乱码彻底解决
- **活动屏幕缓冲区 VT 状态共享**：重构 `KxBase\console.c` 中控制台模式判定，消除虚假 VT 支持回报，实现对 PowerShell 7 / PSReadLine 中 ANSI 颜色序列（如 `←[93m`、`←[0m`）的精准截获与仿真转译；
- **紧急降级开关**：引入 `VXKEX_NO_CONSOLE_VT=1` 环境变量，允许应用一键平滑降级至 Windows 原生 Win32 控制台输入输出模式；
- **UTF-8 输出与 936 代码页共存（OpenCode 登录界面乱码修复）**：Bun 类程序启动时把控制台切到 65001 后输出 UTF-8，而为保证中文双列宽与输入法可用，终端需保持 936（GBK），两者冲突会导致 `opencode auth login` 等界面的框线与掩码显示为 `鈹?`、`鈼?`、`鈻?枲`。`KxBase` 现会记录应用通过 `SetConsoleOutputCP` 声明的代码页，仅当“应用声明 65001 而真实代码页不是 65001”时，在用户态把窄字符输出按 UTF-8 解码后以 Unicode 写入（支持跨写入拆分的多字节字符、大块输出分片），中文仍保持双列宽；其他程序（含 GBK 输出）行为不变。如需关闭，启动程序前设置 `VXKEX_NO_CONSOLE_UTF8=1`；
- **VS Code 内置终端体验优化**：排查并提供 Win7 + `winpty-agent` 架构下中文全角字符重复输入、菱形乱码（``）的全局解决方案与自动化 `$PROFILE` 配置。

### 4. 自动化构建打包体系与 Release 产出
- **纯净离线工具链适配**：彻底摆脱对庞大 Visual Studio IDE 的依赖，基于标准 Windows 7 SDK 7.1 + MSBuild 4.0 搭建全自动化构建环境；
- **一键打包脚本（`build-installer.cmd`）**：支持一键完成 x64 + Win32 全架构 Release 编译、自动化自测（ConPTY 契约校验与句柄泄漏检查）以及通过 7zSFX 自动封装成标准便携安装包 `KexSetup_Release.exe`。

---

## 二、当前 Windows 7 下的运行环境与配置说明

### 1. 基础操作系统要求
- **操作系统**：Windows 7 Service Pack 1 (SP1) 64 位（专业版 / 旗舰版 / 企业版，Build 6.1.7601）；
- **必备系统更新补丁**：
  - **KB4474419 / KB4490628**：SHA-2 代码签名支持（现代驱动、DLL 与工具运行必须）；
  - **KB2533623**：Windows 安全 DLL 搜索路径（`SetDefaultDllDirectories`）支持；
  - **KB2670838**：Windows 7 平台更新（DirectX 11.1 及现代图形渲染组件）；
  - 建议安装后续 Extended Security Updates (ESU) 补丁。

### 2. 软件运行栈与已验证版本说明

| 软件 / 组件 | 实测验证版本 | 架构与 Win7 兼容性说明 |
| :--- | :--- | :--- |
| **VS Code** | **1.107.1**（win32-x64 便携版） | 内嵌 **Electron 39.2.3**，开启 VxKex Win10 伪装后全界面与扩展主机正常运行 |
| **Node.js** | **v22.18.10**（VS Code 内置）<br>支持独立 **Node.js 18.x ~ 22.x** | 配合 KxBase ConPTY 桥接，支持伪终端子进程无损创建与管道重定向 |
| **OpenAI Codex / ChatGPT** | `openai.chatgpt-26.1007.21434-win32-x64` | ✔️ **完全兼容**。内置 `codex.exe 0.162.0-alpha.17.2`（Rust），补齐 ApiSet 与安全能力 SID 后 100% 正常运行与登录 |
| **Google Antigravity** | `google.google-antigravity-1.8.0` | ✔️ **完全兼容**。完整支持代理环境下通信与双向工作区协作 |
| **GitHub Copilot & Chat** | `github.copilot-1.388.0`<br>`github.copilot-chat-0.36.2` | ✔️ **完全兼容**。官方代码补全与对话助手，支持实时生成与流式对话 |
| **Claude Dev / Roo Code** | `saoudrizwan.claude-dev-4.1.25` | ✔️ **完全兼容**。现代自主代码生成与 Agent 插件，完全兼容 |
| **Cline CLI** | **3.0.70** | ✔️ **完全兼容**。基于伪终端的自动化 Agent CLI 工具 |
| ❌ **Claude Code CLI** | `anthropic.claude-code-2.1.301-win32-x64` | ⚠️ **无法在 Win7 下正常运行（无法修复）**。基于 Bun 编译，其本地服务与 Token 换取在 Win7 兼容层下触发底层网络 TCP RST 连接重置，纯用户态兼容层无法突破内核网络模型，建议使用 Codex / Antigravity 替代  **1101版已解决**|
| ❌ **OpenCode CLI** | `sst-dev.opencode-1.0.0` / CLI 2.0.6 | ⚠️ **无法在 Win7 下正常运行（无法修复）**。基于 Bun 编译，依赖微服务架构（`opencode serve` 监听 49374），因 Win7 AFD 异步驱动脱节导致入站 HTTP 握手后即被服务端底层掐断，产生端口死锁与崩溃 **1101版已解决**|
| **C/C++ & CMake** | `ms-vscode.cpptools-1.34.4-win32-x64`<br>`ms-vscode.cmake-tools-1.24.42` | ✔️ 原生 C/C++ 语言服务、IntelliSense、调试器与 CMake 构建集成 |
| **PowerShell 终端与扩展** | `ms-vscode.powershell-2025.4.0`<br>结合 **PowerShell 7.6.7** / 7.2.24 | ✔️ 结合活动屏幕缓冲区 VT 共享与经典控制台模式，中文无乱码 |
| **Python & Jupyter** | `ms-toolsai.jupyter-2025.9.1-win32-x64` | ✔️ 数据科学交互笔记本与 Python 内核 |
| **Java 开发环境** | `redhat.java-1.57.0-win32-x64`<br>`vscjava.vscode-gradle-3.18.1`<br>`vscjava.vscode-maven-0.46.0` | ✔️ 现代 Java Language Server 与 Gradle / Maven 构建套件 |

> [!WARNING]
> **关于 Bun 运行时缺陷及 Claude Code / OpenCode 无法在 Win7 运行的根本原因（无法修复）**（**1101版已经解决**）：
> 1. **内核等待完成包（Wait Completion Packet）缺失**：Bun / JavaScriptCore 引擎强依赖 Windows 8/10 引入的内核级系统调用 `NtCreateWaitCompletionPacket`、`NtAssociateWaitCompletionPacket` 将内核等待对象直接挂入完成端口（IOCP），Win7 原生内核不存在此系统调用（报 `0xc0000139`）；
> 2. **Winsock 内核驱动（`afd.sys`）状态机差异导致 TCP RST**：现代 Bun 底层网络库（`uSockets`/`libuv`）移除了对 Win7 AFD 异步驱动特性的兼容 Workaround。导致 Bun 的 `Bun.serve()` 服务端在 Win7 上虽能监听端口，但任何客户端完成 TCP 三次握手发送 HTTP 数据时，服务端底层会直接发出 `TCP RST` 强行掐断连接（返回 `Connection reset by peer` / `10054`）；
> 3. **纯用户态兼容层的边界**：VxKex 属于纯用户态兼容层，可补充 Win32 API 导出与版本伪装，但**无法突破操作系统内核驱动机制与 `afd.sys` 的异步完成模型**。因此，任何基于 Bun 微服务架构构建的本地命令行工具（如 Claude Code CLI、OpenCode CLI）在 Windows 7 下**根本无法修复，不能正常使用**，请务必改用基于 Node.js / Rust 构建的成熟替代方案（如 Codex、Antigravity、VS Code 官方插件）。

### 3. PowerShell 环境与依赖安装完整说明（WMF 5.1 与 PowerShell 7）

Windows 7 SP1 原生出厂仅搭载极为陈旧的 **PowerShell 2.0**，无法运行现代包管理器、模块或 VS Code 调试插件。为获得现代终端体验，需按需完成以下安装与依赖配置：

#### ① 基础层：升级至 Windows PowerShell 5.1（WMF 5.1）
若需使用系统原生 Windows PowerShell 5.1：
1. **安装 .NET Framework 4.5.2 或更高版本**（推荐直接安装 **.NET Framework 4.8 离线安装包** `NDP48-x86-x64-AllOS-ENU.exe`）；
2. **安装 Windows Management Framework 5.1**：下载并安装微软官方补丁 `Win7AndW2K8R2-KB3191566-x64.zip`；
3. 重启计算机后，原生控制台将升级至 `5.1.14409.1005`。

#### ② 现代跨平台层：安装运行 PowerShell 7.x（Core）
官方自 PowerShell 7.2 起停止正式支持 Windows 7，但**结合 VxKex NEXT 兼容层，可在 Win7 上完美运行最新的 PowerShell 7.6+**：
1. **系统运行库依赖**：
   - **SHA-2 代码签名支持**：补丁 `KB4474419`；
   - **通用 C 运行时（Universal C Runtime, UCRT）**：补丁 `KB2999226`（或直接安装 Visual C++ 2015-2022 Redistributable x64，即 `vc_redist.x64.exe`）；
2. **启用 VxKex 兼容伪装**：
   - 提取或安装 PowerShell 7（如 `C:\Program Files\PowerShell\7\pwsh.exe`）；
   - 右键点击 `pwsh.exe` ->「属性」-> 切换到 **VxKex** 选项卡 -> 勾选“为此程序启用 VxKex NEXT”，伪装版本选择 **Windows 10**。

#### ③ 模块管理与在线安装依赖（PSGallery / Install-Module）
由于 Win7 默认仅协商旧版 SSL 3.0 / TLS 1.0，执行 `Install-Module` 时会因协议不匹配导致连接被拒绝（`Unable to resolve package source`）。需配置 TLS 1.2 支持：
1. **强制启用 TLS 1.2 传输**：
   ```powershell
   [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
   ```
2. **安装并更新 NuGet Provider 与 PowerShellGet**：
   ```powershell
   # 安装现代 NuGet 引导程序
   Install-PackageProvider -Name NuGet -MinimumVersion 2.8.5.201 -Force
   
   # 更新 PowerShellGet 与 PackageManagement
   Install-Module -Name PowerShellGet -Force -AllowClobber
   ```

### 4. 现代 TLS 证书与网络环境配置（重要）
Win7 原生系统根证书库停滞于 2011 年左右，缺少现代云端基础设施（Cloudflare、OpenAI、GitHub、Anthropic）所依赖的 ECC 现代根证书（如 GlobalSign ECC R4、Google GTS 等），会导致 Rust（`reqwest`/`rustls`）、Python、Node 等现代程序报 `TLS handshake failed` 或 `0x80092013 CRYPT_E_REVOCATION_OFFLINE`。

**推荐配置方案**：
1. **全局指定现代根证书**（以 Git for Windows 自带的证书包为例）：
   ```powershell
   [System.Environment]::SetEnvironmentVariable("SSL_CERT_FILE", "C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt", "User")
   ```
2. **关闭网络证书吊销超时误判**：
   ```powershell
   Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings" -Name "CertificateRevocation" -Value 0
   ```
3. **修复本地代理例外白名单（杜绝本地回调死循环）**：
   若本机开启了代理（如端口 `10809`），为防止 VS Code / Codex / Claude 登录时的 OAuth 本地回调（如 `http://localhost:<port>/callback`）被误转发至远端代理，需确保设置代理白名单：
   ```powershell
   Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings" -Name "ProxyOverride" -Value "localhost;127.0.0.1;<local>"
   ```

### 5. VS Code 内置终端 PowerShell 7 中文优化
针对 Win7 下 `winpty` 对 PSReadLine VT 序列处理引发的中文乱码和重复问题，建议在 PowerShell 7 的全局 `$PROFILE` 中禁用该模块：
```powershell
Remove-Module PSReadLine -ErrorAction SilentlyContinue
```

---

## 三、安装与使用

1. 从 [Releases 页面](../../releases) 下载最新的安装包：
   - **`KexSetup_Release.exe`**
   - SHA256: `E89EC8B81D1853BC405DA517C429349DD0E0B1F3405BF7E490D767E925832E43`（2026-11-14 构建，含 OpenCode 2.3.0 渲染库所需的 `NtSetInformationVirtualMemory` 导出）
2. 运行安装程序完成 VxKex NEXT 安装与扩展 DLL 注册；
3. 右键点击目标现代应用程序（如 `Code.exe`、`codex.exe` 等），在「属性」中切换到 **VxKex** 选项卡，勾选“为此程序启用 VxKex NEXT”，并选择伪装为 **Windows 10** 即可。
