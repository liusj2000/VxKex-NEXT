# Claude Code 新模型与 Bun ENOTSUP 修复可行性复核（2026-11-01）

## 结论

针对本机 Win7 + VxKex，目前没有可直接部署、已经验证能让新版 Claude Code 使用 Opus 5.5 的补丁。官方纯 JavaScript / Node CLI 停留在 2.1.112；当前已安装该版。新版原生 CLI 的 ENOTSUP 仍有继续定位和修复的可能，但具体失败调用尚未确定，不能保证修复成功，也不能认定纯用户态一定无法修复。

这是文档、原始实验产物与源码复核，本轮未重新运行网络复现、登录或模型请求，未实施 DLL 修复。

## 本轮核验的现场

- Git HEAD：`96d5992656ac9ff676a152c9ab183c481a9265a6`。
- 开始时存在用户改动：`docx/Antigravity启动失败修复设计-20261101.md` 已修改，`.claude/` 未跟踪；本轮保留。
- 实际 PowerShell 为 7.6.7；SDK SetEnv.cmd 与 MSBuild 4.0 指定路径存在，未运行编译。
- 当前正式包 `D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@anthropic-ai\claude-code\package.json` 为 2.1.112，入口 `cli.js`。
- 当前 `cli.js` SHA256：`BC3358282800E3E99DAA8E71AC5B7B1566BD0D7CA7EB94F714A7859365D3163F`，与原替换记录一致。
- 扩展登记版本为 Claude Code 2.1.314、Antigravity 1.10.0。版本差不能单独证明 stdio 协议兼容或不兼容，需要实际功能验收。
- 原生 2.1.316 的 `claude-code/bin/claude.exe` 和 `retired-package/bin/claude.exe` 备份仍在 `.tools/claude-original-2.1.316-20261101-123638` 下；本轮仅检查存在，未重新核验完整备份哈希。
- 沙箱内 PowerShell/CMD 均启动失败，返回 0xC0000142；沙箱外只读检查成功。这是工具执行条件，不是 Claude ENOTSUP 的证据。

## 记录中已经证明的内容

依据 [11 月 1 日 ENOTSUP 排查](ClaudeCode-ENOTSUP排查-20261101.md)、[原安装切换记录](ClaudeCode从Bun切换Node安装操作记录-20261101.md) 以及留存的 `claude-network-probe-20261101/summary.json` 和 `local-proxy/output.txt`：

1. 原生 CLI 2.1.316 经实际代理和临时本机 HTTP 代理均报 ENOTSUP。
2. 本机临时代理统计是 2 次 TCP 连接、0 次收到请求数据；同一监听的普通 Node HTTP 对照能正常收发。
3. 原始控制台明确显示 `Failed to connect to api.anthropic.com: ENOTSUP`。
4. 以上把故障范围收窄到了原生 CLI 在 HTTP CONNECT 发出前的本机运行时/套接字路径；没有锁定某个 API、参数或内核组件。
5. Node 2.1.112 的历史验收仅包括版本、帮助和欢迎/主题界面；记录中没有联网模型请求通过证据。

## 不能沿用的旧结论

[10 月 18 日 Bun 分析](Bun运行时在Win7兼容层下的网络实现缺陷分析及建议-20261018.md) 中的下列说法不能视为本次已证实根因：

- 缺少 `Nt*WaitCompletionPacket` 导出直接导致当前 ENOTSUP。动态探测失败可能存在回退，必须查匹配运行时的调用路径或抓取调用栈。
- 观察到 TCP RST 就能断定 Win7 afd.sys 存在只能改内核才能修复的阻断。应用层主动关闭、初始化选项失败及完成通知处理也需要排查；RST 本身不能区分这些原因。
- 纯用户态绝对不能实现等待到 IOCP 的替代路径。Win32 提供线程池等待与向完成端口投递通知的基础接口，但这不等于能够无损替代全部 NT 等待包语义；生命周期、取消与并发必须另行设计和验证。
- API Key 模式必然修复 Bun 网络问题。它能减少 OAuth 回调步骤，但模型出站请求仍经过网络栈，不能据此保证不再 ENOTSUP。

