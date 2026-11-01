# OpenCode 中文输入残留 s 的启动配置修复（2026-11-01）

## 证据与设计

用户在 OpenCode 2.2.5 界面输入中文后再次出现残留 s 和占位符错位。历史根因与 CP65001 下中文显示列宽不匹配有关，参见 [历史实测](pwsh黑屏与VSCode终端滚动重行排查解决-20261024.md)。

本轮只读附加到正在运行的 OpenCode 控制台：PID 1812、5596 的输入/输出代码页均为 65001；子进程 3988 无可附加控制台。四个用户 Profile 均已移除强制 UTF-8，均包含 PSReadLine 卸载；PowerShell/profile.ps1 缺少备用屏幕变量。仅删除切换命令不能恢复已经处于 65001 的控制台。

独立隐藏控制台设为 936 后执行实际安装的 OpenCode --version，输出 opencode v2.2.5、退出 0，前后输入/输出代码页均为 936。此测试仅覆盖版本命令，没有证明完整 TUI 不切换代码页，也未锁定是谁最初切换了现有窗口。

修复范围：
1. 保留四个用户 Profile 原有内容，末尾增加显式 CP936 输入/输出初始化、备用屏幕 true 和 PSReadLine 卸载。
2. 不改 npm 自动生成的入口，避免升级覆盖；不改 OpenCode 二进制、系统 DLL、IFEO、全局系统区域设置或 VS Code 配置。
3. 修改前备份原始字节及 SHA256，在仓库保存候选文件并检查 PowerShell 语法，再部署并回读核对。
4. 在独立隐藏控制台从 65001 开始加载每个 Profile，检查实际 Win32 输入/输出代码页为 936；再检查正常加载 Profile 的新 pwsh 会话。
5. 已有窗口不会自动重新加载 Profile。用户退出 OpenCode，关闭旧终端并新开窗口，再进行中文输入、退格、斜杠菜单和 Esc 重绘验收。UI 验收仍由用户完成。

## 实施与验收

已部署至以下四个用户 Profile，原内容保留，末尾追加带标记的修复块：
- C:\Users\liusj\Documents\PowerShell\profile.ps1
- C:\Users\liusj\Documents\PowerShell\Microsoft.PowerShell_profile.ps1
- C:\Users\liusj\Documents\WindowsPowerShell\profile.ps1
- C:\Users\liusj\Documents\WindowsPowerShell\Microsoft.PowerShell_profile.ps1

备份目录：D:\project\VxKex-NEXT\.tools\opencode-startup-20261101-165457。
manifest.json 含四文件修改前后 SHA256；原始字节保存在 profile-N.original.ps1。rollback.ps1 默认仅验证，显式 -Apply 才恢复；若 Profile 在修复后又被修改会拒绝覆盖。回滚预演通过，未实际回滚。

实际验证（本轮运行，退出码 0）：
- 新 pwsh 正常加载 Profile 后，Win32 输入/输出代码页均为 936，备用屏幕 true。
- 先将控制台切到 65001，再分别加载四个 Profile：四次均恢复输入/输出 936，备用屏幕 true，PSReadLine 未加载。
- OpenCode --version 输出 opencode v2.2.5，退出 0；运行后输入/输出仍为 936。
- 四份候选脚本 AST 语法检查通过；部署后文件 SHA256 与候选一致。
- WindowsPowerShell 目录中的两份 Profile 本轮在 pwsh 内加载验证；未单独启动 Windows PowerShell 5.1 验收。

证据：[验证结果](opencode-startup-validation-20261101.json)、[验证脚本](verify-opencode-startup-20261101.ps1)、[部署脚本](repair-opencode-startup-20261101.ps1)、[初次版本探测](opencode-codepage-probe-20261101.json)。

## 用户验收与限制

退出 OpenCode 并关闭旧终端，新开普通 pwsh 窗口（不要使用 -NoProfile），运行 chcp，预期 936，然后启动 opencode。输入“继续”“你好”、退格，再按 / 和 Esc，观察 s 残留与占位符重绘。

本轮未关闭或修改运行中的用户控制台。真实 TUI/输入法重绘尚待用户验收。如果应用启动后又切到 65001，需进一步追踪启动后的切换位置；不能用版本命令通过替代交互验证。未修改 npm 入口、OpenCode 二进制、系统 DLL 或 IFEO，未构建或提交。

## 用户复验未通过后的二次排查

用户反馈重开后仍有 s，故上文 Profile 修复不能作为最终解决方案。2026-11-01 本轮再次读取活动进程：OpenCode PID 2124、5892 的输入/输出代码页均为 65001，说明版本命令验证未覆盖实际 TUI 启动/运行路径。尚未取得具体调用栈，不断言切换来自 Bun 还是 OpenTUI。

已在这两个正在运行的控制台中调用 SetConsoleCP(936) 和 SetConsoleOutputCP(936)，均返回成功，回读均为 936；没有关闭进程、发送按键或修改输入文本。此次为针对运行状态的可逆诊断调整，等待用户删除后重新输入“继续”的结果，再决定持久修复方式。不能将代码页回读成功写成中文 UI 已通过。

证据：opencode-live-cp936-20261101.json。由于 KxBase 的 ANSI 输出按当前输出代码页解码，强制在运行中固定 936 有可能与新版 TUI 的 UTF-8 字节输出冲突；必须观察真实输入/重绘结果，不能先部署后台循环改代码页方案。

