# KxBase 补充 `QueryInterruptTimePrecise` 等实时时钟 API 导出以支持 Antigravity 正常升级设计（2026-11-01）

## 1. 背景与问题陈述

在 Antigravity 扩展升级至 `1.10.0` 后，其配套后端二进制 `C:\Users\liusj\.gemini\bin\agy.exe` 升级为 `1.4.12`（基于 Go 1.24+ 运行时编译）。在 Windows 7 SP1 x64 + VxKex NEXT 环境下启动 `agy.exe`（例如执行 `agy.exe --version` 或启动 `--hub` 服务）时，进程在初始化阶段立即崩溃退出（退出码 `2`），错误栈如下：

```text
fatal error: QueryInterruptTimePrecise not found
runtime: panic before malloc heap initialized

runtime stack:
runtime.throw({0x14345d4c3?, 0x16eece?})
	third_party/go/gc/src/runtime/panic.go:1317 +0x6b fp=0x16ee28 sp=0x16edf8 pc=0x13f7903ab
runtime.loadOptionalSyscalls()
	third_party/go/gc/src/runtime/os_windows.go:281 +0x409 fp=0x16ef30 sp=0x16ee28 pc=0x13f74ffe9
runtime.osinit()
	third_party/go/gc/src/runtime/os_windows.go:483 +0x3d fp=0x16efb0 sp=0x16ef30 pc=0x13f7506dd
runtime.rt0_go()
	third_party/go/gc/src/runtime/asm_amd64.s:328 +0x111 fp=0x16efb8 sp=0x16efb0 pc=0x13f796471
```

此前临时方案通过修改 `extension.js` 将后端锁定在旧版 `agy-win7-1.4.6.exe`，但这会导致 Antigravity 后端无法随扩展升级而正常升级。用户明确要求在 VxKex NEXT 中实现并导出相应函数，从根源上支持新版 `agy.exe`（`1.4.12` 及后续版本）直接运行。

---

## 2. 逆向与调用链分析

### 2.1 `agy.exe` 1.4.12 (`runtime.loadOptionalSyscalls`) 完整探测链路

