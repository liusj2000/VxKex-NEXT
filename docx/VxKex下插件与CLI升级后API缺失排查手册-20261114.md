# VxKex 下插件与 CLI 升级后 API 缺失排查手册

**记录日期**：2026-11-14  
**适用环境**：Windows 7 SP1 x64 + VxKex NEXT，本机 `D:\project\VxKex-NEXT`  
**适用对象**：OpenCode、Claude Code、Antigravity（`agy.exe`）、Codex、Cline 等通过 npm 或 VS Code 扩展分发、自带原生程序的工具  
**用途**：这些工具升级后启动失败时，按本手册判断是不是"新版本用到了 VxKex 还没实现的系统函数"，并定位到具体函数。

---

## 一、 先看结论：五分钟定位法

升级后程序起不来，按顺序做这三步，多数情况到第 2 步就有答案。

```powershell
# 1. 读 VxKex 日志，看加载器报了什么（把 opencode 换成出问题的 exe 名，不带 .exe）
pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-read-log.ps1 -Name opencode

# 2. 对日志 [2] 栏里那个加载失败的 DLL（或主程序 exe）做导入全量核对
pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-check-imports.ps1 -Target "<上一步报出的文件完整路径>"

# 3. 修复并构建后，对着新构建产物再核对一遍，确认缺失项清零
pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-check-imports.ps1 -Target "<同一个文件>" -KexDir D:\project\VxKex-NEXT\x64\Release
```

两个脚本都只读不写，不运行被检查的程序，Windows PowerShell 5.1 和 PowerShell 7 均可用。

第 2 步很重要：**加载器遇到第一个缺失的函数就停**，日志里只会有一个函数名。只补这一个，重新编译、打包、管理员安装之后，可能马上又冒出第二个。导入核对脚本一次列出全部缺失项，避免来回折腾。

---

## 二、 症状对照：先判断是哪一类问题

| 症状 | 最可能的类型 | 跳到 |
| --- | --- | --- |
| `error code 127`、`0xc0000139`、`STATUS_ENTRYPOINT_NOT_FOUND`、"无法定位程序输入点 … 于动态链接库 …"、`The specified procedure could not be found` | A 类：静态导入的函数缺失 | 第四节 |
| 程序能启动，随后自己打印 `XXX not found`、`fatal error`、`unsupported platform` 之类后退出 | B 类：程序运行时查找函数失败 | 第五节 |
| 程序能启动，某个功能报 `ENOTSUP`、`EINVAL`、`operation not supported`、错误码 `10045` / `50` / `87` | C 类：函数存在，但参数或标志 Win7 不认 | 第六节 |
| 报"找不到 `xxx.dll`"、`error code 126`、`0xc0000135` | D 类：整个 DLL 缺失或没有被重定向 | 第七节 |
| 弹框 "VxKex NEXT could not start because the DLL imports of the main process image could not be rewritten" | VxKex 自身的导入重写失败，不是 API 缺失 | `AGENTS.md`"已知阻断"一节 |
| 入口文件只有几百字节、不是 exe；反复下载；启动超时；界面显示错乱 | E 类：与 API 无关 | 第八节 |

数字对照：Win32 错误 `127` = `ERROR_PROC_NOT_FOUND` = NTSTATUS `0xc0000139`（函数找不到）；Win32 错误 `126` = `ERROR_MOD_NOT_FOUND` = NTSTATUS `0xc0000135`（DLL 找不到）。

---

## 三、 背景：VxKex 如何让新程序在 Win7 上找到函数

理解这一段，后面的排查步骤就都能推出来。

