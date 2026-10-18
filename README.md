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

### 2. 现代 AI 插件与 API 集成适配（Codex / Claude Code / Antigravity）
- **解决 0xc0000139 符号缺失崩溃**：
  - 在 `KxNt` 中实现并导出 `IsApiSetImplemented`，解决现代 Rust / C++ 运行时在初始化阶段查询 ApiSet 导致的启动崩溃；
  - 在 `KxBase` 中实现并导出 `DeriveCapabilitySidsFromName`，支持 Windows 现代应用容器及沙箱权限派生。
- **现代化安装包与无边框 DWM 契约修复**：
  - 排查并优化 32 位（x86）NSIS 安装器向导与现代 DWM 样式（如 `DwmSetWindowAttribute` / `DwmExtendFrameIntoClientArea`）在 Win7 下的降级处理；
  - 修正 VxKex 注入时序对 `IsWindowVisible` 窗口判定造成的冲突，使 DeepSeek Harness 等现代 Electron 44+ 应用及安装引导在 Win7 下平稳运行。

### 3. 控制台虚拟终端（VT）重构与终端乱码彻底解决
- **活动屏幕缓冲区 VT 状态共享**：重构 `KxBase\console.c` 中控制台模式判定，消除虚假 VT 支持回报，实现对 PowerShell 7 / PSReadLine 中 ANSI 颜色序列（如 `←[93m`、`←[0m`）的精准截获与仿真转译；
- **紧急降级开关**：引入 `VXKEX_NO_CONSOLE_VT=1` 环境变量，允许应用一键平滑降级至 Windows 原生 Win32 控制台输入输出模式；
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

### 2. 现代 TLS 证书与网络环境配置（重要）
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
   若本机开启了科学代理（如端口 `10809`），为防止 VS Code / Codex / Claude 登录时的 OAuth 本地回调（如 `http://localhost:<port>/callback`）被误转发至远端代理，需确保设置代理白名单：
   ```powershell
   Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings" -Name "ProxyOverride" -Value "localhost;127.0.0.1;<local>"
   ```

### 3. VS Code 内置终端 PowerShell 7 中文优化
针对 Win7 下 `winpty` 对 PSReadLine VT 序列处理引发的中文乱码和重复问题，建议在 PowerShell 7 的全局 `$PROFILE` 中禁用该模块：
```powershell
Remove-Module PSReadLine -ErrorAction SilentlyContinue
```

---

## 三、安装与使用

1. 从 [Releases 页面](../../releases) 下载最新的安装包：
   - **`KexSetup_Release.exe`**
   - SHA256: `CA7CD2A6DB5EB305DA14BE43957F126ECC6463CD0660A86D86FEBAF6F1A2B0BA`
2. 运行安装程序完成 VxKex NEXT 安装与扩展 DLL 注册；
3. 右键点击目标现代应用程序（如 `Code.exe`、`codex.exe` 等），在「属性」中切换到 **VxKex** 选项卡，勾选“为此程序启用 VxKex NEXT”，并选择伪装为 **Windows 10** 即可。