通过对 `C:\Users\liusj\.gemini\bin\agy.exe` 中 `runtime.loadOptionalSyscalls`（RVA `0x4fbe0`～`0x506a0`）的反汇编分析，确认其动态加载逻辑如下：
1. 调用 `LoadLibraryExW(L"api-ms-win-core-realtime-l1-1-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)`；
   - 在 VxKex NEXT 中，[`KexDll/redirects.h`](file:///D:/project/VxKex-NEXT/KexDll/redirects.h#L109) 已配置映射：
     `DLL_REDIRECT("api-ms-win-core-realtime", "kxbase")`
   - 因此该调用成功返回 `KxBase.dll` 的模块句柄；
2. 调用 `GetProcAddress(hRealtime, "QueryInterruptTimePrecise")`：
   - **若返回 `NULL`，立即调用 `runtime.throw("QueryInterruptTimePrecise not found")` 终止进程**；
   - 在获得 `QueryInterruptTimePrecise` 地址后，Go 运行时的 `runtime.nanotime1` 通过 `runtime.nanotimeQPC` 调用该函数获取 100ns 精度的单调中断时间；
3. 加载 `bcryptprimitives.dll`（映射至 `KxCryp.dll`）并解析 `ProcessPrng`（`KxCryp.dll` 已导出）；
4. 加载 `ntdll.dll`（映射至 `KxNt.dll`）并探测 `NtCreateWaitCompletionPacket`（若返回 `NULL` 则属于可选特性，自动回退至传统 `winmm.dll` 定时器机制，不会触发异常）；
5. 探测 `RtlGetCurrentPeb`（Win7 `ntdll.dll` 原生导出）及 `powrprof.dll!PowerRegisterSuspendResumeNotification`（`KxMi.dll` 已导出）。

### 2.2 `KxBase` 现有实现缺口

查阅 [`01-Extended DLLs/KxBase/time.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/time.c) 与 [`01-Extended DLLs/KxBase/kxbase.def`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/kxbase.def#L65-L68)：
- 目前仅实现了 `GetSystemTimePreciseAsFileTime` 与 `QueryUnbiasedInterruptTimePrecise`（Windows 8 `api-ms-win-core-realtime-l1-1-0`）；
- 缺失 Windows 10 `api-ms-win-core-realtime-l1-1-1` 新增的 **`QueryInterruptTimePrecise`** 与 **`QueryInterruptTime`**，以及 `api-ms-win-core-realtime-l1-1-2` 的辅助计数器接口。

---

## 3. 实现方案设计

### 3.1 新增 API 契约与实现逻辑（[`01-Extended DLLs/KxBase/time.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/time.c)）

1. **`QueryInterruptTimePrecise(OUT PULONGLONG InterruptTimePrecise)`**：
   - **官方契约**：返回系统中断时间计数（单位为 100 纳秒，即 $10^{-7}$ 秒），包含系统挂起/睡眠偏置，且具备硬件性能计数器（QPC）的亚微秒级精度与严格单调性。
   - **实现机制**：
     - 首次调用时通过原子状态机初始化基准：读取 `SharedUserData->InterruptTime` 作为起始基准 `g_BaseInterruptTime`（单位 100ns），同时调用 `QueryPerformanceFrequency(&Freq)` 与 `QueryPerformanceCounter(&BaseCounter)` 记录硬件计数器基准 `g_BaseQpc` 与频率 `g_QpcFrequency`；
     - 每次调用时读取当前 `QueryPerformanceCounter(&CurrentQpc)`，计算增量 `DeltaTicks = CurrentQpc.QuadPart - g_BaseQpc`；
     - 为防止 64 位整数乘法溢出，采用商余拆分法将 `DeltaTicks` 换算为 100ns 单位：
       $$\text{Delta100ns} = \left\lfloor\frac{\text{DeltaTicks}}{\text{Freq}}\right\rfloor \times 10^7 + \left\lfloor\frac{(\text{DeltaTicks} \bmod \text{Freq}) \times 10^7}{\text{Freq}}\right\rfloor$$
     - 返回 `g_BaseInterruptTime + Delta100ns`；
     - **优势**：不仅与内核 `InterruptTime` 起点对齐，而且提供真正的 100ns 级高精度单调时钟，同时即使进程开启了 `KEX_STRONGSPOOF_SHAREDUSERDATA`（导致 `SharedUserData` 页面因写时复制不再更新），基于 QPC 的时钟依然持续高精度递增，绝不卡死。

2. **`QueryInterruptTime(OUT PULONGLONG InterruptTime)`**：
   - **官方契约**：返回 `SharedUserData->InterruptTime` 当前的 100ns 单位中断时间计数。
   - **实现机制**：
     - 若开启了 `KEX_STRONGSPOOF_SHAREDUSERDATA`，直接转发至 `QueryInterruptTimePrecise` 防止读到冻结的写时复制页；
     - 否则通过 `High1Time` / `LowPart` / `High2Time` 一致性循环读取 `SharedUserData->InterruptTime`（兼容 32 位与 64 位无撕裂读取）。

3. **`api-ms-win-core-realtime-l1-1-2` 辅助计数器接口**：
   - `ConvertAuxiliaryCounterToPerformanceCounter`
   - `ConvertPerformanceCounterToAuxiliaryCounter`
   - `QueryAuxiliaryCounterFrequency`
   - 按 MSDN 规范，在无硬件辅助计数器的系统上校验输出指针非空后返回 `E_NOTIMPL`（若输出指针为空则返回 `E_POINTER`）。

### 3.2 导出表与头文件同步

- 在 [`00-Common Headers/KxBase.h`](file:///D:/project/VxKex-NEXT/00-Common%20Headers/KxBase.h) 中补充上述函数的 `KXBASEAPI` 声明；
- 在 [`01-Extended DLLs/KxBase/kxbase.def`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxBase/kxbase.def) 的 `;; time.c` 节导出：
  - `QueryInterruptTime`
  - `QueryInterruptTimePrecise`
  - `ConvertAuxiliaryCounterToPerformanceCounter`
  - `ConvertPerformanceCounterToAuxiliaryCounter`
  - `QueryAuxiliaryCounterFrequency`

### 3.3 Antigravity 1.10.0 扩展恢复原版自动升级逻辑

- 撤回此前将 `google.google-antigravity-1.10.0` 锁定到旧版 `1.4.6` 的临时补丁，或在兼容层尚未部署前采取“优先探测官方最新版 `agy.exe`（`1.4.12+`），仅在缺失 `QueryInterruptTimePrecise` 时回退”的自适应策略，确保用户安装新版 `KexSetup_Release.exe` 后 Antigravity 直接使用最新 `1.4.12` 后端并支持后续无缝升级。


---

## 4. 构建、测试与实测验证结果

### 4.1 单元测试验证（[`test/ansi-features-test.c`](file:///D:/project/VxKex-NEXT/test/ansi-features-test.c)）

在 `test/ansi-features-test.c` 中新增 **Test 7 (`QueryInterruptTime` & `QueryInterruptTimePrecise`)**，验证导出函数地址非空、返回值大于 0、`QueryInterruptTimePrecise` 相对于 `QueryInterruptTime` 锚点单调不减，且经过 `Sleep(2)` 后高精度计数值严格递增。实测输出：

```text
--- Test 7: QueryInterruptTime & QueryInterruptTimePrecise (Go 1.24 / Antigravity 1.4.12) ---
InterruptTime=4541880212, Precise0=4541880222, Precise1=4541901053 (delta=20831 100ns)

=== SUMMARY ===
ALL TESTS PASSED SUCCESSFULLY! (failures = 0)
```

### 4.2 `agy.exe` 1.4.12 真实进程端到端验证

在尚未覆盖 `C:\Program Files\VxKex\Kex64\KxBase.dll` 的前提下，通过调试器加载重定向将子进程 `C:\Users\liusj\.gemini\bin\agy.exe`（`1.4.12`）指向本次新编译的 `KxBase.dll`，实测结果如下：
- 执行 `C:\Users\liusj\.gemini\bin\agy.exe --version`：
  - 标准输出：`1.4.12`
  - 进程退出码：`0`（不再报 `fatal error: QueryInterruptTimePrecise not found`）
- 执行 `C:\Users\liusj\.gemini\bin\agy.exe --help`：
  - 完整输出 CLI 帮助信息与子命令列表，进程退出码：`0`。

### 4.3 Antigravity 1.10.0 自适应升级策略（[`docx/antigravity-win7-backend-20261101.cjs`](file:///D:/project/VxKex-NEXT/docx/antigravity-win7-backend-20261101.cjs)）

已将扩展 `D:\VSCode-win32-x64-1.107.1\data\extensions\google.google-antigravity-1.10.0\extension.js` 更新为 V2 自适应策略（SHA256: `042c3a4da6ae4a2d56e35602cdd31add4adaa9e312d93a154cb9f61452974b2b`）：
- 启动时自动检测已安装的 `C:\Program Files\VxKex\Kex64\KxBase.dll` 是否包含 `QueryInterruptTimePrecise` 导出；
- **当用户以管理员身份运行新版 `KexSetup_Release.exe` 完成升级后**：`kxAcquireWin7Backend` 自动走官方原生 `Fc(options)` 流程，直接启用 `agy.exe` `1.4.12` 并完全放开后续官方版本升级；
- **在用户尚未运行安装器前**：自动回退至 `agy-win7-1.4.6.exe`，防止在旧版 `KxBase.dll` 下崩溃或陷入重复下载。

### 4.4 全量构建与安装包产物

- 全量安装器：`D:\project\VxKex-NEXT\KexSetup_Release.exe`
  - 大小：`5,035,179` 字节
  - SHA256：`7fd868685c4ccfea346754815fac9f5a491039fc28493ccc7efbec8207ba49ac`
- 时间戳归档副本：`D:\project\VxKex-NEXT\KexSetup_Release_1.2.3.2463_20261101_1354.exe`
- x64 扩展 DLL：`D:\project\VxKex-NEXT\x64\Release\KxBase.dll`
  - 大小：`162,816` 字节
  - SHA256：`cdeeccfba32483d4997b8d86eaf673087288048ca701fa3db70a9a7b5fbcfe0f`
- Win32 扩展 DLL：`D:\project\VxKex-NEXT\Release\KxBase.dll`
  - 大小：`151,552` 字节