1. **整体改名**。程序的导入表里写着"我要从 `KERNEL32.dll` 拿这些函数、从 `ntdll.dll` 拿那些函数"。VxKex 在加载时把这些 DLL 名字改掉，例如 `ntdll.dll → kxnt`、`KERNEL32.dll → kxbase`、`api-ms-win-crt-*.dll → ucrtbase`。对照表在 [`KexDll\redirects.h`](../KexDll/redirects.h)。日志里的 `Rewrote DLL import: X -> Y` 就是这一步。
2. **改名之后，那个 Kx DLL 必须导出程序要的全部函数**。Kx DLL 的导出分三种：自己实现的新函数；对系统函数的包装；原样转发给系统 DLL 的（`forwards.c`，按 Win7 系统 DLL 的导出表生成）。只要程序要的某个函数三种里都没有，加载就失败，这就是 A 类问题。
3. **只改应用自己的模块**。位于 Windows 目录和 VxKex 安装目录里的 DLL 不重写，个别例外写在 `KexDll\dllrewrt.c` 的 `KexIsRewriteForcedWindowsDll` 里（目前是 `webio`、`wininet`、`sspicli`、`GdiPlus`、`wsock32`）。系统 DLL 内部的调用绕过了 VxKex，这是 D 类问题的来源之一。
4. **搜索顺序**。VxKex 把自己的目录放在最前面。日志里的 `DllPath` 能看到实际顺序：`C:\Program Files\VxKex\Kex64` → 程序所在目录 → `C:\Windows\system32` → ……。`KexDll.dll` 本身装在 `C:\Windows\System32`。
5. **日志**。每个被 VxKex 接管的进程在 `%LOCALAPPDATA%\VxKex\Logs` 留一个 `<exe名>-<时间戳>-<进程号>.vxl`。**没有日志，说明 VxKex 根本没作用到这个进程**（没对该程序启用，或它是某个未启用程序的子进程），那就不是本手册的问题，先去确认 VxKex 是否启用。

---

## 四、 A 类：静态导入的函数缺失（最常见）

**本仓库案例**：Cline 3.0.70 缺 `ClosePseudoConsole`（`kxbase`）；OpenCode 2.3.0 的渲染库缺 `NtSetInformationVirtualMemory`（`kxnt`）。

### 步骤 1：找到真正加载失败的那个文件

报错里的程序名不一定是出问题的文件。常见的几种藏法：

| 分发方式 | 真正的原生程序在哪 |
| --- | --- |
| npm 全局包 | `<prefix>\node_modules\<包名>\bin\*.exe`，或平台子包如 `cli-windows-x64\bin\`；`<prefix>\xxx.cmd` 只是转发壳 |
| Bun 打包的单文件 exe（OpenCode、Claude Code 原生版） | 主程序之外，原生库在首次用到时解压到 `%TEMP%\.bun-*.dll` 或 `*.node`，文件名在报错信息里 |
| VS Code 扩展 | `D:\VSCode-win32-x64-1.107.1\data\extensions\<扩展名-版本>\bin\` 及其子目录 |
| 扩展另行下载的后端 | 例如 Antigravity 的 `C:\Users\liusj\.gemini\bin\agy.exe` |

最省事的办法是直接读日志，[2] 栏给出加载失败的文件完整路径：

```powershell
pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-read-log.ps1 -Name opencode
```

输出解读（以 OpenCode 2.3.0 为例，2026-11-14 实测）：

```text
[1] Hard errors (fatal for the module being loaded). ...
      2 x  STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139)   parameter1="NtSetInformationVirtualMemory"   parameter2="kxnt"
[2] DLLs that failed to load dynamically
      2 x  C:\Users\liusj\AppData\Local\Temp\.bun-1187754466-10d2d8a346f4eb6e.dll
[3] Failed GetProcAddress lookups (often optional probes - suspects only)
      2 x  FlsGetValue2   from kxbase.DLL
      ...
