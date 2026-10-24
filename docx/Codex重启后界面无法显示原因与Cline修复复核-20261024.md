# Codex 重启后界面无法显示：原因与 Cline 修复复核（2026-10-24）

## 结论

**本次可确认的直接故障是 Codex Webview 的 30 秒启动等待到期，扩展提前用错误页替换了仍在加载的界面。Cline 将等待时间延长为 120 秒；之后重载/重新启动扩展，界面完成初始化并恢复显示。**

这次已经有用户反馈和恢复后的日志相互印证，不再只是“补丁已写入、待 UI 验收”。不过，延长超时解决的是过早判定失败的问题，并没有使冷启动本身变快。为什么本机初始化需要几十秒，仍需性能排查。

- 本机环境：Windows 7 SP1 x64 + VxKex，VS Code 便携版位于 D:\VSCode-win32-x64-1.107.1。
- 此次 Codex 扩展版本：26.1021.22027-win32-x64。
- 以下时间统一使用北京时间（UTC+8）。
- 本轮仅复核并补充记录，没有再次改写扩展、重载窗口、修改 IFEO 或运行安装器。

## 1. 故障发生在哪一层

Codex 扩展的后端 app-server 与侧栏 Webview 是两个不同的启动环节。

失败会话中：
1. 12:04:27.721：扩展激活并启动 app-server。
2. 12:04:38.891：记录 Initialize received id=1，说明后端已经回应初始化。
3. 12:05:04.619：Webview 报 renderer_ready_timeout，elapsedMs=30031、timeoutMs=30000、receivedWebviewMessage=false。

因此，直接阻断发生在**侧栏前端尚未向扩展宿主报告就绪**的阶段。这条证据不支持把本次故障写成“codex.exe 缺 DLL 无法启动”“登录失败”或“OpenAI API 故障”。

检查安装版本的 out/extension.js 可见：
- 启动监视器原本只等待 3e4 毫秒，即 30 秒。
- 接收到 renderer_ready 后才销毁定时器；一般 Webview 消息仅设置 receivedWebviewMessage 标记。
- 一旦定时器到期，回调会记录上述错误，将 e.html 替换为包含 “Codex could not start” 的静态错误页，并显示启动失败提示。
- 所以它不只是报告“稍慢”，还会替换正在初始化的页面，用户继续等待也可能看不到原界面。

这是此次“后端已经运行，插件界面却出不来”的具体机制，与 2026-10-17 缺少 API 导出导致的后端加载失败应分开记录。

## 2. Cline 实际做了什么

操作依据是仓库内保存的补丁脚本、执行报告、原始备份及当前文件哈希；用户也明确说明是通过 Cline 处理后恢复。

### 2.1 排查和准备

- 读取 VS Code 的 Codex、renderer、扩展宿主日志，区分后端初始化与 Webview 超时。
- 检查 Webview 静态资源：记录 assets=22385、importReferences=107628、missing=0。
  这只代表该正则检查范围内未发现缺失引用，不等于执行过所有浏览器脚本。
- 保存扩展原文件，在独立 candidate.js 中完成修改并执行 Node --check，再写回安装位置。

相关记录：
- D:\project\VxKex-NEXT\docx\codex-assets-inspect-20261024.txt
- D:\project\VxKex-NEXT\docx\repair-extension-startup-20261024.cjs
- D:\project\VxKex-NEXT\docx\extension-startup-repair-20261024.json

### 2.2 Codex 补丁

目标文件：

D:\VSCode-win32-x64-1.107.1\data\extensions\openai.chatgpt-26.1021.22027-win32-x64\out\extension.js

修改限于启动监视器的一处匹配片段：
- 日志字段 timeoutMs:3e4 改为 timeoutMs:12e4。
- 实际 setTimeout 的 3e4 改为 12e4。
- 仍必须收到 renderer_ready 才算就绪，没有伪造成功，也没有关闭启动检查。

执行报告时间是 2026-10-24T04:17:51.145Z，即北京时间 **12:17:51.145**。报告记载语法检查退出码 0、复制后哈希与候选一致。

备份目录：

D:\VSCode-win32-x64-1.107.1\data\extension-startup-backup-20261024

| 文件状态 | SHA256 |
| --- | --- |
| 原始 codex.extension.js.original | A9CC6736CD2C2D22F6CF43FC5763D5F8A873E4F741F522B44A600B1926F4E627 |
| 修改后 out/extension.js | B3E3A8B193903AAB19F4B85DC3360F6B6E5C12A8F6BFF427852481E7356341D4 |

本轮再次确认：当前安装文件与 afterHash 相符，备份与 beforeHash 相符；再次执行 Node --check 退出码为 0。

### 2.3 生效操作

补丁写入磁盘不会改变已经加载到扩展宿主内存中的代码，需要执行 Developer: Reload Window 或重新打开 VS Code。

日志显示 12:20:01 再次激活扩展，之后成功加载界面；13:04:56 又有一次激活及成功记录。现有日志足以确认发生了扩展重新激活，不据此推断用户具体点击了哪个按钮，或是否再次重启了操作系统。

## 3. 为什么能确认此次处理有效

