## VxKex NEXT 1.2.3.2464 - Windows 7 现代开发环境增强版

本项目基于原作者开源项目 [YuZhouRen86/VxKex-NEXT](https://github.com/YuZhouRen86/VxKex-NEXT) 进行针对性维护与增强，重点解决在 Windows 7 SP1 x64 上运行现代开发工具链与 AI 插件（VS Code 1.107+、OpenAI Codex、Claude Code、Antigravity、PowerShell 7 等）时的兼容性与协议栈问题。

### 核心更新内容

1. **伪控制台（ConPTY）完整兼容桥接与进程管道重构**
   - 补齐 ConPTY 核心导出（`CreatePseudoConsole`、`ResizePseudoConsole`、`ClosePseudoConsole`）；
   - 拦截 `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 属性，子进程标准 I/O 自动无缝挂载管道，对齐微软官方 winconpty 与 Rust portable-pty 契约；
   - 增加快路径标志，未启用 ConPTY 的常规进程零开销直连，修复 Chromium `ScopedHandleVerifier` 句柄保护导致的白屏问题。

2. **现代 AI 插件与 API 兼容（Codex / Claude Code / Antigravity）**
   - 在 `KxNt` 中补齐 `IsApiSetImplemented`，解决 Rust / C++ 运行时初始化查询 ApiSet 导致的 `0xc0000139` 崩溃；
   - 在 `KxBase` 中补齐 `DeriveCapabilitySidsFromName` 导出与实现；
   - 优化 32 位（x86）NSIS 安装器在 Win7 下的 DWM 降级与窗口判定时序，支持 Electron 44+ / 现代应用平稳运行。

3. **控制台虚拟终端（VT）重构与终端乱码彻底解决**
   - 修复活动屏幕缓冲区模式共享，精准截获并仿真转译 PowerShell 7 / PSReadLine VT 颜色转义序列（消除 `←[93m` 乱码与输入变形）；
   - 引入 `VXKEX_NO_CONSOLE_VT=1` 环境变量应急开关，支持原生控制台平滑降级。

4. **全自动化构建打包**
   - 纯净 Win7 SDK 7.1 + MSBuild 4.0 自动化构建双架构产物，并通过 7zSFX 生成标准安装包。

---

### 安装包校验信息 (Checksum)

- **文件名**：`KexSetup_Release.exe`
- **文件大小**：`5,030,365 字节`
- **SHA-256**：`CA7CD2A6DB5EB305DA14BE43957F126ECC6463CD0660A86D86FEBAF6F1A2B0BA`
