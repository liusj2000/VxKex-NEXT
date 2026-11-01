# Antigravity 1.10.0 启动失败修复设计（2026-11-01）

## 当前证据

当前后端 1.4.12 已下载并通过官方 SHA512，启动即在 Go runtime.loadOptionalSyscalls 中报 QueryInterruptTimePrecise not found、panic before malloc heap initialized，日志退出码 2。Windows 7 缺失此 API，KxBase 也没有该导出。该错误早于服务器和代理初始化，不能用增加就绪超时或更换代理修复。

## 修复方案与验证顺序

1. 按本仓库 2026-10-23 留存的官方发布 URL 取回 1.4.6，仅在 SHA512 与留存官方清单一致后使用。先在独立目录实测 --version、--help 和隐藏后端 /healthz。
2. 若通过，后端另存为 C:\Users\liusj\.gemini\bin\agy-win7-1.4.6.exe，保留现有 agy.exe 与其官方版本 sidecar。扩展 1.10.0 的 acquireInstalledBinaryPath 在实际 Win7 上返回经哈希校验、真实版本检查通过的兼容后端路径。其他系统保留原流程。
3. 仅在 Win7 固定经过验证的兼容版本，明确记录真实版本；不伪造新版本、不改变已下载 1.4.12 的字节或官方摘要，不改系统 DLL/IFEO，不尝试旧 1.9.0 补丁的跨版本直接替换。
4. 原扩展 1.10.0 已有 sidecar/超时复用，但启动失败强制版本重检仍会重新下载同一后端。Win7 兼容分支同时避免此已知重复下载路径。
5. 修改前备份扩展 JS/元数据到仓库 .tools，记录前后 SHA256；候选 node --check、离线分支/损坏文件/真实版本测试通过后部署。部署后回读核验，并用扩展实际环境构造逻辑运行已选后端做健康和页面请求验证。
6. 内存中已加载的旧扩展需要重载窗口才会替换；优先利用可用的非破坏性重新激活能力，若当前会话无法重载，则明确将真实界面验收标记为待用户操作，不能把独立后端成功说成 UI 已恢复。

当前代理是 192.168.1.199:10809，历史备用地址本次已不可达。修复不沿用 10 月 24 日的旧代理选择结论；之后按实际后端日志决定是否需要处理额外故障。


---

## 根本解决：在 VxKex NEXT (`KxBase.dll`) 中实现并导出 `QueryInterruptTimePrecise` 以支持 Antigravity 原生升级

根据用户要求“不希望 Antigravity 不能升级，在 VxKex NEXT 中实现并导出相应的函数”：

