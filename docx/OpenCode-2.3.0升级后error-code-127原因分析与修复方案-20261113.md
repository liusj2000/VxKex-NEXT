# OpenCode 升级 2.3.0 后报 `error code 127` 无法启动根因分析与修复方案

**记录日期**：2026-11-13（初版），2026-11-14（复核修订）  
**状态**：2026-11-14 已实施、部署并**验收通过**（实施与验收记录见第六节）  
**目标环境**：Windows 7 专业版 SP1 x64 (Build 7601)  
**涉及模块与文件（修订后）**：
- [`D:\project\VxKex-NEXT\00-Common Headers\NtDll.h`](../00-Common%20Headers/NtDll.h)（删除一段过时声明）
- [`D:\project\VxKex-NEXT\00-Common Headers\KexDll.h`](../00-Common%20Headers/KexDll.h)
- [`D:\project\VxKex-NEXT\KexDll\ntvmem.c`](../KexDll/ntvmem.c)
- [`D:\project\VxKex-NEXT\KexDll\KexDll.def`](../KexDll/KexDll.def)
- [`D:\project\VxKex-NEXT\01-Extended DLLs\KxNt\kxnt.def`](../01-Extended%20DLLs/KxNt/kxnt.def)
- ~~`KexDll\dllrewrt.c`~~（初版要求修改，复核后确认**不需要也不应该改**，原因见第零节第 3 条）

---

## 零、 2026-11-14 复核结论与修订说明

**根因结论成立**：缺的就是 `kxnt.dll` 的 `NtSetInformationVirtualMemory` 导出，而且整个 DLL 只缺这一个静态导入。  
**初版修复方案不能照原样实施**：按原文第四节写代码，`KexDll` 工程编译不过。下面逐条说明改了什么、为什么改。凡标注"实测"的，都是本次在仓库外用同一套编译器和参数实际跑出来的结果，仓库源码未动。