```

- [1] 是确凿的故障：`parameter1` 是缺的函数，`parameter2` 是本该导出它的 DLL。
- [2] 是因此没加载起来的文件，下一步要查它。
- [3] 只是线索，见第五节，不要直接当成故障。

常用参数：`-Last 3` 看最近三份日志；`-Path <文件>` 指定某一份；`-ShowRewrites` 列出 DLL 改名记录；`-Dump` 输出全部文本。

如果主程序自己就起不来（没有 [2]，但有 [1]），要查的文件就是主程序 exe。

### 步骤 2：全量核对导入表

```powershell
pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-check-imports.ps1 `
    -Target "C:\Users\liusj\AppData\Local\Temp\.bun-1187754466-10d2d8a346f4eb6e.dll" -ShowOk
```

实测输出节选：

```text
[OK] KERNEL32.dll -> kxbase (C:\Program Files\VxKex\Kex64\kxbase.dll) : 107
[MISSING] ntdll.dll -> kxnt : 1 of 60
    NtSetInformationVirtualMemory   [not exported by C:\Program Files\VxKex\Kex64\kxnt.dll]
RESULT: static missing = 1, delay-load missing = 0, unverified forwarders = 0
```

各种标记的含义：

| 标记 | 含义 | 严重程度 |
| --- | --- | --- |
| `[MISSING]` + `not exported by` | 目标 DLL 没有这个导出 | 必然导致加载失败 |
| `[MISSING]` + `DLL file not found` | 改名后的 DLL 文件找不到 | 必然导致加载失败，属 D 类 |
| `[MISSING]` + `broken forwarder` | 导出存在，但它转发到的下一级没有这个函数 | 必然导致加载失败 |
| `[MISSING-DELAY]` | 延迟加载的导入缺失 | 只有真正调用到时才出错 |
| `[WARN]` unverified forwarders | 转发目标 DLL 文件找不到，无法确认 | 需要人工看一眼 |

退出码：0 表示静态导入全部可解析，1 表示有缺失，2 表示参数有误。

### 步骤 3：弄清每个缺失函数是什么、调用方怎么用

对每个缺失的函数回答三个问题，答案决定怎么实现：

1. **它是什么**：哪个 Windows 版本引入，做什么，参数和返回值的含义。以微软文档为准；`Nt*` 这类未公开函数参考仓库 `NtDll.h` 里已有的声明和公开的逆向资料。
2. **调用方传了什么参数**：反汇编调用点，或查调用方的开源代码。
3. **调用方拿到失败会怎样**：成功分支和失败分支都要看。这一步最容易漏。OpenCode 2.3.0 这次，初版分析只看了成功分支；补看失败分支才知道失败时程序会改为把整个文件读进内存，从而确认"返回成功"是对的，而不是凭感觉。

### 步骤 4：选择实现策略

| 情况 | 做法 | 本仓库的例子 |
| --- | --- | --- |
| Win7 有等价能力 | 用 Win7 的接口真实实现 | `QueryInterruptTimePrecise` 用 `SharedUserData` 加性能计数器换算 |
| 函数只是性能提示，不做也不影响结果 | 校验参数后返回成功 | `NtSetInformationVirtualMemory` 的预取类 |
| Win7 做不到，但调用方失败后有回退路径 | 如实返回"不支持"，让调用方走回退 | `NtQueryWnfStateData` 返回 `STATUS_NOT_IMPLEMENTED` |
| Win7 做不到，调用方也没有回退 | 需要单独设计，可能做不了 | ConPTY：没有后端时不能返回成功或假句柄 |

**底线**：不对没分析过的调用统一返回成功。一个函数有多种用法（信息类、标志位、控制码）时，只对有证据的那一种放行，其余如实报不支持并写日志（`KexLogWarningEvent`），这样下次有程序用到，日志里直接能看到。

### 步骤 5：落到代码，同步哪些文件

函数放进哪个 DLL，看日志里的 `parameter2`，或者查 `redirects.h` 里导入 DLL 名对应的目标。

