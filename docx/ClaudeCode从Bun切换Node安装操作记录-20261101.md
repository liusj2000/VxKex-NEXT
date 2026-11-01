# Claude Code 从 Bun 版切换为 Node 版：操作与安装位置记录

> 2026-11-01 后续状态：用户已明确要求恢复最新版。本轮正式安装已切回官方 2.1.316 原生版，原 portable prefix 不变，原 claude 命令不再指向 Node 2.1.112。本文保留此前切换过程；当前状态请看 [恢复最新版 portable 安装记录](ClaudeCode恢复最新版portable安装-20261101.md) 与 [当前工作](当前工作.md)。

记录日期：2026-11-01（北京时间）。本记录汇总本次会话已经执行的排查、独立安装、原入口替换与验证，供后续维护和恢复使用。

## 1. 最终状态与使用方法

原来通过 npm 安装的 Claude Code 2.1.316 已替换为 **2.1.112 Node 版**。现在在用户原有 PowerShell 中直接执行：

```powershell
claude
claude --version
```

版本查询已实测输出 `2.1.112 (Claude Code)`，退出码 0。启动链为：

```text
claude → claude.ps1 / claude.cmd → Node 22.22.2 的 node.exe → Claude 2.1.112 的 cli.js
```

原来的 2.1.316 npm 包入口是 `bin/claude.exe`，二进制含 Bun 1.4.4；因此此前虽然使用 npm 安装，运行时仍是 Bun。官方 2.1.113 更新记录记载了 npm 入口从 JavaScript 改为原生二进制的切换，2.1.112 保留 Node 的 `cli.js` 入口。

## 2. 安装、入口、配置及资料位置

| 用途 | 绝对路径 |
| --- | --- |
| 原 npm prefix，也是当前正式安装 prefix | `D:\project\claude-code-engineering\node-v22.22.2-win-x64` |
| 实际使用的 Node 运行时 | `D:\project\claude-code-engineering\node-v22.22.2-win-x64\node.exe` |
| npm 程序 | `D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\npm\bin\npm-cli.js` |
| 当前正式 Claude 包目录 | `D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@anthropic-ai\claude-code` |
| 当前正式 Claude JavaScript 入口 | `D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@anthropic-ai\claude-code\cli.js` |
| PowerShell 原命令入口 | `D:\project\claude-code-engineering\node-v22.22.2-win-x64\claude.ps1` |
| CMD 原命令入口 | `D:\project\claude-code-engineering\node-v22.22.2-win-x64\claude.cmd` |
| Git Bash 原命令入口 | `D:\project\claude-code-engineering\node-v22.22.2-win-x64\claude` |
| 先安装的独立 Node 版副本，现保留备用 | `D:\project\VxKex-NEXT\.tools\claude-code-node-2.1.112` |
| 独立副本的包入口 | `D:\project\VxKex-NEXT\.tools\claude-code-node-2.1.112\node_modules\@anthropic-ai\claude-code\cli.js` |
| 独立副本启动器 | `D:\project\VxKex-NEXT\claude-node.cmd` |
| 本次 npm 缓存和安装日志 | `D:\project\VxKex-NEXT\.tools\npm-cache`，日志在其 `_logs` 子目录 |
| 既有用户配置目录 | `C:\Users\liusj\.claude`，本次未主动改写其中的配置或认证文件 |
| 独立 Node 版启动测试配置和输出 | `D:\project\VxKex-NEXT\.tools\claude-code-node-2.1.112\startup-validation` |
| Bun 网络隔离测试配置和输出 | `D:\project\VxKex-NEXT\docx\claude-network-probe-20261101` |
| Bun 网络复现脚本 | `D:\project\VxKex-NEXT\docx\claude-network-probe-20261101.cjs` |
| 原始排查与分阶段记录 | `D:\project\VxKex-NEXT\docx\ClaudeCode-ENOTSUP排查-20261101.md` |
| 会话恢复入口 | `D:\project\VxKex-NEXT\docx\当前工作.md` |

本机本次实际诊断环境为 Windows 7 SP1 x64、PowerShell 7.6.7、Node 22.22.2、npm 12.0.2。这些是本次现场值，不能沿用历史记录中的其他 PowerShell 版本。

## 3. 实际执行的操作

### 3.1 排查原 Bun 版 ENOTSUP