| # | 初版内容 | 问题 | 修订 | 依据 |
|---|---|---|---|---|
| 1 | 步骤 1 在 `KexDll.h` 里新定义 `MEMORY_RANGE_ENTRY` 结构体 | 该结构体在 [`NtDll.h` 第 1196 行](../00-Common%20Headers/NtDll.h#L1196)已经定义，重复定义直接报错 | 不新增结构体，直接用现有的 | 实测：`error C2011: '_MEMORY_RANGE_ENTRY' : 'struct' type redefinition` |
| 2 | 步骤 1、2 用 `KEXAPI` 声明并定义 `NtSetInformationVirtualMemory` | [`NtDll.h` 第 3962 行](../00-Common%20Headers/NtDll.h#L3962)已有同名原型，修饰符是 `NTSYSCALLAPI`（即 `dllimport`）。`KexDll` 工程内 `KEXAPI` 为空，同一函数先声明为导入、再定义为本地函数，触发 C4273；`KexDll` 工程开了"警告视为错误" | 删除 `NtDll.h` 里那段原型，把原型放到 `KexDll.h` 用 `KEXAPI` 声明（与 `NtCompareObjects` 的做法一致） | 实测：`warning C4273: inconsistent dll linkage` → `error C2220`。那段旧原型本身不可用：Win7 的 `ntdll.dll` 和仓库的 `ntdll_x64.lib` / `ntdll_x86.lib` 都没有这个符号，全仓库也没有任何调用点 |
| 3 | 步骤 4 在 `dllrewrt.c` 的 `NtToKexMap` 增加 `Nt` / `Zw` 两项 | 与本故障无关。`NtToKexMap` 只被 `KexRewriteKernelDllImageImportDirectory` 使用，它的唯一调用点是 `KexDllNotificationCallbackForWindows8AndAbove`，而该回调只在 `KexShouldUseWorkaroundsForNewerWindows()` 为真时注册（[`dllmain.c` 第 210–215 行](../KexDll/dllmain.c#L210-L215)），是给 Win8 及以上宿主用的分支。本故障走的是另一条路：应用 DLL 的 `ntdll.dll` 导入描述符被整体改名为 `kxnt`，只看 `kxnt.def` 的导出 | 不改 `dllrewrt.c` | 源码核对。另有两点：在 Win8+ 宿主上加这两项会把系统原生实现替换成我们的空实现，属于倒退；原文写"在第 997 行后增加一行"，而第 997 行是数组末项、行尾没有逗号，照做会产生语法错误 |
| 4 | 对 `VmPrefetchInformation`、`VmPagePriorityInformation`、`VmCfgCallTargetInformation` 三类一律返回成功 | 只有预取这一类有真实调用方和反汇编证据。另外两类没有分析过任何调用方就返回成功，违反本项目"不得对未知调用统一伪造成功"的约定 | 只有预取类返回成功；另外两类返回 `STATUS_NOT_SUPPORTED` 并写一条 VxKex 警告日志；未知类返回 `STATUS_INVALID_INFO_CLASS` | 项目约定（`AGENTS.md` 安全与验收边界、`当前工作.md` Bun 复核记录） |
| 5 | 步骤 6 要求离线测试"在已加载新版 `KxNt.dll` 的进程中 `LoadLibraryW` 那个临时 DLL 成功" | 部署前做不到。临时 DLL 的导入表写的是 `ntdll.dll`：普通进程里它绑定到真正的 `ntdll.dll`，照样报 127；启用了 VxKex 的进程里，`kxnt` 按搜索路径解析到**已安装的旧版**，`KexDll.dll` 也是已安装的旧版 | 改为三级验证：导入覆盖脚本对新构建产物做静态核对 → 离线单元测试直接调函数 → 部署后实机验收（见第四节步骤 5） | 加载路径来自 VxKex 日志中的 `DllPath`，首项是 `C:\Program Files\VxKex\Kex64` |
| 6 | 2.3 节反汇编的地址和注释 | 调用点 `0x3f90f7`、跳转桩、IAT 地址正确；但 `cmp r14b,1` 标成了 `0x3f90a2`（实为 `0x3f90b6`），两处跳转目标标成 `0x3f9225`（实为 `0x3f922c`），`MEMORY_RANGE_ENTRY` 两个字段的注释标反了 | 已按实际字节重写 | 实测：本次直接读取 DLL 字节逐条核对 |
| 7 | 2.3 节只写了"返回 0 跳到成功"，没看返回非 0 会怎样 | 这决定了"返回成功"还是"返回不支持"哪个更合适，初版没有给出依据 | 补充失败分支分析：失败不会报错，而是回退为"另行分配内存并把整个文件读进来"。所以预取类返回成功是正确选择，结论不变，现在有依据了 | 实测：见 2.3 节 |
| 8 | 2.3 节写 `ucrtbase.dll`（106 个函数） | 计数有误 | 12 个 `api-ms-win-crt-*` 导入描述符共 125 个函数；全 DLL 共 307 个导入 | 实测：`docx\vxkex-check-imports.ps1` |
| 9 | 没有临时止损办法 | 修复要重新编译、打包、管理员安装，期间 OpenCode 不可用 | 补充回退到 2.2.5 的命令（第五节） | `当前工作.md` 中 2.2.5 的安装记录 |

初版关于 `KxBase` 的说法"`vmem.c` 早已导出 `PrefetchVirtualMemory`"是对的，但要补一句：那个实现返回 `FALSE` 并置 `STATUS_NOT_IMPLEMENTED`（[`vmem.c` 第 193–205 行](../01-Extended%20DLLs/KxBase/vmem.c#L193-L205)），与本次 Nt 层返回成功的语义不一致。本次不改它，原因见第三节末尾。

---

## 一、 问题现象与触发背景

用户执行以下命令将 `@opencode/cli` 从 `2.2.5` 升级至最新版 `2.3.0`：
```powershell
npm install -g --prefix "D:\project\claude-code-engineering\node-v22.22.2-win-x64" --allow-scripts=@opencode/cli @opencode/cli@latest
```

升级完成后，在 PowerShell 中运行 `opencode` 启动失败，控制台报错：
```text
PS C:\Users\liusj> opencode
Starting background server...
Error: Failed to initialize OpenTUI render library: Failed to open library "C:\Users\liusj\AppData\Local\Temp\.bun-1187754466-10d2d8a346f4eb6e.dll": error code 127
    at zL (B:/~BUN/root/chunk-7dsbgff2.js:172:129938)
    at new RX (B:/~BUN/root/chunk-7dsbgff2.js:178:29902)
    at new aI (B:/~BUN/root/chunk-7dsbgff2.js:197:1834)
    at oh (B:/~BUN/root/chunk-7dsbgff2.js:188:14087)
    at <anonymous> (B:/~BUN/root/chunk-96rr5tsv.js:24:6603)
    at Tui.run (B:/~BUN/root/chunk-etnbp8mr.js:99:644)
    at Tui.run (definition) (B:/~BUN/root/chunk-etnbp8mr.js:94:16677)
```

2026-11-14 11:26 用户再次启动，日志 `opencode-00134390547946835413-3048.vxl` 记录了完全相同的错误，问题稳定复现。

---

## 二、 根本原因与证据链

### 2.1 错误码与加载链路
1. Win32 错误码 `127` (`0x7F`) 为 `ERROR_PROC_NOT_FOUND`（找不到指定的程序/导出函数入口点）。
2. `@opencode/cli@2.3.0`（`D:\project\claude-code-engineering\node-v22.22.2-win-x64\node_modules\@opencode\cli\bin\opencode.exe`，257,553,288 字节）在启动 TUI 时，将内嵌的 OpenTUI 原生渲染库解压到 `C:\Users\liusj\AppData\Local\Temp\.bun-1187754466-10d2d8a346f4eb6e.dll`（8,898,440 字节，SHA256 `AD031F3B9FB61CD9D514F7EE8BEFB9F7D93D808FAA8AC28936B083C778CB2B47`）并动态加载。
3. `opencode.exe` 主程序自身没有问题：458 个静态与延迟导入在已安装的 VxKex 下全部可解析（2026-11-14 实测）。出问题的只是这个解压出来的渲染库。

### 2.2 VxKex 运行日志（`STATUS_ENTRYPOINT_NOT_FOUND`）
VxKex 日志 `C:\Users\liusj\AppData\Local\VxKex\Logs\opencode-00134390547879960413-1696.vxl` 末尾：
```text
Rewrote DLL import: KERNEL32.dll -> kxbase
Rewrote DLL import: ntdll.dll -> kxnt
Rewrote DLL import: USER32.dll -> kxuser
Rewrote DLL import: api-ms-win-crt-locale-l1-1-0.dll -> ucrtbase
...
Hard Error handler has been called.
ErrorStatus: STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139)
StringParameter1: "NtSetInformationVirtualMemory"
StringParameter2: "kxnt"
The DLL .bun-1187754466-10d2d8a346f4eb6e.dll was unmapped
Full path to the DLL: "C:\Users\liusj\AppData\Local\Temp\.bun-1187754466-10d2d8a346f4eb6e.dll"
Loaded at 0x000007FEE8AF0000 (size: 10985472 bytes)
Failed to dynamically load C:\Users\liusj\AppData\Local\Temp\.bun-1187754466-10d2d8a346f4eb6e.dll.
NTSTATUS error code: STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139)
```

同一份日志里还有若干 `Failed to resolve X (#0) from Y` 记录（`FlsGetValue2`、`VirtualAlloc2`、`VirtualAlloc2FromApp`、`NtAllocateVirtualMemoryEx`、`NtCreateWaitCompletionPacket` 等）。这些是程序用 `GetProcAddress` 做的可选探测，不是本次启动失败的原因；其中 `NtCreateWaitCompletionPacket` 在 Bun 网络问题排查时已确认 libuv 有 Win7 降级分支。其余几项的来源没有逐一确认，升级前的旧日志已不存在，无法对比 2.2.5 时是否同样出现。

### 2.3 PE 导入表全量扫描与反汇编分析

1. **导入表全量核对**（2026-11-14 用 [`docx\vxkex-check-imports.ps1`](vxkex-check-imports.ps1) 复核，该脚本按 `KexDll\redirects.h` 的重定向规则解析，并跟踪转发导出）：

   | 导入 DLL | 重定向到 | 函数数 | 结果 |
   |---|---|---|---|
   | `bcrypt.dll` | `kxcryp` | 3 | 全部可解析 |
   | 12 个 `api-ms-win-crt-*` | `ucrtbase` | 125 | 全部可解析 |
   | `KERNEL32.dll` | `kxbase` | 107 | 全部可解析（含 `DiscardVirtualMemory`、`GetSystemTimePreciseAsFileTime`） |
   | `ntdll.dll` | `kxnt` | 60 | **缺 1 个：`NtSetInformationVirtualMemory`** |
   | `USER32.dll` | `kxuser` | 12 | 全部可解析 |
   | 合计 |  | 307 | 静态缺失 1 项，无延迟导入 |

   加载器遇到第一个缺失就停，所以日志只能证明"至少缺这一个"；全量扫描才能证明"只缺这一个"。补齐后静态加载这一关可以过。

2. **调用点反汇编**（全 `.text` 段只有 `0x3f90f7` 一处经跳转桩 `0x673300` → IAT `0x809608` 调用该函数；跳转桩 `0x673210` → IAT `0x809590` 为 `NtMapViewOfSection`）：
   ```asm
   3f90a2  e8 69 a1 27 00            call  0x673210               ; NtMapViewOfSection，r8 = &[rbp+0x18]（BaseAddress 输出）
   3f90a7  3d 22 00 00 c0            cmp   eax, 0xC0000022        ; STATUS_ACCESS_DENIED
   3f90ac  0f 84 fa 00 00 00         je    0x3f91ac               ; 单独处理拒绝访问
   3f90b2  85 c0                     test  eax, eax
   3f90b4  75 4e                     jne   0x3f9104               ; 映射失败 → 回退路径
   3f90b6  41 80 fe 01               cmp   r14b, 1                ; 调用方是否要求预取
   3f90ba  0f 85 6c 01 00 00         jne   0x3f922c               ; 不要求 → 直接成功返回
   3f90c0  48 8b 45 18               mov   rax, [rbp+0x18]
   3f90c4  48 89 45 f8               mov   [rbp-0x8], rax         ; Entry.VirtualAddress = BaseAddress
   3f90c8  48 89 7d 00               mov   [rbp+0x0], rdi         ; Entry.NumberOfBytes  = 映射长度
   3f90cc  c7 45 14 00 00 00 00      mov   dword [rbp+0x14], 0    ; Flags = 0
   3f90d3  48 8d 45 14               lea   rax, [rbp+0x14]
   3f90d7  48 89 44 24 20            mov   [rsp+0x20], rax        ; arg5: VmInformation = &Flags
   3f90dc  c7 44 24 28 04 00 00 00   mov   dword [rsp+0x28], 4    ; arg6: VmInformationLength = 4
   3f90e4  4c 8d 4d f8               lea   r9, [rbp-0x8]          ; arg4: VirtualAddresses = &Entry
   3f90e8  41 b8 01 00 00 00         mov   r8d, 1                 ; arg3: NumberOfEntries = 1
   3f90ee  48 c7 c1 ff ff ff ff      mov   rcx, -1                ; arg1: 当前进程
   3f90f5  31 d2                     xor   edx, edx               ; arg2: VmPrefetchInformation (0)
   3f90f7  e8 04 a2 27 00            call  0x673300               ; NtSetInformationVirtualMemory
   3f90fc  85 c0                     test  eax, eax
   3f90fe  0f 84 28 01 00 00         je    0x3f922c               ; 返回 0 → 使用映射视图，成功返回
   3f9104  48 8b 55 30               mov   rdx, [rbp+0x30]        ; ↓ 回退路径（映射失败与预取失败共用）
   3f9108  4c 8b aa f0 02 00 00      mov   r13, [rdx+0x2f0]
   3f910f  4c 8b a2 f8 02 00 00      mov   r12, [rdx+0x2f8]
   ...
   3f911d  48 89 fa                  mov   rdx, rdi               ; 长度
   3f9120  41 b0 0c                  mov   r8b, 0x0c              ; 对齐 2^12 = 4096
   3f9127  41 ff 14 24               call  [r12]                  ; 经分配器分配同样大小的内存
   ...
   3f9184  e8 67 f8 ff ff            call  0x3f89f0               ; 循环读取文件内容填入该内存
   ```
   结论：
   - 参数与初版分析一致：信息类为 `VmPrefetchInformation (0)`，1 个范围，`Flags = 0`，长度 4。
   - **返回非 0 不会报错**，而是与"映射失败"走同一条回退路径：另行分配一块与文件等大的内存，把文件内容整个读进去。从指令形态判断这是"内存映射不可用时"的兜底实现；在所查看的指令范围内没有看到释放已建立映射视图的调用。
   - 因此对预取类返回 `STATUS_SUCCESS` 是正确做法：程序直接使用映射视图，页面在首次访问时按需调入，功能与真正预取完全相同，只是少了一次提前读盘。若返回"不支持"，程序也能跑，但每次都要多占一份文件大小的内存并整文件读盘。
   - `NtSetInformationVirtualMemory` 是 Windows 8 新增的系统调用，Win32 层的 `PrefetchVirtualMemory` 建立在它之上。调用形态与 Zig 标准库的文件内存映射实现吻合（属于推断，未对照源码）。

---

## 三、 修复方案设计（修订后）

在 `KexDll.dll` 实现 `NtSetInformationVirtualMemory`，由 `KxNt.dll` 以 `Nt` / `Zw` 两个名字转发导出。

1. **数据结构与枚举**：全部已存在于 [`NtDll.h`](../00-Common%20Headers/NtDll.h)，不新增。
   - `MEMORY_RANGE_ENTRY`：第 1196 行；
   - `VIRTUAL_MEMORY_INFORMATION_CLASS`：第 2979 行（`VmPrefetchInformation = 0`、`VmPagePriorityInformation = 1`、`VmCfgCallTargetInformation = 2`、`VmMaxInformationClass = 3`）。
2. **函数原型的归属**：删除 `NtDll.h` 第 3960–3970 行那段 `NTSYSCALLAPI` 原型，改在 `KexDll.h` 用 `KEXAPI` 声明。这样函数名保持原名，与 `NtCompareObjects`、`NtQueryWnfStateData`、`NtWaitForAlertByThreadId` 这些"Win7 不存在、由 KexDll 提供"的函数是同一约定。
3. **返回语义**：

   | 信息类 | 返回 | 理由 |
   |---|---|---|
   | `VmPrefetchInformation (0)` | 参数基本校验通过后返回 `STATUS_SUCCESS` | 纯性能提示，不做也不改变程序行为；有真实调用方和失败分支分析支持 |
   | `VmPagePriorityInformation (1)`、`VmCfgCallTargetInformation (2)` | `STATUS_NOT_SUPPORTED`，并写 VxKex 警告日志 | 没有分析过任何调用方，不伪造成功；写日志是为了将来有程序用到时能在 `.vxl` 里直接看到 |
   | 其他 | `STATUS_INVALID_INFO_CLASS` | 未知信息类 |

   参数校验是保守写法（条目数非 0、指针非空、长度不小于 4），没有逐一对照真实内核在各种非法参数下的具体错误码。`ProcessHandle` 不使用：预取对任何进程句柄都是空操作。
4. **导出**：`KexDll.def` 导出原名；`kxnt.def` 增加 `Nt` 与 `Zw` 两条转发。`KxNt\forwards.c` 是按 Win7 `ntdll.dll` 生成的转发清单，里面没有这个函数，不会重复导出。
5. **不修改 `KexDll\dllrewrt.c`**（原因见第零节第 3 条）。

**备选写法**（若不想动 `NtDll.h`）：函数命名为 `Ext_NtSetInformationVirtualMemory`，`KexDll.def` 导出该名，`kxnt.def` 写 `NtSetInformationVirtualMemory = KexDll.Ext_NtSetInformationVirtualMemory`（`Zw` 同理）。本次同样试编译通过。不作为首选，是因为 `Ext_` 前缀在本仓库表示"对已有系统函数的扩展包装"，与这个函数的性质不符。

**关于 `KxBase` 的 `PrefetchVirtualMemory`**：它现在返回失败。两者语义不一致，但本次不动：没有证据表明有程序因为它失败而出问题，改它不在本故障范围内。以后若要统一，应让它也返回成功。

**关于 Win8 及以上宿主**：那些系统的 `ntdll.dll` 自带这个函数，本实现会把应用 DLL 的调用换成空操作。预取类无影响；另外两类会从"系统原生可用"变成"不支持"。本机即目标环境（Win7），此项不处理，只在此记录。

---

## 四、 具体实施步骤（修订后）

以下代码已于 2026-11-14 在仓库外用 VC10 按 `KexDll` 工程的参数（`/W3 /WX /Gz /TC`）试编译通过，x64 与 x86 均无警告。**只验证了编译，没有链接、没有生成 DLL、没有运行。**

### 步骤 1：修改 [`00-Common Headers/NtDll.h`](../00-Common%20Headers/NtDll.h) 与 [`00-Common Headers/KexDll.h`](../00-Common%20Headers/KexDll.h)

1a. 在 `NtDll.h` 删除第 3960–3970 行整段（该 region 里只有这一个函数）：
```c
#pragma region Nt* function declarations (not in Windows 7)

NTSYSCALLAPI NTSTATUS NTAPI NtSetInformationVirtualMemory(
	IN		HANDLE								ProcessHandle,
	IN		VIRTUAL_MEMORY_INFORMATION_CLASS	VmInformationClass,
	IN		ULONG_PTR							NumberOfEntries,
	IN		PMEMORY_RANGE_ENTRY					VirtualAddresses,
	IN		PVOID								VmInformation,
	IN		ULONG								VmInformationLength);

#pragma endregion
```

1b. 在 `KexDll.h` 的 `NtCompareObjects` 声明（第 322–324 行）之后增加。**不要**再定义 `MEMORY_RANGE_ENTRY`：
```c
KEXAPI NTSTATUS NTAPI NtSetInformationVirtualMemory(
	IN	HANDLE								ProcessHandle,
	IN	VIRTUAL_MEMORY_INFORMATION_CLASS	VmInformationClass,
	IN	ULONG_PTR							NumberOfEntries,
	IN	PMEMORY_RANGE_ENTRY					VirtualAddresses,
	IN	PVOID								VmInformation,
	IN	ULONG								VmInformationLength);
```

### 步骤 2：修改 [`KexDll/ntvmem.c`](../KexDll/ntvmem.c)
在文件末尾追加（该文件已在 `KexDll.vcxproj` 中，无需改工程文件）：
```c
//
// NtSetInformationVirtualMemory was introduced in Windows 8 and has no
// counterpart in the Windows 7 kernel, so there is nothing to forward to.
//
// VmPrefetchInformation is purely a performance hint (PrefetchVirtualMemory is
// built on it). Not prefetching does not change program behaviour: the pages
// are simply faulted in on first access. So for this class we validate the
// parameters and report success.
//
// The other information classes are not reported as successful, because no
// real caller has been analysed for them. Failing visibly (and logging) is
// preferable to pretending that something was done.
//
KEXAPI NTSTATUS NTAPI NtSetInformationVirtualMemory(
	IN	HANDLE								ProcessHandle,
	IN	VIRTUAL_MEMORY_INFORMATION_CLASS	VmInformationClass,
	IN	ULONG_PTR							NumberOfEntries,
	IN	PMEMORY_RANGE_ENTRY					VirtualAddresses,
	IN	PVOID								VmInformation,
	IN	ULONG								VmInformationLength)
{
	UNREFERENCED_PARAMETER(ProcessHandle);

	switch (VmInformationClass) {
	case VmPrefetchInformation:
		if (NumberOfEntries == 0 ||
			VirtualAddresses == NULL ||
			VmInformation == NULL ||
			VmInformationLength < sizeof(ULONG)) {

			return STATUS_INVALID_PARAMETER;
		}

		return STATUS_SUCCESS;

	case VmPagePriorityInformation:
	case VmCfgCallTargetInformation:
		KexLogWarningEvent(
			L"NtSetInformationVirtualMemory: unsupported information class %d",
			VmInformationClass);

		return STATUS_NOT_SUPPORTED;

	default:
		return STATUS_INVALID_INFO_CLASS;
	}
}
```

### 步骤 3：修改 [`KexDll/KexDll.def`](../KexDll/KexDll.def)
在文件末尾 `NtAlertThreadByThreadId`（第 132 行）之后增加一行：
```def
	NtSetInformationVirtualMemory
```

### 步骤 4：修改 [`01-Extended DLLs/KxNt/kxnt.def`](../01-Extended%20DLLs/KxNt/kxnt.def)
在 `NtAlertThreadByThreadId`（第 52 行）之后增加：
```def
	NtSetInformationVirtualMemory					= KexDll.NtSetInformationVirtualMemory
```
在 `ZwAlertThreadByThreadId`（第 70 行，文件末行）之后增加：
```def
	ZwSetInformationVirtualMemory					= KexDll.NtSetInformationVirtualMemory
```

### 步骤 5：编译、验证与打包

1. **构建**：运行 `D:\project\VxKex-NEXT\build-installer.cmd`，全量编译 x64 与 Win32 Release 并打包 `D:\project\VxKex-NEXT\KexSetup_Release.exe`。记录真实退出码。
2. **静态核对（部署前）**：用导入覆盖脚本对着新构建产物检查那个临时 DLL。它同时验证 `kxnt` 的导出和 `kxnt → KexDll` 的转发链：
   ```powershell
   pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-check-imports.ps1 `
       -Target "$env:LOCALAPPDATA\Temp\.bun-1187754466-10d2d8a346f4eb6e.dll" `
       -KexDir D:\project\VxKex-NEXT\x64\Release
   ```
   期望：`RESULT: static missing = 0`，退出码 0。修复前的基线已实测：同一命令对当前 `x64\Release` 报缺 1 项、退出码 1。
3. **离线单元测试（部署前）**：在 [`test\ansi-features-test.c`](../test/ansi-features-test.c) 按现有 Test 7 的方式新增一组，从 `D:\project\VxKex-NEXT\x64\Release\KxNt.dll` 取函数地址后直接调用：

   | 用例 | 期望 |
   |---|---|
   | `GetProcAddress` 取 `NtSetInformationVirtualMemory`、`ZwSetInformationVirtualMemory` | 均非空且地址相同 |
   | 信息类 0，1 个有效范围，`Flags = 0`，长度 4（即真实调用方的参数） | `STATUS_SUCCESS (0)` |
   | 信息类 0，`NumberOfEntries = 0` | `STATUS_INVALID_PARAMETER (0xC000000D)` |
   | 信息类 1 或 2 | `STATUS_NOT_SUPPORTED (0xC00000BB)` |
   | 信息类 99 | `STATUS_INVALID_INFO_CLASS (0xC0000003)` |

   **必须从未被 VxKex 注入的父进程启动测试**（实测，见第六节）：从 VS Code 终端直接运行时，测试进程启动即被注入已安装的 `C:\Windows\System32\KexDll.dll`，`KxNt` 的转发解析到旧版，新函数一律报找不到（错误 127）。用 WMI 启动即可避开：
   ```powershell
   $r = 'D:\project\VxKex-NEXT'; $log = "$r\test\ansi-features-test.log"
   $cl = "cmd.exe /c chcp 437 >nul & `"$r\test\x64\Release\ansi-features-test.exe`" > `"$log`" 2>&1 & call echo RUN_EXIT=%^ERRORLEVEL%>> `"$log`""
   Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cl; CurrentDirectory = "$r\test" }
   ```
   等该进程退出后读日志；Test 9 会打印 `KexDll in use:`，必须是 `x64\Release` 下那份。
4. **部署**：由用户以管理员身份运行新的 `D:\project\VxKex-NEXT\KexSetup_Release.exe`。
5. **实机验收（部署后）**：
   - 新开终端运行 `opencode`（`2.3.0`），TUI 正常进入；
   - `pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-read-log.ps1 -Name opencode` 的 [1]、[2] 两栏为空；
   - 回归：中文输入"继续"不夹 `s`、斜杠菜单居中、问答菜单高亮、`opencode auth login` 框线显示、一次真实模型请求。

---

## 五、 临时止损与风险

- **临时回退**（未在本轮执行）：修复部署前如需使用 OpenCode，可退回 2.2.5：
  ```powershell
  npm install -g --prefix "D:\project\claude-code-engineering\node-v22.22.2-win-x64" --allow-scripts=@opencode/cli @opencode/cli@2.2.5
  ```
- **补齐后可能暴露的后续问题**：静态导入只缺这一项已确认，但 2.3.0 的新渲染库加载成功之后的运行期行为没有验证过。若启动后出现新的错误，按 [`VxKex下插件与CLI升级后API缺失排查手册-20261114.md`](VxKex下插件与CLI升级后API缺失排查手册-20261114.md) 的流程继续排查，不要假设是同一个问题。
- **回滚**：新安装包有问题时，重新运行上一版 `KexSetup_Release_1.2.3.2463_20261113_2210.exe`（SHA256 `ED5C3F15493D181DC473554C8370968FCE6E824090886330EC5A2F5CD4DEEA9A`）。

---

## 六、 实施记录（2026-11-14）

### 6.1 源码修改
按第四节步骤 1–4 逐字实施，共 5 个文件，另在测试中新增 Test 9：

| 文件 | 改动 |
| --- | --- |
| `00-Common Headers\NtDll.h` | 删除旧 `NTSYSCALLAPI` 原型所在 region（12 行） |
| `00-Common Headers\KexDll.h` | `NtCompareObjects` 后增加 `KEXAPI` 声明 |
| `KexDll\ntvmem.c` | 文件末尾增加实现 |
| `KexDll\KexDll.def` | 增加导出 `NtSetInformationVirtualMemory` |
| `01-Extended DLLs\KxNt\kxnt.def` | 增加 `Nt` / `Zw` 两条转发 |
| `test\ansi-features-test.c` | 新增 `RunTest9`（16 项断言） |

5 个源码文件均保持 CRLF 行尾；全仓库再无其他引用该符号的位置。`KexDll\dllrewrt.c` 未改。

### 6.2 构建
`build-installer.cmd` 全流程完成（x64、Win32、打包、ConPTY 回归），日志中编译/链接警告与错误为 0。

导出核对（`dumpbin /EXPORTS`）：

| 文件 | 结果 |
| --- | --- |
| `x64\Release\KexDll.dll` | `NtSetInformationVirtualMemory` |
| `x64\Release\KxNt.dll` | `NtSetInformationVirtualMemory`、`ZwSetInformationVirtualMemory`，均 forwarded to `KexDll.NtSetInformationVirtualMemory` |
| `Release\KexDll.dll` | `NtSetInformationVirtualMemory = _NtSetInformationVirtualMemory@24`（导出名无修饰） |
| `Release\KxNt.dll` | 同 x64，两条转发 |

产物 SHA256：

| 文件 | 大小 | SHA256 |
| --- | --- | --- |
| `KexSetup_Release.exe`（同 `KexSetup_Release_1.2.3.2463_20261114_1313.exe`） | 5,036,769 | `E89EC8B81D1853BC405DA517C429349DD0E0B1F3405BF7E490D767E925832E43` |
| `x64\Release\KexDll.dll` | 262,144 | `792EC68C45CD4C61A097640D882DBC22C03C818DF48D455A6DD1425DFCAFAD00` |
| `x64\Release\KxNt.dll` | 122,368 | `3824E9F2AC1AC2310182F0487A8986D689E711621F738F9D36F06B1FEA732343` |
| `Release\KexDll.dll` | 254,464 | `CA34E9D67A4A114FADD30947ECD2C5C7F5D26A7C0C6E8D964A60412F30C0411F` |
| `Release\KxNt.dll` | 124,416 | `82D4F342F7D7C87E9C59CDFC86163CD8C28BDCF921B1B78C39C7A4F1C0EEEF5C` |

### 6.3 测试

1. **静态核对**：`vxkex-check-imports.ps1 -KexDir x64\Release` 对 `.bun-1187754466-10d2d8a346f4eb6e.dll` 输出 `[OK] ntdll.dll -> kxnt : 60`、`static missing = 0`，退出码 0（修复前同一命令缺 1 项、退出码 1）。
2. **ConPTY 回归**：`RESULT failures=0`、`TEST_EXIT=0`。
3. **离线单元测试**：
   - 第一次从 VS Code 终端启动，Test 9 有 4 项失败，`KexDll in use: C:\Windows\SYSTEM32\KexDll.dll`。原因是测试进程被 VxKex 注入了已安装的旧 `KexDll`，`KxNt` 转发解析到旧版，函数找不到（错误 127），属于测试环境问题，不是代码缺陷；
   - 改用 WMI 启动（见第四节步骤 5.3），控制台代码页 437：**89 项全部 PASS，`failures = 0`，退出码 0**。Test 9 确认进程中使用的是 `x64\Release\KexDll.dll`；真实调用方参数返回 `0x00000000`；4 种非法参数均返回 `0xC000000D`；信息类 1、2 返回 `0xC00000BB`；信息类 99、-1 返回 `0xC0000003`；调用后内存仍可正常读写。Test 1–8 无回归。
   - 只测了 x64；Win32 版只做了构建和导出核对。

### 6.4 部署与验收（2026-11-14）
- **部署核对**：已安装文件与构建产物 SHA256 一致：`C:\Program Files\VxKex\Kex64\KxNt.dll`、`C:\Windows\System32\KexDll.dll`、`C:\Program Files\VxKex\Kex32\KxNt.dll`、`C:\Windows\SysWOW64\KexDll.dll`。
- **加载**：部署后最新日志 `opencode-00134391099886063290-4164.vxl`（14:07:24）经 `vxkex-read-log.ps1` 读取，[1] 硬错误、[2] 动态加载失败均为空。[3] 栏为可选探测（新增 `IsMouseInPointerEnabled`），程序正常运行，判断为无害。
- **用户实测**：`opencode` 2.3.0 正常启动；中文输入、斜杠菜单、问答高亮、`auth login` 框线、一次真实模型请求均通过。
- **未覆盖**：Win32 版未运行离线测试（只做了构建与导出核对）；Win8+ 宿主上的影响见第三节，未测试。