| 目标 | 需要同步的文件 |
| --- | --- |
| `kxnt`（即 `ntdll` 的函数） | 实现放 `KexDll\*.c`；`KexDll\KexDll.def` 导出；`00-Common Headers\KexDll.h` 声明；`01-Extended DLLs\KxNt\kxnt.def` 加 `Nt` 和 `Zw` 两条转发 |
| `kxbase`（`kernel32` / `kernelbase` 及大量 `api-ms-win-core-*`） | `01-Extended DLLs\KxBase\*.c`；`kxbase.def`；`00-Common Headers\KxBase.h`；若接管的是系统已有函数，还要处理 `forwards.c` 里的同名转发 |
| `kxnet`（`ws2_32`、`winhttp`、`dnsapi`） | `01-Extended DLLs\KxNet\winsock.c` 等；`kxnet.def`（`ws2_32` 的函数要按原序号导出）；`kxnetp.h`；`forwards.c` 去掉已接管的转发 |
| 其他（`kxuser`、`kxcryp`、`kxcom`、`kxadvapi`、`kxmi` 等） | 同样是"实现 + `.def` + 头文件 + 检查 `forwards.c`"，动手前先看该目录下已有函数怎么写 |

新增 `.c` 文件必须同时加进对应的 `.vcxproj` 和 `.vcxproj.filters`。

**本仓库踩过的坑**（实施前逐条过一遍）：

1. **头文件里可能已经有同名声明**。`NtDll.h` 里有一批声明为 `NTSYSCALLAPI`（即从 `ntdll` 导入）的原型，包括一些 Win7 并不存在的函数。在 `KexDll` 里再定义同名函数会报 C4273，而 `KexDll` 工程把警告当错误。动手前先全仓库搜一遍函数名和相关结构体名。
2. **结构体、枚举可能已经定义过**。重复定义报 C2011。同样先搜。
3. **接管系统已有的函数时，`forwards.c` 里有同名转发**。不去掉会出现重复导出（LNK4197）。
4. **`ws2_32` 这类按序号导入的 DLL**，`.def` 里必须写对序号。
5. **`.def` 和数组的末尾**。往数组末项后面加元素时，记得给原来的末项补逗号。
6. **`KexDll\dllrewrt.c` 的 `NtToKexMap` 一般不用动**。它只服务于 Win8 及以上宿主的一条特殊路径，与"应用 DLL 的 `ntdll` 导入改名为 `kxnt`"无关。
7. **x64 和 Win32 都要能编**。`build-installer.cmd` 两个平台都构建，只在 x64 下试过不算完。
8. **方案文档里的代码要先试编译**。在仓库外建一个临时 `.c`，包含 `buildcfg.h` 和该模块的私有头，用该工程的参数（`KexDll` 是 `/W3 /WX /Gz /TC`）跑一次 `cl /c`，几十秒就能发现第 1、2 条这类问题。OpenCode 2.3.0 的初版方案就是没做这一步，三处编译错误留到了文档里。

### 步骤 6：构建与分级验证

构建命令见 `AGENTS.md`"编译命令"一节（必须带 `/p:TrackFileAccess=false`，建议 `/m:1`），或直接运行 `D:\project\VxKex-NEXT\build-installer.cmd`。

验证分四级，前两级不需要管理员权限，也不改动系统：

1. **静态核对**：第一节的第 3 条命令，对着 `x64\Release` 再跑一次导入核对，缺失项应清零。这一步同时覆盖了转发链（例如 `kxnt → KexDll`）。
2. **离线单元测试**：在 `test\ansi-features-test.c` 里按已有用例的写法，从 `x64\Release` 加载新 DLL，直接调用新函数，检查返回值。
   **注意**：从 VS Code 终端（或其他已被 VxKex 接管的进程）直接启动测试时，测试进程一开始就被注入已安装的 `C:\Windows\System32\KexDll.dll`。只改 `KxBase` 等 DLL 时影响不大，但新函数若在 `KexDll` 里（`kxnt` 的函数都是），转发会解析到旧版，表现为错误 127，看起来像修复没生效。办法是用 WMI 从一个干净的父进程启动测试，并在测试里打印实际使用的 `KexDll` 路径加以确认。命令见 [`OpenCode-2.3.0升级后error-code-127原因分析与修复方案-20261113.md`](OpenCode-2.3.0升级后error-code-127原因分析与修复方案-20261113.md) 第四节步骤 5.3。
