# Claude Code 恢复最新版原生版：portable 安装记录（2026-11-01）

## 恢复完成后的正式使用状态

用户已明确选择恢复 Claude Code 原生版，停止使用旧 Node CLI 作为正式命令。以下是本次已经完成的恢复记录，便于后续会话直接核对。

| 项目 | 已完成的状态 |
| --- | --- |
| 恢复版本 | 官方 npm latest 在安装时确认为 2.1.316 |
| 运行方式 | 原生 claude.exe（Bun 打包），由 portable 启动器直接运行 |
| portable 根目录 | D:\project\claude-code-engineering\node-v22.22.2-win-x64 |
| 正式程序绝对路径 | D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@anthropic-ai\claude-code\bin\claude.exe |
| 日常命令 | 原终端直接运行 claude；claude --version 显示 2.1.316 (Claude Code) |
| 已验证入口 | PowerShell 裸命令、claude.ps1、claude.cmd；版本检查均退出 0 |
| 其他已验证内容 | 原生 --help、npm 包版本、14 个部署文件源/目标哈希及旧版备份校验 |
| 旧 Node 版处置 | 已移入本仓库备份，不再由正式 claude 命令启动；正式包没有旧 cli.js |
| 自动更新入口 | 已移除此前 Node 启动器强加的 DISABLE_AUTOUPDATER=1；未更改用户持久环境 |
| 备份与回滚 | .tools\claude-node-before-latest-20261101-153555；默认只验证，rollback.ps1 -Apply 才回退 |
| 尚未验收 | 登录、联网、Opus 5.5 和真实交互；Git Bash 入口只核对文件，未运行测试 |

可从原终端检查：

```powershell
claude --version
claude
```

若当前终端没有 portable 目录的 PATH，可直接使用绝对入口：

```powershell
& 'D:\project\claude-code-engineering\node-v22.22.2-win-x64\claude.ps1' --version
& 'D:\project\claude-code-engineering\node-v22.22.2-win-x64\claude.ps1'
```

本次恢复没有实施 Bun ENOTSUP 的兼容修复。版本与帮助通过仅代表基础启动验证；后续联网验收需另行记录。本文补录依据是本次部署清单与 validation.json，不把文档整理记为重新运行测试。

## 用户要求与实施方案

用户明确要求替换旧 Node CLI，改用最新版，继续放在当前 portable 安装目录。授权范围为 D:\project\claude-code-engineering\node-v22.22.2-win-x64 下的 Claude 包与 claude / claude.cmd / claude.ps1 三个入口。

本次官方 npm registry 在启用 TLS 验证后确认 latest=2.1.316；bin.claude=bin/claude.exe，win32-x64 原生平台依赖为同版本。查询真实退出码 0。首次查询继承环境的 NODE_TLS_REJECT_UNAUTHORIZED=0，因此已用单独子进程设置为 1 重新核对；后续下载安装也明确启用 TLS 验证，不修改用户持久环境。

为避免此前原 prefix 上 npm 遇到 EPERM，以及影响该目录其他工具，先在本仓库 .tools 中使用官方 registry 准备固定版本的干净安装，再精确部署 Claude 包和三个入口到现有 prefix。

步骤：

1. 在 .tools 下创建唯一 staging 目录，用已有 Node/npm 安装官方 2.1.316，缓存放本仓库；不改全局 PATH 或用户配置。
2. 检查包版本、原生 EXE 与三个入口；阶段性运行 --version / --help，记录真实退出码。npm integrity 检查由安装器完成。
3. 准备 .tools 下的独立回滚备份；逐文件记录当前 Node 包和入口 SHA256。确认没有正在使用该安装的 Claude 进程后，核实源、目标的绝对路径，再将原包移入备份，部署 staging 包并恢复官方原生启动入口。
4. 逐文件核验源/目标哈希，验证正式 PowerShell 和 CMD 命令入口的 --version / --help，使用 npm ls 核对登记版本。保留未运行测试的明确边界。
5. 保存部署清单与回滚脚本，更新当前工作入口。保留旧独立 Node 副本仅供回滚，不作为正式 claude 入口。