源码 `01-Extended DLLs/KxNet/winsock.c` 的 `Ext_setsockopt` 已对 SOL_SOCKET 下返回 WSAENOPROTOOPT 的 SO_REUSE_UNICASTPORT / SO_REUSE_MULTICASTPORT 做特定处理。不能把重新添加这些处理当成新修复。当前没有查到专门的 Ext_WSAIoctl 实现；应先捕获实际失败调用，不能对未知 IOCTL 一律返回成功。

`KxNt/forwards.c` 仍将 RtlGetVersion 转发至系统；旧文档列出的拦截建议不代表该修改已经落地，更不能据此修改本机版本伪装设置。

11 月 1 日 QueryInterruptTimePrecise 等导出补丁有 Antigravity 1.4.12 的历史通过记录，没有新版 Claude 网络通过记录，二者验收不可互相替代。

## 新模型与运行时约束

- Anthropic 维护者确认，2.1.112 是最后的 JS bundle，2.1.113 开始改为原生可执行文件。新 npm 包也是分发原生程序，换 npm 安装或升级本机 Node 不能切换其内部运行时。
- 官方当前要求 Opus 5.5 使用 Claude Code >= 2.1.280。不能把在 2.1.112 中修改菜单、模型字符串或伪报 CLI 版本视为受支持的升级。
- 对其他模型，菜单没有该项与服务端拒绝完整模型 ID 是不同问题；需要分别核实旧 CLI、供应商和账号权限。此处没有实测 Opus 4.7 或 Opus 5.5 的请求结果。
- 扩展 wrapper 仅改变启动链，无法使原生 Bun 改用 Node 网络栈，也无法自动补齐新 CLI 功能。

## 后续修复的具体顺序

1. 保留当前 Node 安装，在本仓库独立目录使用原生备份作为测试目标。旧 `claude-network-probe-20261101.cjs` 硬编码的是替换前的正式 bin/claude.exe 路径，该路径已不再是当前 Node 包入口；重用前应改为显式传入经过核验的备份 EXE 路径，不能为了测试恢复全局安装。
2. 使用隔离 CLAUDE_CONFIG_DIR、隐藏 winpty 和临时本机代理重新建立可重复对照；清除测试子进程的认证变量，不读取用户凭据，不发送模型任务。
3. 对该诊断子进程跟踪连接建立至首次发送之间的调用、返回值和栈：包括 setsockopt、WSAIoctl、ConnectEx 完成处理、WSASend/WSARecv、完成通知配置，以及后续 close。API 列表是取证范围，不是已确认失败点。必须保留首个底层失败的真实错误码，检查 ENOTSUP 是返回错误还是运行时自行判定。
4. 若是可选选项不支持，核对匹配 Bun/libuv 源码后设计有限回退；若是事件通知兼容问题，设计具有真实等待、投递和取消语义的实现。不得用空 stub 或对所有未知错误返回成功。
5. 先验证本机代理收到完整 CONNECT，再验证 TLS、官方预检、真实模型和扩展交互。预检通过不等于模型可用，基础对话通过也不等于 worktree 等功能通过。
6. DLL 改动在独立输出目录构建、测试并准备回滚。系统安装、IFEO 或版本伪装的变更遵守仓库部署边界，不由本轮文档复核自动执行。

## 官方核对来源

- [Anthropic 维护者对 2.1.113 原生切换的说明](https://github.com/anthropics/claude-code/issues/50307)。
- [当前 npm 安装机制](https://code.claude.com/docs/en/setup#install-with-npm)。
- [模型配置与 Opus 5.5 的最低 CLI 版本](https://code.claude.com/docs/en/model-config)。
- [RegisterWaitForSingleObject](https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-registerwaitforsingleobject) 与 [PostQueuedCompletionStatus](https://learn.microsoft.com/windows/win32/fileio/postqueuedcompletionstatus)。这些基础能力不能单独证明本任务可用某种等待包替代实现。