3. **部署**：由用户以管理员身份运行新的 `KexSetup_Release.exe`。部署前记下上一版安装包的文件名和 SHA256，以便回滚。
4. **实机验收**：启动真实程序；再读一次日志，[1]、[2] 两栏应为空；跑一遍该程序已有的回归项。

**部署前做不到的事**：让真实程序加载新 DLL。启用了 VxKex 的进程总是先找到已安装的旧版 Kx DLL，`KexDll.dll` 也是 `System32` 里那份；没启用 VxKex 的进程则根本不做改名。所以"加载成功"只能在第 4 级验证，方案里不要把它写成离线测试项。

---

## 五、 B 类：程序运行时查找函数失败

**本仓库案例**：Antigravity 后端 `agy.exe` 1.4.12（Go 1.24+）启动时用 `GetProcAddress` 找 `QueryInterruptTimePrecise`，找不到就主动崩溃退出。

特点：导入核对脚本查不出来，因为函数名不在导入表里，而是程序里的一个字符串。线索来自两处：

1. **程序自己的报错**，通常直接带函数名（`fatal error: QueryInterruptTimePrecise not found`）。
2. **日志 [3] 栏**的 `Failed to resolve X from Y`。

难点在于 [3] 栏里大多数是无害的。程序常常先探测新函数，找不到就用旧办法，例如 libuv 探测 `NtCreateWaitCompletionPacket` 失败后会降级。区分办法：

- **与正常版本的日志对比**。升级前的版本如果也有同一条，而且那时工作正常，这一条基本无害。所以升级前值得留一份基线：
  ```powershell
  pwsh -NoProfile -File D:\project\VxKex-NEXT\docx\vxkex-read-log.ps1 -Name opencode > D:\project\VxKex-NEXT\.tools\vxl-baseline-opencode-<版本号>.txt
  ```
- **看时间顺序**。用 `-Dump` 输出完整日志，紧挨在程序退出之前的那几条最可疑。
- **与程序报错里的名字对应**。能对上就是它。

修法与 A 类步骤 3 到 6 相同。注意 `Y` 是程序实际去查找的那个模块：`agy.exe` 加载的是 `api-ms-win-core-realtime-l1-1-1.dll`，被重定向到 `kxbase`，所以函数要加在 `KxBase`。

---

## 六、 C 类：函数存在，但参数或标志 Win7 不认

**本仓库案例**：Bun 在 `recv` 里固定传 Win8 才有的标志 `MSG_PUSH_IMMEDIATE (0x20)`，Win7 返回 `WSAEOPNOTSUPP (10045)`，表现为 `ENOTSUP`；`WSAIoctl` 的 `SIO_TCP_INITIAL_RTO` 同理。

特点：两个脚本都查不出来，日志里通常也没有记录，因为调用是"成功找到函数、函数返回了错误"。只能靠缩小范围：

1. 确认故障发生在哪个动作上（联网、读文件、开子进程……），由此圈定是哪一组 API。
2. 找一个最小复现，例如用临时本地代理看请求有没有发出来、发到了哪一步。
3. 在调用方里找可疑的常量：Win8 以后新增的标志位、控制码、信息类编号。
4. 修法是在对应的 Kx DLL 里包装该函数，去掉或翻译 Win7 不认的部分。只处理确认过的那几个值，其余原样传给系统。

这一类工作量最大，排查方法详见 [`Bun在Win7下ENOTSUP与网络失败根因逆向分析-20261101.md`](Bun在Win7下ENOTSUP与网络失败根因逆向分析-20261101.md)。

---

## 七、 D 类：整个 DLL 缺失，或调用绕过了 VxKex

两种情形：