只做版本替换与基础启动验证；历史 Bun ENOTSUP 尚未修复。不会把 --version / --help 成功写成联网或模型请求通过。本次不更改 DLL、IFEO、版本伪装、代理、账号、系统策略或其他项目文件，不运行 VxKex 安装器。

## 执行结果

已完成，详见下方本次验证结果。

## 实际执行与验证结果

- 官方 latest 查询与启用 TLS 的隔离安装均成功：版本 2.1.316；npm 安装退出 0，added 2 packages。安装先使用 --ignore-scripts，然后在暂存包内运行已审阅的官方 install.cjs 放置原生 EXE，真实退出码 0。
- 暂存目录：D:\project\VxKex-NEXT\.tools\claude-native-2.1.316-20261101-153045。
- 正式安装 prefix：D:\project\claude-code-engineering\node-v22.22.2-win-x64。
- 正式包：上述 prefix 下 node_modules\@anthropic-ai\claude-code；bin.claude=bin/claude.exe，已不包含旧 cli.js。
- 正式原生 EXE 大小 335469216 字节，SHA256 为 E896A9D8DA519353A49BF69CA4A695BA7E09067752687C675B3969041E632D52，与旧版原生备份的同版本一致。
- claude / claude.cmd / claude.ps1 均换为官方安装生成的原生入口；不再通过 Node 执行 cli.js，旧入口中的 DISABLE_AUTOUPDATER=1 固定逻辑已移除。未改变用户持久环境变量。
- 部署脚本先核对版本、原生文件、使用目标安装的进程及源/目标绝对路径；保存旧文件摘要后将整个旧包移入本仓库备份，再复制干净新版包与入口。14 个源/目标文件的 SHA256 全部一致。没有在原 prefix 上执行可能影响其他工具的 npm 全局重装。
- 当前旧 Node 安装和三个入口备份：D:\project\VxKex-NEXT\.tools\claude-node-before-latest-20261101-153555。
- before.json 保存旧版摘要，after.json 保存正式新版摘要；validation.json 及各命令 stdout/stderr 留存于备份目录。

| 验证 | 本次结果 |
| --- | --- |
| 原生 EXE --version | 2.1.316 (Claude Code)，退出 0 |
| PowerShell 裸命令 claude --version | 2.1.316，退出 0；测试子进程 PATH 加入现有 prefix，没有持久改 PATH |
| 正式 claude.ps1 --version | 2.1.316，退出 0 |
| 正式 claude.cmd --version | 2.1.316，退出 0 |
| 原生 EXE --help | 正常输出帮助，退出 0 |
| npm ls -g --prefix 原目录 @anthropic-ai/claude-code --depth=0 | 识别 2.1.316，退出 0 |
| rollback.ps1 默认验证模式 | BACKUP_VERIFIED，退出 0 |
| Git Bash 入口 | 已替换并核对哈希；未作运行验收 |
| 联网、登录、Opus 5.5 与交互 | 本轮未测试；历史 ENOTSUP 仍待独立修复 |
| 实际回滚 -Apply | 未执行 |

回滚默认只核验旧备份：

```powershell
& 'D:\project\VxKex-NEXT\.tools\claude-node-before-latest-20261101-153555\rollback.ps1'
```

只有需要退回旧 Node 版时才使用 -Apply；当前用户要求保留最新原生版，本轮没有执行回退。回滚脚本核验当前新版与备份哈希，若被后续更新则拒绝覆盖，并保留被替换的原生包。

实施脚本：docx/switch-claude-portable-latest-20261101.ps1（默认只检查，-Apply 才部署）；回滚模板：docx/rollback-claude-portable-20261101.ps1。旧独立 Node 副本和原生 2.1.316 的旧备份仍保留作历史备份，不参与正式 claude 启动。

本轮只修改用户明确指定的 Claude 包与三个命令入口，未改动该 portable 目录中的其他工具。没有改动 VS Code 扩展配置、账号、代理、DLL、IFEO 或系统策略，未构建 VxKex、未调用模型、未提交 Git。