| 阶段 | 记录 | 判断 |
| --- | --- | --- |
| 修改前，12:05:04 | renderer_ready_timeout；30031ms；receivedWebviewMessage=false | 界面在原 30 秒期限内没有就绪 |
| 修改后，12:20:05 | 后端 Initialize received | 后端初始化正常 |
| 修改后，12:20:59 | 账号查询 succeeded | 恢复过程中账号查询成功 |
| 修改后，12:21:20 的性能记录 | renderer_ready=45053.2ms；home_visible=57116ms；app_start outcome=success | 界面约 45.1 秒才就绪，首页约 57.1 秒可见 |
| 再次激活后，13:05:29 的性能记录 | renderer_ready=33569.2ms；home_visible=40010.9ms；app_start outcome=success | 界面约 33.6 秒才就绪，首页约 40.0 秒可见 |

两个成功启动的 renderer_ready 时间都超过原来的 30 秒，且低于新的 120 秒。结合源代码定时器逻辑、修改前超时日志、修改后成功日志和用户确认，可以确认延长等待是本次恢复的有效措施。

注意：性能 marksMs 与宿主超时定时器并非保证同一计时原点；第一条成功记录还标有 timingQuality=heartbeat_gap，wallDurationMs=65900.2。因此不能把这些数字当成毫秒级性能基准，也不能断言第一次失败若多等恰好多少秒就一定成功。它们足以支持“实际启动较慢、原 30 秒预算不足”的判断。

原始日志：
- D:\VSCode-win32-x64-1.107.1\data\user-data\logs\20261024T120404\window1\exthost\openai.chatgpt\Codex.log
- D:\VSCode-win32-x64-1.107.1\data\user-data\logs\20261024T130435\window1\exthost\openai.chatgpt\Codex.log

已保存精简证据：
D:\project\VxKex-NEXT\docx\codex-restart-recovery-evidence-20261024.txt

## 4. 为什么重启后容易暴露，哪些原因尚未证明

目前可以确认前端加载慢，尚不能从这些日志唯一确定慢在哪里。

已有线索：
- Webview 有较多资源；重启后的磁盘读取、缓存预热和脚本执行成本值得检查，但没有冷/热启动性能对照证明。
- 失败会话出现 SQLite 连接获取约 2 秒、wal_checkpoint 约 2.79 秒的慢操作；这说明启动期存在延迟，不足以证明它就是 Webview 超时的唯一根因。
- 恢复后仍有 IPC 初始化超时、模型列表请求超时、ResizeObserver 等日志，但 app_start 已成功；不应把日志中所有 error 都解释成同一个启动故障。
- 本轮 pwsh -NoProfile 仍输出 InitializeDefaultDrives 错误，失效 Y: 共享挂载问题需要单独处理。没有证据证明修复 Y: 是这次 Codex 恢复的措施。

因此应写成：**直接故障机制已确认，启动速度的深层原因仍未完全定位；120 秒补丁是已经奏效的兼容性缓解。**

## 5. 与其他改动的边界

- 同一 Cline 脚本还修改了 Antigravity 的默认就绪等待逻辑，并保留原 manifest 摘要复用补丁。这是另一条服务启动链路，不能当作 Codex 修复的原因。
- 本轮检查 Antigravity 当前哈希与修复报告一致，防重复下载标记仍存在；但是 13:04 会话日志中它仍两次以 after 30000ms 结束就绪探测。这说明不能宣称 Antigravity 也已恢复，默认值补丁是否被显式等待值覆盖需要另查。
- 本轮读取的已安装 x64 KxBase.dll SHA256 为 B19E1D7D5835E859694750CBDB88A1973C0682B54CE69A3E8282BF3635B7FE11，已经与上一轮终端候选产物一致。旧交接中的“仍未部署”不能继续当作当前事实；但本轮没有执行部署，不能由哈希推断安装时间、操作人或其它组件的部署状态。
- 上述终端 DLL 状态与 Codex 前端启动超时应分别记录，不能据此声称 DLL 补丁修复了 Webview。

## 6. 后续使用、复发处理和回滚

1. 当前 Codex 界面已恢复，保留现有补丁即可；不需要为了本次现象重新登录、删除会话或再装 VxKex。
2. 扩展升级可能覆盖 out/extension.js。升级后若复发，先确认新版本、renderer_ready_timeout 和实际 timeoutMs，再检查新版本初始化逻辑。
3. 现有修复脚本针对 26.1021.22027 的精确片段，不是适配任意版本的通用补丁，也不适合在已修复文件上反复执行。应先备份新版并核对匹配，保留厂商已有修复。
4. 若 120 秒仍不能就绪，不再无依据增加时间；收集同一会话 Codex.log、renderer.log 及 Webview 开发者工具第一条脚本/资源错误，进一步测量资源加载和初始化耗时。
5. 回滚命令：
   pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\run-extension-startup-repair-20261024.ps1 -Rollback
   该命令会同时尝试撤销报告中的 Codex 和 Antigravity 两项超时补丁，运行前核对影响范围。
6. 回滚脚本先核对当前文件 afterHash 和备份 beforeHash，存在后续改动就拒绝覆盖；回滚后要重新加载窗口。该分支本轮未运行。PowerShell 包装器异步启动 Node，不能只凭包装器退出码判断完成，需要查看 extension-startup-repair-20261024.json.rollback.json 及文件哈希。
7. 保留用户已有会话、账号和扩展数据，不用删除缓存来代替诊断。

官方背景资料：[Codex IDE extension](https://learn.chatgpt.com/docs/codex/ide)。官方通用文档不能证明这台 Win7 机器的具体故障；本报告的原因、补丁和恢复结论以本地日志、文件与用户反馈为依据。
