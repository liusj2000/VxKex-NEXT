# Antigravity 每次启动重复下载安装：原因与修复（2026-10-23）

## 处理结果

已给本机 Google Antigravity **1.9.0** 扩展应用修复，原文件已备份并记录 SHA256。版本探测失败时，如果现有后端与当前发布清单的 SHA512/SHA256 完全一致，就复用现有文件，避免重复下载约 200 MB 的相同后端。正常版本升级仍按原流程进行。

**生效操作：保存工作后，在 VS Code 按 `Ctrl+Shift+P`，执行 `Developer: Reload Window`（开发人员：重新加载窗口），或下次正常重启 VS Code。** 当前已加载的扩展仍在使用内存中的旧代码。本次未强制重载用户窗口，真实窗口重载后的表现尚待观察，不能写成 UI 已验证通过。

## 实际位置与版本

| 项目 | 实测值 |
| --- | --- |
| VS Code | `D:\VSCode-win32-x64-1.107.1\Code.exe`，便携模式 |
| 扩展 | `D:\VSCode-win32-x64-1.107.1\data\extensions\google.google-antigravity-1.9.0` |
| 后端 | `C:\Users\liusj\.gemini\bin\agy.exe` |
| 后端版本 | `1.4.6`，`--version` 实际退出码 0 |
| 后端大小 | 204,022,936 字节，约 194.6 MiB |
| 安装日志 | `C:\Users\liusj\.gemini\logs\install.log` |
| VS Code 日志 | `D:\VSCode-win32-x64-1.107.1\data\user-data\logs` |

截图中的 `Installing Antigravity Backend: Downloading` 是扩展下载独立后端，不是重新安装整个 VS Code 或 Windows SDK。

## 已确认原因

历史日志累计出现 **43 次版本探测执行失败**、**23 次发现下载版本的记录**。其中 1.4.2、1.4.5、1.4.6 等相同版本均有重复下载。典型链路为：

```text
[INSTALL] Failed to execute binary ...\agy.exe: Error: Command failed: ...\agy.exe --version
[INSTALL] Found release v1.4.6 ...
[INSTALL] Downloading Antigravity Backend to temporary path ...
[INSTALL] SHA512 checksum verified successfully.
[INSTALL] Antigravity Backend successfully installed ...
[LAUNCH] Server at http://127.0.0.1:... is READY ...
```

扩展原安装判断的缺陷是：

1. `inspectBinaryVersion` 只尝试 `execFile(agy.exe, ['--version'], {timeout:5000})`。执行失败就认为版本不可用。
2. 即使磁盘文件完整、与最新发布内容完全相同，也没有用清单校验和确认现有文件，而是进入下载。
3. 下载成功后，版本只缓存于扩展宿主内存 `Map`。重载宿主后缓存丢失，重新触发失败探测与下载。
4. 另有版本缓存来源错误：两次获取清单之间版本变化，或前一次清单请求失败回退到最低版本时，下载后仍可能缓存旧的目标值。历史日志中存在实际下载 1.4.5 却把启动版本标记为 1.1.11 的记录。

**底层启动失败原因尚未完全定位。** 本次在普通命令行、VS Code 自带 Node v22.21.1、无控制台 Node 父进程三种条件下，当前 `--version` 都能成功，实测通常不到 1 秒；没有证据证明历史失败一定由 5 秒超时引起。旧失败日志未记录 code/signal/killed，不能据此断言是代理、文件权限、杀毒清理或某个 Win7 API。

本次独立诊断运行时还遇到过 VxKex 日志中的 `FlsGetValue2` 导出缺失，相关测试进程被清理后重试成功。此项是兼容环境的独立观测，**不能直接当作历史 agy 版本探测失败的根因**。

## 完整性核验与修复逻辑

通过 Node HTTPS 获取官方发布清单时明确启用证书验证，未通过关闭 TLS 校验获取清单：

`https://antigravity-cli-auto-updater-974169037036.us-central1.run.app/manifests/windows_amd64.json`

清单版本 `1.4.6`，SHA512 与磁盘现有文件完全一致：

```text
56e6f15ce99d349ae8186d4e049a1ca8c7002e09447b5498da06581bd02843641bc95aa37046b0121dd3783c3a863135edbc0650b409a0e6b562c60e76240d22
```

补丁包含以下行为：