1. **VxKex NEXT (`KxBase.dll`) 原生支持**：
   - 已在 [`01-Extended DLLs/KxBase/time.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/time.c)、[`01-Extended DLLs/KxBase/kxbase.def`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/kxbase.def) 与 [`00-Common Headers/KxBase.h`](file:///D:/project/VxKex-NEXT/00-Common%20Headers/KxBase.h) 中完整实现并导出 `QueryInterruptTimePrecise`、`QueryInterruptTime` 以及 `api-ms-win-core-realtime-l1-1-2` 辅助计数器接口；
   - 由于 [`KexDll/redirects.h`](file:///D:/project/VxKex-NEXT/KexDll/redirects.h) 已将 `api-ms-win-core-realtime` 映射至 `kxbase.dll`，新版 `agy.exe` (`1.4.12` 及后续升级版本) 启动时通过 `LoadLibraryExW(L"api-ms-win-core-realtime-l1-1-1.dll", ...)` 与 `GetProcAddress(..., "QueryInterruptTimePrecise")` 即可直接解析到 `KxBase.dll` 的实现；
   - 经实测，加载新版 `KxBase.dll` 后，`C:\Users\liusj\.gemini\bin\agy.exe --version` 正常输出 `1.4.12`（退出码 `0`），`--help` 正常输出全部帮助信息（退出码 `0`）。
2. **扩展脚本自适应放开升级（[`docx/antigravity-win7-backend-20261101.cjs`](file:///D:/project/VxKex-NEXT/docx/antigravity-win7-backend-20261101.cjs) V2）**：
   - 将 `kxAcquireWin7Backend` 更新为自适应检测：一旦检测到 `C:\Program Files\VxKex\Kex64\KxBase.dll` 已包含 `QueryInterruptTimePrecise` 导出（即用户运行新版 `KexSetup_Release.exe` 安装后），立即委托给官方原版 `Fc(options)`，直接使用 `1.4.12` 并支持未来所有版本正常自动升级；仅在旧版 `KxBase.dll` 尚未升级前临时回退至 `1.4.6`。
3. **详细设计文档**：参见 [`docx/KxBase补充QueryInterruptTimePrecise导出支持Antigravity升级设计-20261101.md`](file:///D:/project/VxKex-NEXT/docx/KxBase%E8%A1%A5%E5%85%85QueryInterruptTimePrecise%E5%AF%BC%E5%87%BA%E6%94%AF%E6%8C%81Antigravity%E5%8D%87%E7%BA%A7%E8%AE%BE%E8%AE%A1-20261101.md)。


---

## 根本解决：在 VxKex NEXT (`KxBase.dll`) 中实现并导出 `QueryInterruptTimePrecise` 以支持 Antigravity 原生升级

根据用户要求“不希望 Antigravity 不能升级，在 VxKex NEXT 中实现并导出相应的函数”：

1. **VxKex NEXT (`KxBase.dll`) 原生支持**：
   - 已在 [`01-Extended DLLs/KxBase/time.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/time.c)、[`01-Extended DLLs/KxBase/kxbase.def`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/kxbase.def) 与 [`00-Common Headers/KxBase.h`](file:///D:/project/VxKex-NEXT/00-Common%20Headers/KxBase.h) 中完整实现并导出 `QueryInterruptTimePrecise`、`QueryInterruptTime` 以及 `api-ms-win-core-realtime-l1-1-2` 辅助计数器接口；
   - 由于 [`KexDll/redirects.h`](file:///D:/project/VxKex-NEXT/KexDll/redirects.h) 已将 `api-ms-win-core-realtime` 映射至 `kxbase.dll`，新版 `agy.exe` (`1.4.12` 及后续升级版本) 启动时通过 `LoadLibraryExW(L"api-ms-win-core-realtime-l1-1-1.dll", ...)` 与 `GetProcAddress(..., "QueryInterruptTimePrecise")` 即可直接解析到 `KxBase.dll` 的实现；
   - 经实测，加载新版 `KxBase.dll` 后，`C:\Users\liusj\.gemini\bin\agy.exe --version` 正常输出 `1.4.12`（退出码 `0`），`--help` 正常输出全部帮助信息（退出码 `0`）。
2. **扩展脚本自适应放开升级（[`docx/antigravity-win7-backend-20261101.cjs`](file:///D:/project/VxKex-NEXT/docx/antigravity-win7-backend-20261101.cjs) V2）**：
   - 将 `kxAcquireWin7Backend` 更新为自适应检测：一旦检测到 `C:\Program Files\VxKex\Kex64\KxBase.dll` 已包含 `QueryInterruptTimePrecise` 导出（即用户运行新版 `KexSetup_Release.exe` 安装后），立即委托给官方原版 `Fc(options)`，直接使用 `1.4.12` 并支持未来所有版本正常自动升级；仅在旧版 `KxBase.dll` 尚未升级前临时回退至 `1.4.6`。
3. **详细设计文档**：参见 [`docx/KxBase补充QueryInterruptTimePrecise导出支持Antigravity升级设计-20261101.md`](file:///D:/project/VxKex-NEXT/docx/KxBase%E8%A1%A5%E5%85%85QueryInterruptTimePrecise%E5%AF%BC%E5%87%BA%E6%94%AF%E6%8C%81Antigravity%E5%8D%87%E7%BA%A7%E8%AE%BE%E8%AE%A1-20261101.md)。
当前代理是 192.168.1.199:10809，历史备用地址本次已不可达。修复不沿用 10 月 24 日的旧代理选择结论；之后按实际后端日志决定是否需要处理额外故障。