## 运行期间修复方案（用户确认运行中恢复 CP936 后正常）

用户已明确回复“正常了”：当前控制台改为 936 后重新输入中文恢复正常。因此采用范围限定在 opencode 命令内的运行期间配置包装：
- 保留 npm 官方 EXE 与生成的启动脚本；在两个 CurrentUserAllHosts Profile 中加载独立辅助脚本，定义 opencode 函数。
- 继承原控制台启动官方 EXE，每 100ms 检查本控制台代码页，仅在被改变时恢复 936；子进程退出后结束检查，恢复启动前代码页并保留退出码。没有常驻后台服务。
- 保持参数、当前目录与备用屏幕；管道输入或无交互控制台直接执行官方入口，不强制改变代码页。
- 部署前验证 Windows 参数转义、退出码、重复切换后的恢复、退出后恢复原代码页及实际 OpenCode 版本；再用隐藏真实 TUI 验证。
- 这是针对本机 Win7 + 当前 OpenCode 的兼容启动方式；直接运行 EXE、opencode.cmd 或使用 -NoProfile 不经过它。未来若修复兼容层，应移除此临时包装。

## 最终实施结果（2026-11-01，已部署）

用户已确认：对运行中的控制台恢复 CP936 后，中文输入“正常了”。前一阶段仅修改 Profile 的方案不足，现已改为随 opencode 命令运行的代码页检查。

正式实现：C:\Users\liusj\Documents\PowerShell\VxKex.OpenCode.ps1，SHA256=19EDBE8F257E0D8D05935E2B6C42327B5402DA2CD92C6D75228EC8154357EAB5。源码留存：[opencode-win7-console.ps1](opencode-win7-console.ps1)。两个 CurrentUserAllHosts Profile 已追加加载语句：
- C:\Users\liusj\Documents\PowerShell\profile.ps1
- C:\Users\liusj\Documents\WindowsPowerShell\profile.ps1

最终版本采用同一 PowerShell 进程中的短期后台线程，保留原生 & EXE 调用；每 100ms 检查输入/输出代码页，只在变化时恢复 936。finally 停止线程并恢复命令启动前代码页；没有独立常驻服务。命令参数、退出码及标准输入输出由 PowerShell 处理；管道调用直接走官方入口。备用屏幕仍为 true。前文 ProcessStartInfo / 手工参数转义是候选实现，已由此最终实现替代。

备份：D:\project\VxKex-NEXT\.tools\opencode-runtime-20261101-171233。manifest.json 记录两个 Profile 的前后哈希及辅助脚本哈希。rollback.ps1 默认仅验证，-Apply 才恢复两个 Profile 并移除新增辅助脚本；若部署后有其他修改会拒绝覆盖。回滚预演通过，未实际回滚。此前初始 CP936 配置备份仍在 opencode-startup-20261101-165457。

### 本轮实际验证

1. 模拟子程序连续三次切回 65001，包装均在 500ms 采样时恢复输入/输出 936；带空值、空格、双引号、尾随反斜杠、中文的参数完整传递；退出码 23 透传；退出后原输入/输出 65001 恢复。
2. 候选最终脚本的真实隐藏 OpenCode TUI：找到 Ask 占位符，注入“继续”，未按回车或提交模型请求；字符单元回读 U+7EE7、U+7EED，随后为空格，无 s。输入前后代码页均为 936。
3. 正式部署后，启动正常加载 Profile 的新 pwsh（没有 -NoProfile）：Get-Command opencode 为 Function；版本为 opencode v2.2.5；管道及重定向版本命令均退出 0。
4. 在该正式 Profile 新窗口内再次启动真实 TUI 并输入“继续”，回读仍无 s，输入/输出均为 936。仅结束本轮创建的隐藏测试实例，用户当前会话保留。
5. 候选及正式 Profile 语法、源/目标 SHA256 核验通过。WindowsPowerShell 的配置按 5.1 语法编写，但本轮实际集成验证使用 pwsh 7；未另行宣称 Windows PowerShell 5.1 UI 通过。

证据：[运行期间检查](opencode-runtime-validation-20261101.json)、[正式 Profile 验证](opencode-installed-profile-20261101.json)、[真实 TUI 字符回读](opencode-runtime-tui-validation-20261101.json)。部署工具：[install-opencode-runtime-20261101.ps1](install-opencode-runtime-20261101.ps1)。

### 后续使用与边界

新开普通 PowerShell 后直接输入 opencode；现有 PowerShell 需要重新加载 Profile 或重开窗口才获得新函数。直接调用 opencode.exe / opencode.cmd、使用 -NoProfile 或管道入口不经过运行期间检查。npm 升级不会覆盖独立辅助脚本；若改变 portable 安装目录，应同步调整其 EXE 路径。

这是本机 Win7 控制台下已验证的启动兼容措施，尚未追踪切换代码页的具体调用栈，不将其称为 Bun/OpenTUI 源码层根治。每次切换与下一次检查间有至多约 100ms 的调度窗口；持续多窗口/其他语言/复杂重绘仍需日常观察。没有改写 npm 包、系统 DLL、IFEO 或外部项目，未构建和提交。