- 将版本探测超时调整为 30 秒并隐藏探测窗口；失败日志补充 `code`、`signal`、`killed`。
- 仅当版本探测失败、已获得当前发布清单且未切换发布通道时，验证现有后端的 SHA512/SHA256。
- 摘要格式合法、摘要匹配、校验前后文件指纹一致时，把**本次清单版本**存入原进程内版本缓存，沿用原来的“版本有效，跳过下载”分支。
- 文件缺失、摘要不符、校验期间文件改变、没有校验和、切换通道时，仍按原流程处理。清单无法获取且探测也失败的情况没有新增离线复用保证。
- 下载成功后的版本缓存改用实际下载目标 `Kc.targetVersion`，使启动版本与本次下载一致。

修复后应能看到类似日志：

```text
[INSTALL] Reusing existing backend v1.4.6 after SHA512 manifest verification (version probe failed).
[INSTALL] Installed binary is valid (actual version 1.4.6 >= target 1.4.6). Skipping download.
```

如果 `--version` 正常，则直接走原有有效版本判断；如果官方确实发布新后端，仍会下载新版本。联网检查清单和计算本地摘要也会消耗少量时间。

## 备份、脚本与回滚

已安装补丁文件：

`D:\VSCode-win32-x64-1.107.1\data\extensions\google.google-antigravity-1.9.0\extension.js`

原始文件备份与修复记录：

```text
D:\VSCode-win32-x64-1.107.1\data\antigravity-repair-backups\google.google-antigravity-1.9.0\extension.js.original
D:\VSCode-win32-x64-1.107.1\data\antigravity-repair-backups\google.google-antigravity-1.9.0\repair.json
```

| 文件 | SHA256 |
| --- | --- |
| 原文件 | `F77A37C4BD66612F4ED26D9E80CE3969AF543F853E2CC113CA466E7F8B6AA880` |
| 已安装补丁 | `8CB67D343E4D4B407EB28204BC1014BC714567B792DAA1989BBBA7CDE64333AE` |

校验当前安装与原始备份：

```powershell
pwsh -NoProfile -File "D:\project\VxKex-NEXT\docx\repair-antigravity-backend-cache.ps1" -Mode Verify
```

需要撤销本次扩展补丁时：

```powershell
pwsh -NoProfile -File "D:\project\VxKex-NEXT\docx\repair-antigravity-backend-cache.ps1" -Mode Rollback
```

随后重新加载窗口。回滚前会验证备份和当前文件哈希；扩展已被更新或另行修改时拒绝覆盖。

本补丁只适用于 **1.9.0**。扩展升级或重装可能覆盖 `extension.js`；以后遇到新版，先核对新版代码是否已修复，不把本补丁强行套入其他版本。未关闭全局自动更新、未更换官方发布地址。脚本重复 Apply 为幂等操作。

## 验证记录与边界

- 候选扩展 JavaScript `--check`：退出码 0。
- 提取候选补丁中实际 `acquireInstalledBinaryPath`、版本探测、摘要计算及新增复用函数执行，**14 项行为检查通过**。网络与下载动作使用受控替身；不进行额外的 200 MB 下载。
- 覆盖有效版本、探测失败/超时且摘要一致、摘要不符、文件缺失、旧版本升级、切换通道、缺失/非法摘要、校验期间替换、离线分支。
- 两项使用真实已安装 `agy.exe`：与官方清单做完整摘要核验后跳过下载，以及实际 `--version` 探测成功后跳过下载。
- 独立副本验证 Apply、再次 Apply、Verify、Rollback，回滚后与原始文件逐字节对应的 SHA256 一致。
- 已安装 `extension.js` 的哈希与通过检查的候选文件一致；原始备份校验通过。
- **未执行实际 VS Code 窗口重载、重载后的 UI 验收与聊天功能回归**；现有聊天与工作窗口未主动中断。
- 本次未修改 VxKex 源码、DLL、IFEO、系统代理或其他项目。现场有其他并行工作产生的源码变动，保留原样；本次未提交 Git。

证据和可复用脚本均在 `D:\project\VxKex-NEXT\docx`：

- [修复设计](Antigravity重复下载安装修复设计-20261023.md)
- [修复与回滚脚本](repair-antigravity-backend-cache.ps1)
- [版本探测脚本](antigravity-version-probe.cjs)、[普通探测结果](antigravity-version-probe-20261023.txt)、[无控制台探测结果](antigravity-version-probe-noconsole-20261023.txt)
- [安装历史证据（仅安装及就绪行，去除认证与 CSRF 行）](antigravity-install-evidence-20261023.txt)
- [官方清单快照](antigravity-release-windows-amd64-20261023.json)
- [行为检查脚本](antigravity-cache-repair-test.cjs)、[14 项检查结果](antigravity-cache-repair-test-20261023.txt)
- [补丁生命周期与回滚检查](antigravity-repair-lifecycle-test-20261023.txt)