1. **导入了 `redirects.h` 里没有的新 API 集**（`api-ms-win-*` 或 `ext-ms-win-*`）。导入核对脚本会报 `DLL file not found`。修法是在 `redirects.h` 里加一条映射，指向已经实现了这些函数的 Kx DLL，再检查函数是否齐全。注意改名后的名字不能比原名长。
2. **调用经由系统 DLL 绕过了 VxKex**。**本仓库案例**：Bun 延迟加载 `wsock32.dll`，而 `wsock32.dll` 在 Windows 目录里，它内部对 `ws2_32.dll` 的调用没有被改到 `kxnet`，所以 `kxnet` 里的修复不生效。修法是把这个系统 DLL 加进 `KexIsRewriteForcedWindowsDll`。判断依据：明明已经在 Kx DLL 里实现了修复，却完全没效果，就要怀疑调用没经过它。

---

## 八、 E 类：先排除与 API 无关的问题

这些问题的表现也是"升级后不能用"，但和 VxKex 的函数实现无关，先花一分钟排除：

| 现象 | 实际原因 | 记录 |
| --- | --- | --- |
| npm 装完后 `bin\xxx.exe` 只有几百字节，是一段文本 | 安装脚本被 npm 拦截没有运行，需要 `--allow-scripts=<包名>` | [`opencode2安装问题说明及解决措施-20261022.md`](opencode2安装问题说明及解决措施-20261022.md) |
| Antigravity 每次重启都重新下载约 200MB 后端 | 扩展的版本探测超时后直接重下，不核对本地文件；扩展升级会覆盖补丁 | `AGENTS.md` 相关约定、[`Antigravity重复下载安装原因与修复-20261023.md`](Antigravity重复下载安装原因与修复-20261023.md) |
| Codex 侧栏时好时坏，报启动超时 | 界面加载慢，与内存压力有关，后端其实正常 | [`Codex插件间歇启动失败复查-20261101.md`](Codex插件间歇启动失败复查-20261101.md) |
| 界面乱码、错位、没有高亮、中文输入夹杂字母 | 控制台转义序列或代码页问题，不是加载问题 | `AGENTS.md` 最新会话任务各条 |

快速检查入口文件是不是真的可执行程序：

```powershell
Get-Item "<exe 路径>" | Select-Object Length
[System.IO.File]::ReadAllBytes("<exe 路径>")[0..1]   # 应为 77 90，即 "MZ"
```

---

## 九、 升级前预检与止损

**升级前**（把问题挡在正式环境之外）：

1. 记下当前能用的版本号，并按第五节的命令留一份日志基线。
2. 把新版本装到临时目录，不动正式安装：
   ```powershell
   npm install -g --prefix "D:\project\VxKex-NEXT\.tools\precheck-<名称>-<版本>" --allow-scripts=<包名> <包名>@<版本>
   ```
3. 对临时目录里的原生 exe 跑导入核对脚本。
4. 有缺失就先不升级，按第四节处理完再升。

预检的局限：Bun 单文件程序内嵌的原生库要运行到那一步才会解压出来，静态扫主程序扫不到。OpenCode 2.3.0 这次就是主程序 458 个导入全部正常，问题出在解压出来的渲染库里。所以预检通过后仍要在临时目录里实际启动一次，再按修改时间检查新出现的文件：

```powershell
Get-ChildItem $env:TEMP -Force -Filter ".bun-*" | Sort-Object LastWriteTime -Descending | Select-Object -First 5 Name, Length, LastWriteTime
```

**升级后发现不能用**：

1. 先保留现场：报错原文、日志文件名、失败 DLL 的路径和 SHA256。临时目录里的文件和日志都可能被清理。
2. 需要继续干活就先退回旧版本，例如：
   ```powershell
   npm install -g --prefix "D:\project\claude-code-engineering\node-v22.22.2-win-x64" --allow-scripts=@opencode/cli @opencode/cli@2.2.5
   ```
   VS Code 扩展在扩展面板该扩展的齿轮菜单里选 Install Another Version（安装另一个版本），并关闭该扩展的自动更新。
