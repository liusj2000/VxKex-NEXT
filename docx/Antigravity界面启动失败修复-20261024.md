# Antigravity 界面启动失败：原因、修复与验收（2026-10-24）

## 当前结论
已发现并修复 Antigravity 后端代理选择与 VS Code 设置冲突。修复后的独立后端使用原用户配置及插件环境标记，在 6790ms 返回 /healthz HTTP 200。安装扩展已备份、写入并通过语法检查；真实插件界面仍待重载后验收，不能将独立探测通过等同于界面已经恢复。

## 故障证据及与上一轮修改的关系
1. 用户截图报 Server failed to start at http://127.0.0.1:50039 after 30000ms。此错误是后端就绪等待失败，与 Codex Webview 就绪等待是不同链路。
2. 上轮 Cline 改了 waitForServerReady 的缺省回退值，但 start 先调用 Ac(configOverride) 取得默认 30000，再显式传给 waitForServerReady。因此该缺省补丁没有改变实际启动预算。不能因改动发生在前便认定补丁造成后端故障；现有证据不支持这一因果关系。
3. 本轮添加扩展正式支持的 antigravity.serverStartupTimeoutMs=120000，用户点击 Try again。PID 5068 从 13:15:24 启动，到 13:17:25 被扩展超时终止；日志明确记录 after 120000ms。用户反馈“仍在加载”，延长超时未解决。
4. 该进程日志 cli-20261024_131525.log 在 13:16:51 记录连接代理 127.0.0.1:10809 超时；初始化各阶段有数十秒间隔，截止终止仍未就绪。
5. 同机 VS Code 用户设置 http.proxy 为 http://10.0.0.1:10809；用户级 HTTP_PROXY、HTTPS_PROXY 却仍为 http://127.0.0.1:10809。诊断 shell 的进程级代理是新地址。旧扩展 buildServerEnvironment/xc 仅在继承环境没有代理时才填入 http.proxy，因此插件仍使用旧地址。这解释了同一后端独立启动快、插件启动慢的差异。
6. 只连接 TCP、不发送应用层数据的检查：旧地址超时，新地址端口约 4ms 可连接。端口可达本身不证明云端请求正常。
7. 本次没有证明所有初始化耗时均由代理导致，也未进行模型请求验证；已有真实旧代理超时日志、源码选择逻辑及修复后健康探测共同支持此故障链路。

## 修改范围与设计
- 保留 settings.json 中 antigravity.serverStartupTimeoutMs=120000 作为启动余量，不再继续无限增加。
- 仅在 google.google-antigravity-1.9.0/extension.js 的 xc 环境构造函数中修改一处：显式、非空的 VS Code http.proxy 同时赋给该后端子进程的 HTTP_PROXY/http_proxy/HTTPS_PROXY/https_proxy；无显式配置时保持继承行为。
- 不硬编码代理地址到已安装扩展，不改用户/机器环境变量，不改 Codex、系统 DLL、IFEO、聊天配置或凭据。
- 保留防重复下载的 manifest verification 补丁，保留 isServerHealthy 的显式 2 秒健康探测超时。
- 扩展模块在内存中缓存，因此磁盘修复需要 Developer: Reload Window；仅 Try again 不会重新加载已修改的 JS。
- 扩展升级会覆盖本地补丁。升级后应先检查官方代理选择行为与防重复下载机制，再决定是否重做；本脚本带版本内容哈希保护，不能盲目跨版本运行。

## 实际验证
- 直接原配置基线：PID 4796，2423ms，/healthz HTTP 200；此前记录 antigravity-profile-probe-20261024.jsonl。
- 正式配置解析测试 5 项通过：默认值、新设置、环境变量优先级、非法环境值及非法配置回退。该测试证明超时配置生效，不证明 UI 恢复。
- 修复候选 node --check 退出码 0，安装后再检查退出码 0。
- 从实际扩展源码提取 xc，用离线 VM 复现旧环境覆盖新设置；6 项测试通过，包括新设置覆盖旧代理、无设置保留环境、空白设置保留环境、trim、大小写变量一致及无环境时赋值。确认不修改调用方原环境对象、保留 NO_PROXY 和既有证书设置。
- 经用户明确授权后，使用修复后实际 xc 构造后端环境，断言最终两个代理均为新地址，再启动原 profile 后端。参数包含 --hub、动态端口、--app_data_dir=antigravity、测试 CSRF 值、当前工作区；环境含 AGY_ENABLE_HUB、ANTIGRAVITY_VSCODE_HOST 等插件标记。
- PID 2392，13:21:49 启动，6790ms 返回 /healthz HTTP 200；随后仅终止本次诊断子进程，确认退出。未发送模型请求。此探测不是 VS Code 内真实面板验收。
- 用户界面验收：已请用户保存工作并 Reload Window，当前待回执与新日志。
- 最初“双代理原账户启动对比”被自动审批拒绝，未执行。之后只做无应用数据的 TCP 检查、离线测试；用户明确回复“允许应用并验证”后，仅经已配置的新代理做上述验证。

## 备份、哈希与回滚
代理补丁安装文件：
D:\VSCode-win32-x64-1.107.1\data\extensions\google.google-antigravity-1.9.0\extension.js

备份：
D:\VSCode-win32-x64-1.107.1\data\antigravity-proxy-backup-20261024-132132\extension.js.original

修改前 SHA256：
57027F41332AEDD78E91917F2A2BD8E059EDCD612E63AE6AFF72A08AF2C61A06

修改后 SHA256：
9A5EA28267AC6860023235C1E7218523D659E2030E7B4D34F777EDD1BC146F17

仅回滚本次代理补丁（校验当前文件和备份哈希，文件已变化则拒绝覆盖）：
~~~powershell
pwsh -NoProfile -File "D:\project\VxKex-NEXT\docx\apply-antigravity-proxy-repair-20261024.ps1" -Rollback
~~~
回滚后仍需 Reload Window。回滚保留此前防重复下载及超时回退补丁；不会回滚 Codex 或系统代理。

120 秒用户设置的独立备份、前后哈希见 antigravity-startup-config-repair-20261024.json；如确需回滚该设置，可执行 verify-antigravity-startup-config-20261024.ps1 -Rollback。该工具要求当前整个 settings.json 哈希仍与修复后相同，否则拒绝覆盖后续用户修改。

## 证据与复现脚本
- antigravity-proxy-repair-20261024.json：补丁位置、哈希、备份、离线测试结果。
- prepare-antigravity-proxy-repair-20261024.cjs：候选生成及离线测试；对原内容哈希严格校验，已应用后不应重复运行。
- apply-antigravity-proxy-repair-20261024.ps1：应用及独立回滚。
- probe-antigravity-proxy-fixed-20261024.cjs、antigravity-proxy-fixed-probe-20261024.jsonl：修复后实际后端探测；联网验证需遵守本会话授权范围。
- test-antigravity-timeout-resolver-20261024.cjs、antigravity-timeout-resolver-test-20261024.json：正式超时设置解析。
- Codex 问题另见 Codex重启后界面无法显示原因与Cline修复复核-20261024.md。
