# Claude Code ENOTSUP 排查（2026-11-01）

完整安装位置与操作归档见：[Claude Code 从 Bun 切换 Node 安装操作记录](ClaudeCode从Bun切换Node安装操作记录-20261101.md)。下文按会话阶段保留历史状态；最终状态以该操作记录及当前工作入口为准。

## 方案与边界

检查代理和 CLI 包入口；用无凭据 curl 请求区分代理/TLS 故障；用独立 CLAUDE_CONFIG_DIR 启动同一 CLI，再用本机临时 HTTP 代理记录是否发送 CONNECT。无模型提示、无用户认证，不更改全局环境、DLL 或 IFEO，仅结束诊断脚本自己创建的进程。

## 初始现场

Git HEAD 2e81e562c2970cafb742b2c1f64744c71bcee85a，初始工作区干净。Win7 SP1；本次 shell 为 PowerShell 7.6.7，Node v22.22.2。SDK SetEnv.cmd/MSBuild 路径存在，本次无需构建。沙箱 shell 启动返回 0xC0000142；获准的沙箱外诊断可运行，不能将此视作 Claude 故障。

npm Claude 2.1.316 的 claude.ps1 直接启动 bin/claude.exe，没有 cli.js。npm 安装不等于 Node 运行。BUN_BE_BUN=1 不能进入通用 Bun 模式，-e 报未知参数，不属于成功的 fetch 实测。

当前用户与进程代理均为 http://192.168.1.199:10809，TCP 可达；历史备用 10.81.235.69:10809 本次 TCP 超时。curl 经当前代理 CONNECT 200，首次 TLS 超时，复测 Anthropic 根路径 HTTP 404、证书校验结果 0、退出码 0。一般站点 example.com 本次超时，链路存在波动。404 仅说明已收到 HTTP 响应，不代表模型 API 验收。

旧 Bun/Claude 文档是其他版本与 ECONNREFUSED 的历史证据，不能据此断言本次 ENOTSUP 的具体内核调用或不可修复。

## 约定附带检查

最新登记启用的 Antigravity 为 1.10.0，无旧补丁标记；新版已有持久化版本 sidecar、文件状态核对、版本超时复用、下载失败退避与跨窗口安装锁，不再是旧版只凭每次 --version 探测的流程。保留新版实现，未套用只适用于 1.9.0 的脚本；未对其运行验收，也不宣称所有失败分支均有清单哈希回退。

## 对照结果

见下方实际对照结果。
## 实际对照结果与结论

用仓库已验证的 node-pty/useConpty:false 创建隐藏 winpty 控制台，两轮均复现：

| 客户端/路径 | 结果 |
| --- | --- |
| Claude 2.1.316 → 192.168.1.199:10809 | 启动预检 ENOTSUP，退出 1 |
| 普通 Node HTTP → 临时 127.0.0.1 代理 | 收到测试端设计的 HTTP 502，证明监听及收发正常 |
| Claude 2.1.316 → 同一临时代理 | ENOTSUP，退出 1；2 次 TCP 连接、0 次收到数据/HTTP 请求 |
| curl → 当前代理 → api.anthropic.com | 复测 CONNECT 200、TLS 校验成功、HTTP 404、退出 0 |

二进制可检出 Bun v1.4.4 字符串。结合 npm 入口与本机代理对照，故障定位在本机 Claude 原生运行时的网络路径，且发生在 HTTP CONNECT 发出之前。Win7/VxKex 与该运行时的兼容问题为主要判断；不是仅凭代理提示判定地址填错，也不能依据通用国家提示认定账号或地区限制。

本次最新 VxKex 日志还出现 NtCreateWaitCompletionPacket/NtAssociateWaitCompletionPacket/NtCancelWaitCompletionPacket 与 WSHGetBroadcastSockaddr 动态解析失败；这是兼容性线索，但没有调用栈证明这些缺失直接产生 ENOTSUP。未捕获明确失败的 setsockopt 参数。不能据此直接改 DLL、伪造成功返回或宣称不可修复。

当前代理确有偶发 TLS 超时，属于另一个观察结果；本机代理已能独立复现 ENOTSUP，因此远端网络波动不足以解释这个错误。官方说明支持 HTTP/HTTPS 代理、不支持 SOCKS；当前代理是 HTTP，符合配置类型：https://code.claude.com/docs/en/network-config 。

## 后续与边界

- 本次完成原因定位与隔离复现，尚未修复 CLI；未进行登录、模型调用、构建、部署或 Git 提交。
- 若继续兼容层修复，下一步应跟踪网络初始化到首个发送之间的 Winsock/libuv 调用和错误码，锁定返回 ENOTSUP 的准确位置，再设计补丁。不要将缺失符号日志直接当作因果链。
- 当前 npm 包没有 cli.js，仅重装 npm/换 Node 并不会让相同版本改用 Node 网络栈；旧 Node 实现能否替代需另行验证。
- 复现脚本：claude-network-probe-20261101.cjs；精简统计：claude-network-probe-20261101/summary.json。原始控制台输出与调试配置留在同目录并用本地 .gitignore 排除，避免提交临时状态。
- 无终端的初次试验仅报 print 模式需要输入，不能当作网络复现。工具自带 ConPTY 在本机不可用；最终以 winpty 结果为准。
## Node 版独立安装方案（用户于 2026-11-01 明确要求）