1. 读取工作记录，检查 Git 状态和原包入口。开始时 HEAD 为 `2e81e562c2970cafb742b2c1f64744c71bcee85a`、工作区干净；本次相关文档和启动器变更尚未提交。
2. 检查进程及用户代理配置，当前为 `http://192.168.1.199:10809`。该代理 TCP 可达，curl 复测可完成 TLS 校验并收到 Anthropic 根路径 HTTP 404；也观察到偶发 TLS 超时。历史备用代理 `10.81.235.69:10809` 本次不可达。
3. 使用隔离配置和隐藏 winpty 控制台复现原 Claude 2.1.316 启动错误；用本机临时 HTTP 代理作对照。Claude 两条代理路径均报 ENOTSUP；本机代理收到 2 次 TCP 连接、0 次 HTTP 请求。普通 Node 客户端访问同一代理正常。
4. 故障主要定位于本机原生运行时在 Win7/VxKex 下的网络兼容路径，发生在 HTTP CONNECT 发出之前；具体产生 ENOTSUP 的调用尚未锁定。没有把通用国家提示认定为账号或地区封禁。
5. 诊断工具的 ConPTY 在本机不可用，最终采用仓库已有的 `node-pty`、`useConpty:false` 路径。只结束本次创建的诊断进程和临时监听。

### 3.2 先独立安装并验证 Node 2.1.112

先从官方 npm registry 查询元数据，确认 `version=2.1.112`、`bin.claude=cli.js`、Node 要求 `>=18`，再使用现有 Node 和 npm 安装到本仓库 `.tools`。实际命令的关键内容如下：

```powershell
$env:NODE_TLS_REJECT_UNAUTHORIZED = '1'
$env:PATH = 'D:\project\claude-code-engineering\node-v22.22.2-win-x64;' + $env:PATH
& 'D:\project\claude-code-engineering\node-v22.22.2-win-x64\node.exe' `
  'D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\npm\bin\npm-cli.js' `
  install --global `
  --prefix 'D:\project\VxKex-NEXT\.tools\claude-code-node-2.1.112' `
  '@anthropic-ai/claude-code@2.1.112' `
  --omit=optional --ignore-scripts --no-audit --no-fund --strict-ssl=true `
  --registry=https://registry.npmjs.org `
  '--cache=D:\project\VxKex-NEXT\.tools\npm-cache' `
  --fetch-retries=1 --fetch-timeout=25000
```

以上环境设置仅用于当时的安装子进程，没有改写用户持久环境。初次查询曾发现继承的 `NODE_TLS_REJECT_UNAUTHORIZED=0`；最终安装明确启用 TLS 验证。安装真实退出码为 0，npm 输出 `added 2 packages`。

随后检查安装后的 `package.json` 和 `cli.js` 首行，确认入口为 `#!/usr/bin/env node`；创建独立启动器 `D:\project\VxKex-NEXT\claude-node.cmd`。使用隔离配置启动到欢迎界面和主题菜单，观察 9 秒后主动结束，没有执行登录或模型请求。

### 3.3 备份原版并替换原安装

用户之后明确要求把原来的命令替换为 Node 版，因此修改范围扩展为原 prefix 下的 Claude 包和三个 Claude 启动器；没有修改该外部项目的其他文件。

1. 检查未有来自该 prefix 的原 Claude EXE 运行。
2. 将原 2.1.316 完整包和 `claude`、`claude.cmd`、`claude.ps1` 复制到本仓库备份目录；逐文件对照 SHA256，15 个文件均一致，保存 `manifest.json`。
3. 尝试将上述 npm 安装命令的 `--prefix` 改为原 prefix 执行原位安装。npm 在创建 `node_modules\@img\sharp-win32-x64` 时返回 EPERM，真实退出码 `-4048`。随后核实 npm 已回滚，原包仍为 2.1.316，不能把这次尝试记为安装成功。
4. 改用文件部署：核实源和目标绝对路径，将原 Claude 包移至本仓库备份的 `retired-package`，再将前一步已安装并验证的官方 2.1.112 完整包复制到原包目录。所有部署文件均与独立副本逐一核对 SHA256，一致。
5. 重写原位置的三个 Claude 启动器，明确执行同目录 `node.exe` 和 `node_modules\@anthropic-ai\claude-code\cli.js`。
6. 保存替换后的版本、原 prefix、CLI 和启动器摘要到备份目录的 `replacement.json`，并准备恢复脚本。

原位 npm 失败日志：

`D:\project\VxKex-NEXT\.tools\npm-cache\_logs\2026-11-01T04_36_47_705Z-debug-0.log`

### 3.4 禁用自动更新的具体方式

三个原启动器均为 Claude 设置 `DISABLE_AUTOUPDATER=1`。CMD 使用 `setlocal`，Git Bash 使用其脚本进程环境；PowerShell 使用 `try/finally` 保存并恢复原环境变量，已测试原变量不存在和已有值两种情况均可恢复。