3. 然后按本手册排查，修复部署并验收通过后再升回去。

---

## 十、 工具说明与局限

| 工具 | 作用 | 查不到的 |
| --- | --- | --- |
| [`docx\vxkex-read-log.ps1`](vxkex-read-log.ps1) | 汇总 `.vxl` 日志里的加载失败、失败的函数查找、DLL 改名记录 | 函数调用成功但返回错误的情况（C 类） |
| [`docx\vxkex-check-imports.ps1`](vxkex-check-imports.ps1) | 按 VxKex 的改名规则核对一个 PE 文件的全部静态导入和延迟导入，跟踪转发链 | 运行时用字符串查找的函数（B 类）；行为差异（C 类）；内嵌在单文件程序里、尚未解压的原生库 |

`vxkex-check-imports.ps1` 的几点说明：

- 改名规则读自仓库的 `KexDll\redirects.h`。如果源码里的表比已安装的版本新，结果可能与实际不符；以实际日志里的 `Rewrote DLL import` 为准。
- 被检查的文件如果本身位于 Windows 目录或 VxKex 目录，VxKex 实际上不会改它的导入，脚本的结果不适用。
- 搜索顺序是 `-KexDir`、目标文件所在目录、系统目录、`-ExtraSearchDir`，与日志里的 `DllPath` 一致。
- `-KexDir` 指向 `x64\Release` 时，脚本会自动把已安装目录 `C:\Program Files\VxKex\Kex64` 排在它后面。原因是 `ucrtbase.dll` 这类文件不由本仓库构建，只存在于已安装目录；安装新包后的实际效果也正是"新构建的 DLL 覆盖旧的，其余保持不变"。输出开头的 `Fallback` 行会显示这一点，`-NoInstalledFallback` 可以关掉。
- 32 位程序自动改用 `Kex32` 和 `SysWOW64`。32 位路径只按代码逻辑写了，没有拿真实的 32 位程序测过。

已有的 `test\check_codex_*.ps1`、`test\read_vxl.ps1` 是早期针对 Codex 写死路径的版本，功能已被上面两个脚本覆盖。

---

## 十一、 历史案例索引

| 日期 | 程序与版本 | 类型 | 缺失或不兼容的内容 | 落点 | 记录 |
| --- | --- | --- | --- | --- | --- |
| 2026-10-12 | Cline CLI 3.0.70 | A | `ClosePseudoConsole` 等伪控制台函数 | `KxBase` | [`cline-vxkex启动失败排查-20261012.md`](cline-vxkex启动失败排查-20261012.md) |
| 2026-11-01 | Antigravity 后端 `agy.exe` 1.4.12 | B | `QueryInterruptTimePrecise`、`QueryInterruptTime` | `KxBase\time.c` | [`KxBase补充QueryInterruptTimePrecise导出支持Antigravity升级设计-20261101.md`](KxBase补充QueryInterruptTimePrecise导出支持Antigravity升级设计-20261101.md) |
| 2026-11-01 | Bun 运行时（Claude Code 原生版、OpenCode） | C + D | `MSG_PUSH_IMMEDIATE`、`SIO_TCP_INITIAL_RTO`；`wsock32` 绕过 | `KxNet\winsock.c`、`KexDll\dllrewrt.c` | [`VxKex-NEXT修复Bun网络兼容性设计方案-20261101.md`](VxKex-NEXT修复Bun网络兼容性设计方案-20261101.md) |
| 2026-11-13（2026-11-14 验收） | OpenCode 2.3.0 渲染库 | A | `NtSetInformationVirtualMemory` | `KexDll\ntvmem.c`、`kxnt.def` | [`OpenCode-2.3.0升级后error-code-127原因分析与修复方案-20261113.md`](OpenCode-2.3.0升级后error-code-127原因分析与修复方案-20261113.md) |

以后每处理一例，在这张表里加一行。