将官方 @anthropic-ai/claude-code@2.1.112 安装到本仓库 .tools/claude-code-node-2.1.112，npm 缓存也位于本仓库；先核对 registry 元数据与包入口。使用已存在的 Node 22.22.2 绝对路径执行 cli.js，禁用自动更新，防止变回 Bun 版。准备仓库根目录 claude-node.cmd 独立入口。保留原安装及现有全局命令，避免修改禁止写入的外部项目。验收 --version、--help 和独立配置的交互启动；不主动发送模型请求。
## Node 版已安装与验证结果

- 独立安装位置：D:\project\VxKex-NEXT\.tools\claude-code-node-2.1.112。
- 官方 registry 元数据与安装后 package.json 均确认 version=2.1.112、bin.claude=cli.js、Node >=18；cli.js 第一行为 #!/usr/bin/env node。
- 使用现有 Node v22.22.2 运行 npm 12.0.2，固定版本安装成功（真实 INSTALL_EXIT=0，added 2 packages）。安装使用 --ignore-scripts、--omit=optional、--strict-ssl=true，且安装子进程明确设 NODE_TLS_REJECT_UNAUTHORIZED=1；初次读取元数据曾发现继承环境的该变量为 0，最终安装已启用验证，未修改持久环境。
- 独立入口：D:\project\VxKex-NEXT\claude-node.cmd，明确用 Node 绝对路径执行上述 cli.js；仅在该启动子进程设置 DISABLE_AUTOUPDATER=1，并补入 Node PATH。使用现有用户配置；启动器不强制改变代理、认证或 TLS 环境。
- cli.js SHA256：BC3358282800E3E99DAA8E71AC5B7B1566BD0D7CA7EB94F714A7859365D3163F。
- --version 显示 2.1.112 (Claude Code)、退出 0；--help 显示帮助、退出 0。
- 独立配置 + winpty 实测到达欢迎界面及主题选择菜单；观察 9 秒后主动结束诊断进程。因此该次记录 exitCode=1 是人为结束，不是自然崩溃。该阶段无 ENOTSUP，但尚未执行登录后的联网或模型请求，不能宣称网络问题已修复。
- 验证输出保存在 .tools/claude-code-node-2.1.112/startup-validation，安装文件、缓存及测试配置均由 .tools/.gitignore 排除。
- 使用：在本仓库 PowerShell 输入 .\claude-node.cmd；原来的 claude 命令仍指向原安装。本轮没有修改外部项目、用户 PATH、Profile、DLL 或 IFEO，未作 Git 提交。
## 原 npm 安装切换方案（用户明确要求替换）

本轮授权范围是原 prefix 下的 Claude npm 包及 claude/claude.cmd/claude.ps1 启动器，不涉及该外部项目的其他文件。先把原 2.1.316 包和三个启动器复制到本仓库 .tools 下，记录 SHA256 并核验；再在原 prefix 定点安装官方 2.1.112，缓存仍放本仓库且启用 TLS 验证。启动器只为其启动的 Claude 设置 DISABLE_AUTOUPDATER=1，避免改变用户持久环境。最后用原入口核验 Node 的 cli.js、--version、--help；原独立入口仍保留作备用。恢复可将已验证备份包和启动器覆盖回原位置，无需修改用户配置。
## 原 claude 命令已切换为 Node 版

用户明确要求将原 Bun 安装替换为 Node 版，本轮仅对该 prefix 下 Claude 包及三个入口进行修改。

- 原 2.1.316 包和 claude/claude.cmd/claude.ps1 已备份并逐文件核对 SHA256，共 15 个文件，manifest.json 保存原 prefix 和摘要。
- 原 prefix 上 npm install 遇到 @img/sharp-win32-x64 目录创建 EPERM，退出 -4048；核实 npm 已回滚到原包后，采用 PowerShell 文件部署：先验证精确绝对源/目标路径，将原包移入本仓库备份 retired-package，再把已安装验证的官方 2.1.112 完整包复制到原位置，所有部署文件逐一与源包 SHA256 相等。
- 原安装路径仍为 D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@anthropic-ai\claude-code，package.json 为 2.1.112、入口 cli.js。
- 原 claude.cmd、claude.ps1 和 Git Bash claude 入口均明确调用同目录 node.exe + cli.js，并仅在 Claude 启动环境禁用自动更新。PowerShell 入口 finally 恢复之前的环境状态；原变量不存在/已有值两项验证均通过。
- 实测 PowerShell 裸命令 claude --version、claude.ps1 --version、claude.cmd --version 均显示 2.1.112 (Claude Code)，真实退出码 0；帮助退出码 0；npm ls -g 识别 2.1.112、退出 0。
- Git Bash 启动验证未在观察期间返回，已仅结束本次创建的 Bash 进程树；该入口未作运行通过声明。用户当前 PowerShell 入口已验收。
- 未修改用户配置、全局 PATH、Profile、代理、系统 DLL 或 IFEO；没有登录或调用模型。本仓库独立的 claude-node.cmd 仍可作为备用。
- 备份路径见下方；restore-original.ps1 默认只验证备份（已通过），加 -Apply 可将旧包和三个入口恢复。恢复脚本没有删除操作，会保留未引用的额外 Node 文件。
备份目录：D:\project\VxKex-NEXT\.tools\claude-original-2.1.316-20261101-123638