独立备用启动器也禁用自动更新。没有向用户 Profile 或全局环境写入此变量，也没有修改全局 PATH。手动执行 npm 重新安装最新版会覆盖当前版本或启动器；若要保留 Node 运行方式，维护时需继续固定 2.1.112。

## 4. 验证结果与未完成项目

| 项目 | 实际结果 |
| --- | --- |
| Node 版 package.json | 2.1.112，`bin.claude=cli.js` |
| PowerShell 直接 `claude --version` | 2.1.112，退出 0 |
| 原 `claude.ps1 --version` | 2.1.112，退出 0 |
| 原 `claude.cmd --version` | 2.1.112，退出 0 |
| `--help` | 正常输出帮助，退出 0 |
| 原 prefix 的 `npm ls -g @anthropic-ai/claude-code --depth=0` | 识别 2.1.112，退出 0 |
| PowerShell 启动后恢复自动更新环境变量 | 缺省和已有值两种情况通过 |
| 独立 Node 副本的交互启动 | 到达欢迎界面及主题选择菜单，观察后主动结束 |
| Git Bash 原入口 | 已改为 Node，但本次运行验证未返回；仅结束本次诊断进程树，未宣称通过 |
| 原版备份 | SHA256 核验通过；恢复脚本默认验证模式通过 |
| 完整回滚 | 尚未实际执行 `-Apply` |
| 登录、模型请求和完整交互 | 尚未验证，不能宣称联网故障已完全解决 |

本操作文档整理时，再次读取了实际包和启动器，并核验当前 CLI 及三个启动器摘要与 `replacement.json` 一致；没有重新执行登录或联网测试。

当前正式 `cli.js` SHA256：

```text
BC3358282800E3E99DAA8E71AC5B7B1566BD0D7CA7EB94F714A7859365D3163F
```

三个启动器的 SHA256 保存在下面的 `replacement.json` 中，便于以后识别自动更新或 npm 重装是否覆盖了入口。

## 5. 原版备份、验证和回滚

完整备份目录：

`D:\project\VxKex-NEXT\.tools\claude-original-2.1.316-20261101-123638`

| 备份内容 | 相对备份目录的位置 |
| --- | --- |
| 原包的完整复制备份 | `claude-code` |
| 替换时移出的原包 | `retired-package` |
| 原三个入口 | `claude`、`claude.cmd`、`claude.ps1` |
| 原文件摘要清单 | `manifest.json` |
| 替换后版本及摘要 | `replacement.json` |
| 恢复脚本 | `restore-original.ps1` |

仅检查原版备份，不改变当前安装：

```powershell
pwsh -NoProfile -File 'D:\project\VxKex-NEXT\.tools\claude-original-2.1.316-20261101-123638\restore-original.ps1'
```

需要恢复原 Bun 2.1.316 时，关闭正在使用该安装的 Claude，再执行：

```powershell
pwsh -NoProfile -File 'D:\project\VxKex-NEXT\.tools\claude-original-2.1.316-20261101-123638\restore-original.ps1' -Apply
```

脚本先核对备份 SHA256，再恢复原包和三个入口，并核验恢复文件的摘要。它不执行删除操作，会保留 Node 包产生但旧入口不会引用的额外文件。此脚本的验证模式已运行，实际恢复模式尚未运行。

## 6. 文件与环境变更范围

- 正式变更：原 prefix 下的 Claude 包、三个原命令入口；范围由用户明确要求替换原安装授权。
- 本仓库新增：独立 Node 安装副本、缓存、旧版备份、`claude-node.cmd`、网络诊断脚本及文档。
- 生成物由 `D:\project\VxKex-NEXT\.tools\.gitignore` 排除；网络诊断临时配置由其目录内 `.gitignore` 排除。备份未提交到 Git，不能只依赖 Git 历史恢复原二进制。
- 未修改用户持久代理、PATH、Profile、系统 DLL、IFEO、系统策略或其他外部项目文件；未运行安装器、未构建 VxKex、未做 Git 提交。
- 会话约定的 Antigravity 附带检查仅为读取已启用的 1.10.0 实现，没有应用旧版补丁。

## 7. 相关来源与记录

- [官方 2.1.113 更新记录](https://github.com/anthropics/claude-code/blob/main/CHANGELOG.md#21113)
- [官方 npm 安装说明](https://code.claude.com/docs/en/setup#install-with-npm)
- [本次 ENOTSUP 排查与分阶段操作记录](ClaudeCode-ENOTSUP排查-20261101.md)
- [当前工作恢复入口](当前工作.md)
