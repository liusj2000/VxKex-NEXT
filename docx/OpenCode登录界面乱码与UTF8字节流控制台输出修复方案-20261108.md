# OpenCode 登录界面乱码（`鈹?` / `鈼?` / `鈻?枲`）根因分析与修复方案

**初版日期**：2026-11-08  
**评审修订**：2026-11-13（本版替换了初版第二、三节的根因描述与修复方案，修改原因见第三节）  
**状态**：2026-11-13 已按第五节实施、构建、离线测试并打包（结果见附 C）；用户已部署新安装包并按第六节验收通过（见附 D）；已合并到 `main` 并推送  
**目标环境**：Windows 7 专业版 SP1 x64 (Build 7601)，仓库 HEAD `5f355efad4ce8d70c72f6cc1234282e9b45abe6d`  
**涉及文件**：

- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\console.c`（核心修改）
- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\file.c`（一行条件）
- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbasep.h`、`D:\project\VxKex-NEXT\00-Common Headers\KxBase.h`（声明）
- `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbase.def`、`D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\forwards.c`（导出）
- `D:\project\VxKex-NEXT\test\ansi-features-test.c`（回归测试 Test 8）
- **不修改** `D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\conansi.c`（初版方案改的是这个文件，本版不再动它）

---

## 〇、评审结论

初版方案的**方向是对的**：在用户态把 UTF-8 字节解码成 UTF-16，用 `WriteConsoleW` 上屏，并向调用方返回原始字节数。

但初版方案**不能照原样实施**。它在 `conansi.c` 里对每一次窄字符写入做“先严格按 UTF-8 解码，失败再按控制台代码页解码”的猜测，这会带来三个实测可复现的问题：UTF-8 字符被拆到两次写入时仍然乱码；一部分合法 GBK 文本会被误判成 UTF-8 而显示错字；GBK 回退路径破坏了 conhost 原本能正确处理的“前导字节与尾字节分两次写入”。另外它没有给大块输出切片，也没有覆盖未开启 VT 的写入路径。逐条证据见第三节。

修订后的方案不再猜编码，改为**记录应用自己声明的输出代码页**：

> 只有当**本进程调用过 `SetConsoleOutputCP(65001)`**，而**真实控制台输出代码页当前不是 65001**（被 PowerShell 的 936 守护改回去了）时，KxBase 才把窄字符控制台输出按 UTF-8 解码后走 `WriteConsoleW`。其他情况一律保持现有行为，一个字节都不改。

这个规则对 GBK 程序没有回归面，对 OpenCode 是确定性的。

---

## 一、 问题现象与字符级还原

在 VS Code 集成终端（`pwsh`）中执行 `opencode auth login` 时，交互界面中的框线、步骤图标与 API Key 密码掩码全部显示为生僻汉字与问号乱码：

| 截图实际乱码显示 | 原本应显示的字符 | Unicode 码位 | UTF-8 原始字节 (Hex) | 被按 CP936 (GBK) 错误解码的字节拆分过程 |
| :--- | :--- | :--- | :--- | :--- |
| `鈹? Connect an integration` | `┌  Connect an integration` | `U+250C` (`┌`) | `E2 94 8C` (+ `20`) | `0xE2 0x94` → **`鈹`** (`U+9239`)；`0x8C` 在 GBK 中无后续尾字节 → **`?`** |
| `鈼? Select integration` | `◇  Select integration` / `●` | `U+25C7` (`◇`) / `U+25CF` (`●`) | `E2 97 87` / `E2 97 8F` | `0xE2 0x97` → **`鈼`** (`U+923C`)；`0x87`/`0x8F` → **`?`** |
| `鈹? DeepSeek` / `鈹` | `│  DeepSeek` / `│` | `U+2502` (`│`) | `E2 94 82` | `0xE2 0x94` → **`鈹`** (`U+9239`)；`0x82` → **`?`**（行尾被截断为 `鈹`） |
| `鈹? 鈻?枲鈻?枲鈻?枲...` | `│  ■■■■■■■■...` | `U+25A0` (`■`) 连续重复 | `E2 96 A0 E2 96 A0 ...` | 每两个 `■`（6 字节）：`0xE2 0x96` → **`鈻`** (`U+923B`)；`0xA0 0xE2` 非法 → **`?`**；`0x96 0xA0` → **`枲`** (`U+67B2`) |
| `鈹? Done` | `└  Done` | `U+2514` (`└`) | `E2 94 94` (+ `20`) | `0xE2 0x94` → **`鈹`** (`U+9239`)；`0x94` → **`?`** |

2026-11-13 用仓库已有构建 `D:\project\VxKex-NEXT\x64\Release\KxBase.dll`（旧代码）复现：真实输出代码页为 936 时，经 `KxBase!WriteConsoleA` 与 `KxBase!WriteFile` 写入 `┌ Connect◇■■` 的 UTF-8 字节，回读屏幕缓冲区为 `9239 003F 'Connect' 923C 56E3 67B2 923B`，即截图中的 `鈹?`、`鈼`、`枲`、`鈻`。

---

## 二、 根因（修订）

乱码是三件事叠加的结果，`KxBase` 在其中只是**忠实转发**，并没有独立的解码缺陷。

1. **`@clack/prompts` 输出 Unicode 符号**  
   VS Code 终端注入 `TERM_PROGRAM=vscode`，`is-unicode-supported` 返回 `true`，于是 `opencode auth login` 输出 `┌ │ └ ◇ ■`；没有这个环境变量时输出的是 ASCII 的 `T | *`。

2. **`opencode.exe` 是 Bun，启动时把控制台切到 65001，然后写 UTF-8 字节**  
   - `opencode.exe` 内嵌字符串为 `Bun v1.4.4`，导入表静态导入 `KERNEL32!SetConsoleOutputCP`、`SetConsoleCP`、`GetConsoleOutputCP`、`WriteFile`、`WriteConsoleA`、`WriteConsoleW` 以及 `ntdll!NtWriteFile`（dumpbin 实查）。
   - 2026-11-13 实测：在独立隐藏控制台运行 `opencode.exe --version`，启动后约 47 ms 输出与输入代码页由 936 变为 65001，进程退出时恢复 936。
   - 也就是说，Bun 的约定是“我已经把控制台设成 65001，所以我写 UTF-8 字节”。

3. **PowerShell 的 936 守护把真实代码页改回去**  
   `C:\Users\liusj\Documents\PowerShell\VxKex.OpenCode.ps1`（源文件 `D:\project\VxKex-NEXT\docx\opencode-win7-console.ps1`）中的 `Invoke-KxConsole936` 在 OpenCode 运行期间每 100 ms 检查一次并把代码页恢复为 936。这个守护是必须的：Win7 conhost 只有在 936 下汉字才按双列宽推进、输入法才可用（见 AGENTS.md 第 2 项）。

4. **结果**  
   Bun 认为控制台是 65001 并继续写 UTF-8；真实控制台已是 936。字节沿
   `Ext_WriteFile`（`file.c`）→ `Ext_WriteConsoleA`（`console.c`）→ `WriteConsoleWithEscapeSequencesAorW(..., Unicode = FALSE)`（`conansi.c`）→ 原生 `WriteConsoleA`
   到达 conhost，被按 GBK 解码成 `鈹?`。  
   `KexDll\ntcondrv.c` 的 `Ext_NtWriteFile` 在 ConDrv 仿真开启时同样转到 `KxBase` 的 `WriteConsoleA` 导出，所以三条窄字符入口最终都汇聚到 `Ext_WriteConsoleA`。

因此修复点应当是：**KxBase 知道应用声明的是 65001，却把字节交给了一个实际是 936 的控制台**。这正是修订方案处理的状态。

> 初版第二节的“缺陷 B（字节数与字符数混淆）”在 936 下不成立，见第三节第 4 条。

---

## 三、 初版方案的问题与修改原因

以下实测均在本机 Win7 SP1 x64 的独立隐藏控制台中完成，日期 2026-11-13。

| # | 初版内容 | 问题 | 实测证据 | 修订做法 |
| :-- | :--- | :--- | :--- | :--- |
| 1 | 对每次写入的整块缓冲区做严格 UTF-8 解码（`MB_ERR_INVALID_CHARS`），无跨调用状态 | 一个 UTF-8 字符被拆到两次写入时，前后两块都解码失败，整块回退到 936，照样乱码 | `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)` 对 `61 62 E2 94` 返回 0（错误 1113）；对孤立的 `8C 61 62` 也返回 0 | 保留末尾不完整序列（最多 3 字节）到下一次写入再拼接；解码标志用 0，非法字节显示为 U+FFFD 而不是拖垮整块 |
| 2 | “UTF-8 优先”猜测编码 | 合法 GBK 文本会被误判为 UTF-8，所有在 VT 仿真下输出 GBK 的程序都有回归风险 | GBK “平”= `C6 BD`，同时是合法 UTF-8（U+01BD）；“小平”= `D0 A1 C6 BD` 被严格 UTF-8 解码为 U+0421 U+01BD | 不猜。只在“应用声明 65001 且真实代码页不是 65001”时解码 |
| 3 | 解码失败时用 `MultiByteToWideChar(GetConsoleOutputCP(), 0)` 回退 | conhost 的原生 `WriteConsoleA` 能跨调用拼接 DBCS 前导字节，用户态逐块转换做不到 | 用户态转换孤立 `D6` 得到 `?`，孤立尾字节 `D0 61` 得到 U+884B 并吞掉 `a`；而原生 `WriteConsoleA` 分两次写 `D6` 与 `D0 61 62`，屏幕上是正确的 `中ab` | 非 UTF-8 场景完全不经过用户态转换，原样交给原生 `WriteConsoleA` |
| 4 | “缺陷 B”：`WriteConsoleA` 返回字符数，导致指针推进不足、吞掉后续 ESC | 在 936 下不成立 | 936 下 `WriteConsoleA` 写 GBK `中文ab` 返回 6（字节数）；旧 `KxBase` 在 936 下写 40 字节的 UTF-8 提示串返回 40。返回字符数（11 字节返回 5）只发生在真实代码页为 65001 且为 TrueType 字体时 | 不把它作为本次根因；真实代码页为 65001 的情形不在本次范围（见 4.3） |
| 5 | 转换后一次性调用 `WriteConsoleW` | 大块输出会失败，这是初版引入的回归 | 原生 `WriteConsoleW` 单次写 60000 个 WCHAR 失败（错误 8）；30000 个成功；`WriteConsoleA` 单次写 60000 字节成功 | 按 8192 个 WCHAR 切片，不拆代理对。切片后 120000 字节的输出实测成功 |
| 6 | 只改 `WriteConsoleWithEscapeSequencesAorW`（VT 路径） | 未开启 VT 的句柄走原生 `WriteConsoleA` / `WriteFile`，不经过该函数 | `Ext_WriteConsoleA` 第二分支与 `Ext_WriteFile` 的条件均以 VT 开启为前提 | 在 `Ext_WriteConsoleA` 入口统一判断；`Ext_WriteFile` 的转发条件补上同一判断 |
| 7 | 失败路径不写 `*CchWrittenOut` | 调用方可能读到未初始化的写入计数 | 代码审查 | 入口先置 0 |
| 8 | Test 8 只回读固定 16 格，无拆分用例、无 GBK 回归用例、无 `WriteFile` 用例 | 测不出上面第 1、2、3、6 条 | — | 重写 Test 8（8a–8g），并已用旧 DLL 验证它“修复前会红” |
| 9 | 构建命令 `cl.exe ... /Fe:"..."` | VC10 不接受 `/Fe:` 写法 | 本机 VC10 实测报错 | 改用工程文件构建；备用命令用 `/Fe"路径"` |
| 10 | “原生 Unicode 框线能完美渲染” | 未经验证的结论 | 936 下 `┌ ◇ ■` 属于 GBK 双宽字符，各占 2 格（回读 40 格得到 36 个字符，其中 4 个双宽），与 65001 下的单宽排版不同 | 改为待验收项，见第六、八节 |

---

## 四、 修订后的方案设计

### 4.1 规则

`KxBase` 新增 `Ext_SetConsoleOutputCP`，替换现有的纯转发导出。它照常调用原生 `SetConsoleOutputCP`，成功后把代码页记到进程内变量 `KxConsoleAppOutputCodePage`（“应用意图”）。

窄字符控制台输出是否按 UTF-8 解码，由 `KxBasepShouldDecodeConsoleOutputAsUtf8()` 决定，三个条件同时成立才返回 `TRUE`：

1. `KxConsoleAppOutputCodePage == 65001`（本进程声明过 UTF-8）；
2. 环境变量 `VXKEX_NO_CONSOLE_UTF8` 没有设为 `1` / `y` / `Y`（回滚开关）；
3. `GetConsoleOutputCP() != 65001`（真实控制台被别人改走了）。

| 应用意图 | 真实输出代码页 | 行为 |
| :--- | :--- | :--- |
| 从未调用（0） | 任意 | **不变**（原生 `WriteConsoleA`） |
| 936 等非 65001 | 任意 | **不变** |
| 65001 | 65001 | **不变**（原生路径，与现状相同） |
| 65001 | 936 等非 65001 | **新行为**：UTF-8 解码 → `WriteConsoleW` |

### 4.2 数据流

```
WriteFile / WriteConsoleA / NtWriteFile(ConDrv 仿真)
        └─► Ext_WriteConsoleA
              ├─ KxBasepShouldDecodeConsoleOutputAsUtf8() == TRUE
              │     └─► KxBasepWriteConsoleUtf8
              │           1. 取回上次留下的不完整 UTF-8 序列（≤3 字节）并拼到本次数据前
              │           2. 截下本次末尾的不完整序列，留给下一次
              │           3. MultiByteToWideChar(CP_UTF8, 0, ...)
              │           4. 每 8192 个 WCHAR 一片：
              │                VT 已开启 → WriteConsoleWithEscapeSequencesAorW(..., Unicode = TRUE)
              │                VT 未开启 → WriteConsoleW
              │           5. 向调用方返回本次传入的字节数
              └─ 否则：与现在完全相同
```

- 转义序列解析复用 `conansi.c` 已有的宽字符管线，所以 `conansi.c` 不需要改。
- 汉字经 `WriteConsoleW` 写入 936 控制台仍占 2 格（实测 UTF-8 的 `中文g` 写完后光标 X = 5），AGENTS.md 第 2 项修好的双列宽对齐不受影响。
- 跨调用状态只有一个槽位（句柄 + 最多 3 字节），用 SRW 锁保护，与 `conansi.c` 中 `g_PendingEscape` 的单槽约定一致。

### 4.3 范围与非目标

本次**只**解决“应用声明 65001、真实代码页不是 65001”这一状态。以下内容**不要**在本次实施中顺手做：

- 不把 65001 “虚拟化”（即不拦截、不吞掉对真实控制台的 `SetConsoleOutputCP(65001)`）。
- 不处理真实代码页为 65001 时 `WriteConsoleA` 返回字符数的问题。
- 不为从不调用 `SetConsoleOutputCP` 的程序做编码猜测。
- 不修改 `conansi.c`，不修改 `GetConsoleOutputCP` / `SetConsoleCP` 的转发。
- 不修改 PowerShell 守护脚本、Profile、IFEO 或已安装的 DLL。

---

## 五、 实施步骤（供实施模型逐条执行）

约定：C 源码用 **Tab 缩进**；VC10 按 C89 编译，**局部变量必须声明在块的开头**；`kxbase.vcxproj` 开启了“警告视为错误”（`/W3 /WX`）。下面的行号对应 HEAD `5f355ef`，仅作提示，请以文本定位为准。代码块请原样粘贴，不要改写、不要“优化”。

### 步骤 0：前置核查

```bat
git -C "D:\project\VxKex-NEXT" status --short
git -C "D:\project\VxKex-NEXT" rev-parse HEAD
```

预期只有 `docx` 下的文档改动。若 `01-Extended DLLs`、`00-Common Headers`、`test` 下已有未提交改动，先停下来向用户确认。

确认 `D:\project\VxKex-NEXT` 下保留有上一版带时间戳的安装包副本（`KexSetup_Release_1.2.3.2463_*.exe`）；`build-installer.cmd` 会覆盖 `KexSetup_Release.exe`。

### 步骤 1：`D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\console.c`

#### 1.1 插入新代码块

在函数 `KxBasepIsPseudoConsoleHandle` 结束之后、`KXBASEAPI BOOL WINAPI Ext_WriteConsoleA(` 之前（约第 173 行）插入以下整块代码：

```c
#define KX_CONSOLE_UTF8_WRITE_CHUNK_CCH 8192

STATIC volatile LONG KxConsoleAppOutputCodePage = 0;
STATIC RTL_SRWLOCK KxConsoleUtf8CarryLock = RTL_SRWLOCK_INIT;

STATIC struct {
	HANDLE	ConsoleHandle;
	ULONG	Cb;
	CHAR	Bytes[4];
} KxConsoleUtf8Carry = {0};

STATIC BOOLEAN KxBasepIsConsoleUtf8DecodeDisabled(VOID)
{
	STATIC LONG Initialized = 0;
	STATIC BOOLEAN Disabled = FALSE;

	if (InterlockedCompareExchange(&Initialized, 1, 0) == 0) {
		WCHAR EnvBuf[16];
		if (GetEnvironmentVariableW(L"VXKEX_NO_CONSOLE_UTF8", EnvBuf, ARRAYSIZE(EnvBuf)) > 0) {
			if (EnvBuf[0] == L'1' || EnvBuf[0] == L'y' || EnvBuf[0] == L'Y') {
				Disabled = TRUE;
			}
		}
	}

	return Disabled;
}

BOOLEAN KxBasepShouldDecodeConsoleOutputAsUtf8(VOID)
{
	if (KxConsoleAppOutputCodePage != CP_UTF8) {
		return FALSE;
	}

	if (KxBasepIsConsoleUtf8DecodeDisabled()) {
		return FALSE;
	}

	return (GetConsoleOutputCP() != CP_UTF8);
}

STATIC ULONG KxBasepUtf8IncompleteTailCb(
	IN	PCSTR	Buffer,
	IN	ULONG	Cb)
{
	ULONG Back;

	for (Back = 1; Back <= 3 && Back <= Cb; ++Back) {
		UCHAR Ch;
		ULONG Need;

		Ch = (UCHAR) Buffer[Cb - Back];

		if ((Ch & 0xC0) == 0x80) {
			continue;
		}

		if (Ch >= 0xC2 && Ch <= 0xDF) {
			Need = 2;
		} else if (Ch >= 0xE0 && Ch <= 0xEF) {
			Need = 3;
		} else if (Ch >= 0xF0 && Ch <= 0xF4) {
			Need = 4;
		} else {
			return 0;
		}

		return (Need > Back) ? Back : 0;
	}

	return 0;
}

STATIC BOOL KxBasepWriteConsoleUtf8(
	IN	HANDLE	ConsoleHandle,
	IN	PCSTR	Buffer,
	IN	ULONG	CbToWrite,
	OUT	PULONG	CbWrittenOut OPTIONAL,
	IN	PVOID	Reserved OPTIONAL,
	IN	BOOLEAN	ProcessAnsi)
{
	WCHAR StackWide[512];
	CHAR CarryBytes[4];
	PWCHAR WideBuffer;
	PCHAR Combined;
	PCSTR Source;
	ULONG CarryCb;
	ULONG SourceCb;
	ULONG TailCb;
	ULONG DecodeCb;
	ULONG WideCch;
	ULONG Offset;
	BOOL Success;

	if (CbWrittenOut) {
		*CbWrittenOut = 0;
	}

	if (CbToWrite == 0) {
		return TRUE;
	}

	if (!Buffer) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	WideBuffer = StackWide;
	Combined = NULL;
	Source = Buffer;
	SourceCb = CbToWrite;
	CarryCb = 0;
	Success = TRUE;

	//
	// Take over the incomplete UTF-8 sequence (1..3 bytes) left behind by the
	// previous write to the same handle, if any.
	//

	RtlAcquireSRWLockExclusive(&KxConsoleUtf8CarryLock);
	if (KxConsoleUtf8Carry.Cb > 0 && KxConsoleUtf8Carry.ConsoleHandle == ConsoleHandle) {
		CarryCb = KxConsoleUtf8Carry.Cb;
		RtlCopyMemory(CarryBytes, KxConsoleUtf8Carry.Bytes, CarryCb);
		KxConsoleUtf8Carry.Cb = 0;
	}
	RtlReleaseSRWLockExclusive(&KxConsoleUtf8CarryLock);

	if (CarryCb > 0) {
		Combined = SafeAlloc(CHAR, CarryCb + CbToWrite);
		if (!Combined) {
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return FALSE;
		}

		RtlCopyMemory(Combined, CarryBytes, CarryCb);
		RtlCopyMemory(Combined + CarryCb, Buffer, CbToWrite);
		Source = Combined;
		SourceCb = CarryCb + CbToWrite;
	}

	//
	// Do not decode a multibyte sequence that is cut off at the end of this
	// write. Keep it for the next write instead.
	//

	TailCb = KxBasepUtf8IncompleteTailCb(Source, SourceCb);
	DecodeCb = SourceCb - TailCb;

	if (DecodeCb > 0) {
		INT Converted;

		if (DecodeCb > ARRAYSIZE(StackWide)) {
			WideBuffer = SafeAlloc(WCHAR, DecodeCb);
			if (!WideBuffer) {
				WideBuffer = StackWide;
				SetLastError(ERROR_NOT_ENOUGH_MEMORY);
				Success = FALSE;
				goto Exit;
			}
		}

		// Flags must be 0 (not MB_ERR_INVALID_CHARS): invalid bytes become
		// U+FFFD and the rest of the text is still shown.
		Converted = MultiByteToWideChar(
			CP_UTF8,
			0,
			Source,
			(INT) DecodeCb,
			WideBuffer,
			(INT) DecodeCb);

		if (Converted <= 0) {
			Success = FALSE;
			goto Exit;
		}

		WideCch = (ULONG) Converted;
		Offset = 0;

		while (Offset < WideCch) {
			ULONG ChunkCch;
			ULONG CchDummy;

			ChunkCch = WideCch - Offset;

			if (ChunkCch > KX_CONSOLE_UTF8_WRITE_CHUNK_CCH) {
				ChunkCch = KX_CONSOLE_UTF8_WRITE_CHUNK_CCH;

				// Do not split a surrogate pair between two console writes.
				if (WideBuffer[Offset + ChunkCch - 1] >= 0xD800 &&
					WideBuffer[Offset + ChunkCch - 1] <= 0xDBFF) {
					--ChunkCch;
				}
			}

			if (ProcessAnsi) {
				Success = WriteConsoleWithEscapeSequencesAorW(
					ConsoleHandle,
					&WideBuffer[Offset],
					ChunkCch,
					&CchDummy,
					Reserved,
					TRUE);
			} else {
				Success = WriteConsoleW(
					ConsoleHandle,
					&WideBuffer[Offset],
					ChunkCch,
					&CchDummy,
					NULL);
			}

			if (!Success) {
				goto Exit;
			}

			Offset += ChunkCch;
		}
	}

	if (TailCb > 0) {
		RtlAcquireSRWLockExclusive(&KxConsoleUtf8CarryLock);
		RtlCopyMemory(KxConsoleUtf8Carry.Bytes, Source + DecodeCb, TailCb);
		KxConsoleUtf8Carry.Cb = TailCb;
		KxConsoleUtf8Carry.ConsoleHandle = ConsoleHandle;
		RtlReleaseSRWLockExclusive(&KxConsoleUtf8CarryLock);
	}

	// The caller counts in bytes of its own buffer.
	if (CbWrittenOut) {
		*CbWrittenOut = CbToWrite;
	}

Exit:
	if (WideBuffer != StackWide) {
		SafeFree(WideBuffer);
	}

	if (Combined) {
		SafeFree(Combined);
	}

	return Success;
}

KXBASEAPI BOOL WINAPI Ext_SetConsoleOutputCP(
	IN	UINT	CodePage)
{
	BOOL Success;

	Success = SetConsoleOutputCP(CodePage);

	if (Success) {
		// Remember which code page THIS process asked for. Another process
		// sharing the console may change the real one behind our back.
		InterlockedExchange(&KxConsoleAppOutputCodePage, (LONG) CodePage);
	}

	return Success;
}
```

#### 1.2 替换 `Ext_WriteConsoleA`（原第 174–197 行）

修改前：

```c
KXBASEAPI BOOL WINAPI Ext_WriteConsoleA(
	IN	HANDLE	ConsoleHandle,
	IN	PCVOID	Buffer,
	IN	ULONG	CchToWrite,
	OUT	PULONG	CchWritten OPTIONAL,
	IN	PVOID	Reserved OPTIONAL)
{
	if (KxBasepShouldProcessConsoleAnsi(ConsoleHandle)) {
		return WriteConsoleWithEscapeSequencesAorW(
			ConsoleHandle,
			Buffer,
			CchToWrite,
			CchWritten,
			Reserved,
			FALSE);
	}

	return WriteConsoleA(
		ConsoleHandle,
		Buffer,
		CchToWrite,
		CchWritten,
		Reserved);
}
```

修改后：

```c
KXBASEAPI BOOL WINAPI Ext_WriteConsoleA(
	IN	HANDLE	ConsoleHandle,
	IN	PCVOID	Buffer,
	IN	ULONG	CchToWrite,
	OUT	PULONG	CchWritten OPTIONAL,
	IN	PVOID	Reserved OPTIONAL)
{
	BOOLEAN ProcessAnsi;

	ProcessAnsi = KxBasepShouldProcessConsoleAnsi(ConsoleHandle);

	//
	// The application asked for UTF-8 output (SetConsoleOutputCP(65001)) but
	// the real console is currently on another code page, e.g. 936 kept by a
	// shell for DBCS cell width and IME. Decode here instead of letting
	// conhost decode UTF-8 bytes as GBK.
	//

	if (KxBasepShouldDecodeConsoleOutputAsUtf8()) {
		return KxBasepWriteConsoleUtf8(
			ConsoleHandle,
			(PCSTR) Buffer,
			CchToWrite,
			CchWritten,
			Reserved,
			ProcessAnsi);
	}

	if (ProcessAnsi) {
		return WriteConsoleWithEscapeSequencesAorW(
			ConsoleHandle,
			Buffer,
			CchToWrite,
			CchWritten,
			Reserved,
			FALSE);
	}

	return WriteConsoleA(
		ConsoleHandle,
		Buffer,
		CchToWrite,
		CchWritten,
		Reserved);
}
```

#### 1.3 修改 `Ext_FreeConsole`（原第 335–343 行）

修改前：

```c
KXBASEAPI BOOL WINAPI Ext_FreeConsole(
	VOID)
{
	RtlAcquireSRWLockExclusive(&KxConsoleModeLock);
	RtlZeroMemory(KxConsoleModes, sizeof(KxConsoleModes));
	RtlReleaseSRWLockExclusive(&KxConsoleModeLock);
	BaseDisableConsoleAnsiSupport(GetStdHandle(STD_OUTPUT_HANDLE));
	return FreeConsole();
}
```

修改后：

```c
KXBASEAPI BOOL WINAPI Ext_FreeConsole(
	VOID)
{
	RtlAcquireSRWLockExclusive(&KxConsoleModeLock);
	RtlZeroMemory(KxConsoleModes, sizeof(KxConsoleModes));
	RtlReleaseSRWLockExclusive(&KxConsoleModeLock);

	// A new console starts with its own code page; forget the old intent.
	RtlAcquireSRWLockExclusive(&KxConsoleUtf8CarryLock);
	KxConsoleUtf8Carry.Cb = 0;
	KxConsoleUtf8Carry.ConsoleHandle = NULL;
	RtlReleaseSRWLockExclusive(&KxConsoleUtf8CarryLock);
	InterlockedExchange(&KxConsoleAppOutputCodePage, 0);

	BaseDisableConsoleAnsiSupport(GetStdHandle(STD_OUTPUT_HANDLE));
	return FreeConsole();
}
```

### 步骤 2：`D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\file.c`

在 `Ext_WriteFile` 中（约第 324 行），只改这一行条件，其余不动。

修改前：

```c
	if (IsConsoleHandle(FileHandle) && BaseIsConsoleAnsiSupportEnabled(FileHandle)) {
```

修改后：

```c
	if (IsConsoleHandle(FileHandle) &&
		(BaseIsConsoleAnsiSupportEnabled(FileHandle) || KxBasepShouldDecodeConsoleOutputAsUtf8())) {
```

### 步骤 3：`D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbasep.h`

在 `WriteConsoleWithEscapeSequencesAorW` 的声明之后、`// consup.c` 注释块之前（约第 190 行）插入：

```c
//
// console.c
//

BOOLEAN KxBasepShouldDecodeConsoleOutputAsUtf8(
	VOID);

```

### 步骤 4：`D:\project\VxKex-NEXT\00-Common Headers\KxBase.h`

在这一行（约第 927 行）之后：

```c
KXBASEAPI BOOL WINAPI Ext_SetConsoleMode(IN HANDLE ConsoleHandle, IN ULONG Mode);
```

增加一行：

```c
KXBASEAPI BOOL WINAPI Ext_SetConsoleOutputCP(IN UINT CodePage);
```

### 步骤 5：`D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbase.def`

在 `;; console.c` 段的 `FreeConsole = Ext_FreeConsole` 一行（约第 243 行）之后增加一行，用 Tab 对齐成与相邻行相同的样式：

```
	SetConsoleOutputCP						= Ext_SetConsoleOutputCP
```

### 步骤 6：`D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\forwards.c`

把第 1090 行注释掉（与第 1085 行 `SetConsoleMode` 的处理方式相同），否则会出现重复导出：

修改前：

```c
#pragma comment(linker, "/EXPORT:SetConsoleOutputCP=kernel32.SetConsoleOutputCP")
```

修改后：

```c
//#pragma comment(linker, "/EXPORT:SetConsoleOutputCP=kernel32.SetConsoleOutputCP")
```

`GetConsoleOutputCP`（第 438 行）与 `SetConsoleCP`（第 1068 行）的转发保持不变。

### 步骤 7：`D:\project\VxKex-NEXT\test\ansi-features-test.c`

该文件用 4 个空格缩进。

#### 7.1 在 `#define WRITE_STR(...)` 一行之后、`int main(void)` 之前插入以下整块代码

```c
typedef BOOL (WINAPI *EXT_SETCONSOLEOUTPUTCP)(UINT);
typedef BOOL (WINAPI *EXT_WRITEFILE)(HANDLE, const VOID *, DWORD, LPDWORD, LPOVERLAPPED);

static void t8_begin(HANDLE conout, COORD pos)
{
    DWORD n = 0;

    FillConsoleOutputCharacterW(conout, L' ', 80, pos, &n);
    SetConsoleCursorPosition(conout, pos);
}

// "line" must have room for 64 WCHARs. A double-width (DBCS) character is
// returned as ONE WCHAR, so compare by character index, not by column.
static void t8_read(HANDLE conout, COORD pos, WCHAR *line)
{
    DWORD n = 0;
    DWORD i;

    RtlZeroMemory(line, 64 * sizeof(WCHAR));
    ReadConsoleOutputCharacterW(conout, line, 40, pos, &n);
    printf("    read %lu chars:", n);
    for (i = 0; i < n && i < 14; ++i) {
        printf(" %04X", line[i]);
    }
    printf("\n");
}

static SHORT t8_cursor_x(HANDLE conout)
{
    CONSOLE_SCREEN_BUFFER_INFO info;

    RtlZeroMemory(&info, sizeof(info));
    GetConsoleScreenBufferInfo(conout, &info);
    return info.dwCursorPosition.X;
}

static void RunTest8(
    HMODULE dll,
    HANDLE conout,
    EXT_SETCONSOLEMODE setmode,
    EXT_WRITECONSOLEA write_a,
    DWORD vtMode)
{
    // "<cyan>U+250C<reset> Connect<green>U+25C7<reset>U+25A0 U+25A0" as UTF-8
    static const char prompt[] =
        "\x1b[36m\xe2\x94\x8c\x1b[39m Connect\x1b[32m\xe2\x97\x87\x1b[39m\xe2\x96\xa0\xe2\x96\xa0";
    EXT_SETCONSOLEOUTPUTCP kx_setocp;
    EXT_SETCONSOLEOUTPUTCP native_setocp;
    EXT_WRITEFILE kx_writefile;
    CONSOLE_SCREEN_BUFFER_INFO sbi8;
    COORD pos;
    WCHAR line[64];
    DWORD written;
    DWORD promptCb;
    UINT origOutCP;

    printf("\n--- Test 8: UTF-8 console output while the real output code page is 936 ---\n");

    kx_setocp = (EXT_SETCONSOLEOUTPUTCP) GetProcAddress(dll, "SetConsoleOutputCP");
    kx_writefile = (EXT_WRITEFILE) GetProcAddress(dll, "WriteFile");
    native_setocp = (EXT_SETCONSOLEOUTPUTCP) GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetConsoleOutputCP");

    CHECK(kx_setocp != NULL);
    CHECK(kx_writefile != NULL);
    CHECK(native_setocp != NULL);
    if (!kx_setocp || !kx_writefile || !native_setocp) {
        return;
    }

    // Negative control: fails while SetConsoleOutputCP is still a plain
    // forwarder to kernel32 (KxBase.dll without the fix).
    CHECK(kx_setocp != native_setocp);

    promptCb = (DWORD) strlen(prompt);
    origOutCP = GetConsoleOutputCP();

    RtlZeroMemory(&sbi8, sizeof(sbi8));
    GetConsoleScreenBufferInfo(conout, &sbi8);
    pos.X = 0;
    pos.Y = (SHORT) (sbi8.srWindow.Top + 12);

    // The app asks for UTF-8 through KxBase, then something else (the
    // PowerShell 936 guard) switches the real console back to 936.
    CHECK(kx_setocp(65001));
    CHECK(native_setocp(936));
    printf("real output CP is now %u (expected 936)\n", GetConsoleOutputCP());
    CHECK(GetConsoleOutputCP() == 936);

    printf("8a: whole UTF-8 prompt through WriteConsoleA\n");
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, prompt, promptCb, &written, NULL));
    printf("    expected bytes=%lu written=%lu\n", promptCb, written);
    CHECK(written == promptCb);
    t8_read(conout, pos, line);
    CHECK(wcsncmp(line, L"\x250C Connect\x25C7\x25A0\x25A0", 12) == 0);

    printf("8b: one UTF-8 character split over three writes\n");
    t8_begin(conout, pos);
    written = 99;
    CHECK(write_a(conout, "\xE2", 1, &written, NULL) && written == 1);
    written = 99;
    CHECK(write_a(conout, "\x94", 1, &written, NULL) && written == 1);
    written = 99;
    CHECK(write_a(conout, "\x8C" "ab", 3, &written, NULL) && written == 3);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x250C && line[1] == L'a' && line[2] == L'b');

    printf("8c: split inside a character that sits between two colour sequences\n");
    t8_begin(conout, pos);
    written = 99;
    CHECK(write_a(conout, "\x1b[32m\xE2\x97", 7, &written, NULL) && written == 7);
    written = 99;
    CHECK(write_a(conout, "\x87\x1b[39mZ", 7, &written, NULL) && written == 7);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x25C7 && line[1] == L'Z');

    printf("8d: UTF-8 Chinese keeps the double cell width of code page 936\n");
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xE4\xB8\xAD\xE6\x96\x87" "g", 7, &written, NULL) && written == 7);
    t8_read(conout, pos, line);
    printf("    cursor X=%d (expected 5)\n", t8_cursor_x(conout));
    CHECK(line[0] == 0x4E2D && line[1] == 0x6587 && line[2] == L'g');
    CHECK(t8_cursor_x(conout) == 5);

    printf("8e: whole UTF-8 prompt through WriteFile\n");
    t8_begin(conout, pos);
    written = 0;
    CHECK(kx_writefile(conout, prompt, promptCb, &written, NULL));
    printf("    expected bytes=%lu written=%lu\n", promptCb, written);
    CHECK(written == promptCb);
    t8_read(conout, pos, line);
    CHECK(wcsncmp(line, L"\x250C Connect\x25C7\x25A0\x25A0", 12) == 0);

    printf("8f: same rule while VT processing is switched off\n");
    CHECK(setmode(conout, vtMode & ~0x0004));
    t8_begin(conout, pos);
    written = 0;
    CHECK(kx_writefile(conout, "\xe2\x94\x8c Done", 8, &written, NULL) && written == 8);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x250C && line[1] == L' ' && line[2] == L'D');
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xe2\x97\x87" "X", 4, &written, NULL) && written == 4);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x25C7 && line[1] == L'X');
    CHECK(setmode(conout, vtMode));

    // 8g protects EXISTING behaviour: an app that asked for 936 (or never
    // asked) keeps the native WriteConsoleA path byte for byte.
    printf("8g: legacy GBK output is untouched when the app asked for 936\n");
    CHECK(kx_setocp(936));

    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xD6\xD0\xCE\xC4" "ab", 6, &written, NULL));
    printf("    GBK written=%lu (expected 6)\n", written);
    CHECK(written == 6);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x4E2D && line[1] == 0x6587 && line[2] == L'a' && line[3] == L'b');

    // GBK lead byte and trail byte in two separate writes.
    t8_begin(conout, pos);
    written = 99;
    CHECK(write_a(conout, "\xD6", 1, &written, NULL));
    written = 99;
    CHECK(write_a(conout, "\xD0" "ab", 3, &written, NULL));
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x4E2D && line[1] == L'a' && line[2] == L'b');

    // These four GBK bytes are also well-formed UTF-8 (U+0421 U+01BD).
    // They must still be shown as the two Chinese characters U+5C0F U+5E73.
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xD0\xA1\xC6\xBD", 4, &written, NULL) && written == 4);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x5C0F && line[1] == 0x5E73);

    t8_begin(conout, pos);
    CHECK(kx_setocp(origOutCP ? origOutCP : 936));
}
```

注意字符串里 `"\x8C" "ab"`、`"\xD0" "ab"` 这类写法是有意拆开的：C 的十六进制转义会贪婪吞掉后面的 `a`–`f`，不要把它们合并成一个字面量。

#### 7.2 在 `main` 中调用

在 `printf("\n=== SUMMARY ===\n");` 之前增加一行：

```c
    RunTest8(dll, conout, setmode, write_a, mode);
```

### 步骤 8：构建与离线测试

#### 8.1（建议）先确认 Test 8 在修复前是红的

只完成步骤 7、尚未改任何 KxBase 源码时，按 8.3 构建并运行测试。测试程序固定加载 `D:\project\VxKex-NEXT\x64\Release\KxBase.dll`（此时还是旧代码的构建）。Test 8 应当**恰好 9 个 `[FAIL]`**：钩子检查 1 个、8a 1 个、8b 1 个、8c 1 个、8d 2 个、8e 1 个、8f 2 个；8g 全部 `[PASS]`。

这一结果已在 2026-11-13 用等价的独立测试程序对该 DLL（SHA256 `491F6E30C14442E82FA6AC4153025BEEF60459ABA3A117751DEBB8A865AF55A2`）实测得到。8g 通过说明它保护的是既有行为。

#### 8.2 构建 KxBase（x64 Release）

```bat
call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /x64 /Release
set VxKex_NEXT_Platform_Toolset=Windows7.1SDK
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe" "D:\project\VxKex-NEXT\01-Extended DLLs\KxBase\kxbase.vcxproj" /p:SolutionDir=D:\project\VxKex-NEXT\ /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m
```

`/p:TrackFileAccess=false` 与 `/m:1` 不能省（原因见 AGENTS.md “已知阻断”）。构建后检查导出不再是转发：

```bat
dumpbin /exports "D:\project\VxKex-NEXT\x64\Release\KxBase.dll" | findstr /i "SetConsoleOutputCP"
```

输出行中**不应**出现 `forwarded to kernel32.SetConsoleOutputCP`。

#### 8.3 构建并运行离线测试

```bat
"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe" "D:\project\VxKex-NEXT\test\ansi-features-test.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=Windows7.1SDK /p:TrackFileAccess=false /m:1 /nologo /v:m
"D:\project\VxKex-NEXT\test\x64\Release\ansi-features-test.exe" > "D:\project\VxKex-NEXT\test\ansi-features-test.log" 2>&1
echo EXIT=%ERRORLEVEL%
```

回读 `D:\project\VxKex-NEXT\test\ansi-features-test.log`，要求没有任何 `[FAIL]` 行、末尾为 `ALL TESTS PASSED SUCCESSFULLY! (failures = 0)`、退出码 0。日志是生成物，不要提交。

注意：Test 2（DEC 线框字符）要求控制台初始代码页是单字节代码页（如 437）。在初始为 936 的控制台中运行时，`▒`（U+2592）在 GBK 下是双宽字符，第 324 行 `readBuf[1] == 0x2500` 会失败。修改前的 DLL 也是如此，与本修复无关。因此判定标准是：**初始代码页 437 下全部通过；初始代码页 936 下只允许这一条失败**。Test 8 会自己设置 936，两种情况下都必须全部通过。

测试需要一个真实的控制台（它打开 `CONOUT$` 并回读屏幕缓冲区）。如果日志开头出现打不开控制台之类的提示，或终端工具拿不到真实退出码，改为在新开的控制台窗口中运行（本轮评审的实测就是在独立的新控制台中做的）：

```bat
start "" /wait /min cmd /c ""D:\project\VxKex-NEXT\test\x64\Release\ansi-features-test.exe" > "D:\project\VxKex-NEXT\test\ansi-features-test.log" 2>&1"
```

这种方式下以日志末尾的 SUMMARY 行为准。

如果工程文件构建不可用，备用命令（VC10 的 `/Fe`、`/Fo` 后面直接跟路径，没有冒号）：

```bat
cl.exe /nologo /W3 /O2 /MT /TC "D:\project\VxKex-NEXT\test\ansi-features-test.c" /Fe"D:\project\VxKex-NEXT\test\x64\Release\ansi-features-test.exe" /Fo"D:\project\VxKex-NEXT\test\x64\Release\ansi-features-test.obj"
```

说明：8.2 的 MSBuild 命令照搬自 `D:\project\VxKex-NEXT\test\build_test_targets.cmd` 中构建 KxBase 的那一行；8.3 的 MSBuild 命令是按 `D:\project\VxKex-NEXT\test\ansi-features-test.vcxproj`（只有 `Release|x64` 配置，输出到 `test\x64\Release\`）写出的，现有脚本里没有对应的行。这两条命令以及 `start` 备用命令**本轮评审都没有实际执行**，执行结果以实施时的真实输出为准。

若 Test 8 有失败：先核对步骤 1–6 是否逐字完成；**不要**为了让测试通过去改 `conansi.c` 或改测试期望值，把失败日志原样报告给用户。

#### 8.4 打包

离线测试全部通过后运行：

```bat
"D:\project\VxKex-NEXT\build-installer.cmd"
```

它会依次构建 x64 与 Win32 Release 并生成 `D:\project\VxKex-NEXT\KexSetup_Release.exe`，日志在 `D:\project\VxKex-NEXT\test\build-installer.log`。回读日志确认两个平台都构建成功，记录安装包的 SHA256。

**到此为止。** 运行安装包、替换 `C:\Program Files\VxKex` 下的 DLL 需要管理员权限，由用户执行；实施模型不要运行安装器，不要覆盖已安装的 DLL。

#### 8.5 收尾

- 更新 `D:\project\VxKex-NEXT\docx\当前工作.md`：记录实际构建与测试结果、安装包路径与 SHA256、待用户验收的项目。
- 只有用户要求时才提交；提交前检查差异，不提交 `x64\`、`test\*.log` 等生成物。

---

## 六、 验收（部署后由用户实测）

安装新包后，新开一个 VS Code 集成终端（正常加载 Profile 的 `pwsh`）：

1. `opencode auth login`：框线与图标应显示为 `┌ │ └ ◇ ■`，不再出现 `鈹?`、`鈼?`、`鈻?枲`。
2. 同一界面留意排版：框线与后面文字的间距、密码掩码 `■` 的宽度、光标位置是否可接受。936 下这些符号各占 2 格，与其他终端里的单宽效果可能略有差别，这一点只能靠实际观察判断。
3. 回归：运行 `opencode` 进入主界面，输入“继续”，确认没有夹杂 `s`；按 `/` 展开菜单再 `Esc`，确认仍然居中、无残影；问答菜单焦点行仍有高亮色。
4. 回归：`opencode --version`、`opencode --help` 正常退出。
5. 可选：在独立控制台（非 VS Code）重复第 1、3 项。

---

## 七、 回滚

- **不重装即可关闭新行为**：启动程序前设置环境变量 `VXKEX_NO_CONSOLE_UTF8=1`（PowerShell：`$env:VXKEX_NO_CONSOLE_UTF8 = '1'`），该进程即恢复为修改前的输出路径。变量只读取一次（进程声明 65001 之后第一次写控制台时），所以必须在启动程序之前设置。
- **完全回滚**：重新运行上一版安装包（AGENTS.md 第 10 项记录的 SHA256 `e173e24a9ecbef1f4bdd72d4ea1aaad37310bc173c9a6ef6ac1960d46b930dfa`，或步骤 0 确认过的带时间戳副本）。

---

## 八、 剩余风险与未验证项

1. **新代码尚未在 KxBase 内编译运行。** 第五节 1.1 的代码块已用 VC10 以 `/W4 /TC` 在仓库外编译，并在真实 936 控制台中通过了全部用例；但它在仓库外是配合桩函数运行的，没有和真实的 `conansi.c` 宽字符管线、`kxbase.def` 导出一起链接。步骤 1.2、1.3、2–6 的改动只做了等价逻辑验证，未在仓库内编译。
2. **`auth login` 子命令本身没有被直接观测。** 观测到的是 `opencode.exe --version` 启动时把代码页设为 65001，据此推断这是 Bun 运行时的启动行为、对所有子命令相同。如果验收时乱码依旧，首先核对这一点。
3. **启动后最多约 100 ms 的窗口。** Bun 设 65001 之后、守护改回 936 之前，真实代码页是 65001，这段时间的输出走原生路径，表现与现在相同。`auth login` 的首屏输出通常晚于这个窗口，但不保证。
4. **双宽符号的排版**（见第六节第 2 项）需要真实终端验收。
5. **单槽残留**：不完整序列只保存一份、按句柄匹配。多个线程同时向同一控制台写被拆开的 UTF-8 字符时可能互相打断，这与 `g_PendingEscape` 的现有约束相同。
6. **无效句柄的边角情形**：处于解码状态时，如果调用方向一个无效的控制台句柄只写入了不完整的 UTF-8 前缀（1–3 字节），函数会返回成功并把这几个字节留作残留，而不是像原生 `WriteConsoleA` 那样立即失败。正常程序不会走到这里。
7. **子进程**：意图只记录在调用 `SetConsoleOutputCP` 的进程内。子进程若自己不调用却输出 UTF-8，不在本次修复范围。
8. Win32（32 位）版本只会在步骤 8.4 中被编译，没有对应的离线测试。

---

## 附 A：不重编 DLL 时的临时脚本绕行方法（可选）

在安装新版之前，如果想让 `opencode auth login` 等非全屏子命令先不显示乱码，可以在 `C:\Users\liusj\Documents\PowerShell\VxKex.OpenCode.ps1` 的 `global:opencode` 函数中，对这些子命令临时清空 `TERM_PROGRAM`，使 `@clack/prompts` 退回 ASCII 边框（`T`、`|`、`*`）：

```powershell
    if ($args.Count -gt 0 -and $args[0] -in @('auth', 'models', 'mcp', 'agent', 'run', 'upgrade', 'uninstall')) {
        $prevTermProgram = $env:TERM_PROGRAM
        try {
            $env:TERM_PROGRAM = ''
            & $exe @args
            $global:LASTEXITCODE = $LASTEXITCODE
        } finally {
            $env:TERM_PROGRAM = $prevTermProgram
        }
        return
    }
```

这只是绕行：它不解决这些子命令输出中文或其他非 ASCII 文本时的乱码。该脚本改动本轮未实施、未测试。

---

## 附 B：2026-11-13 评审实测记录

环境：本机 Win7 SP1 x64，VC10 x64 编译器，独立隐藏控制台（初始输出代码页 936）。测试程序与日志在会话临时目录中，未放入仓库。

| 项目 | 结果 |
| :--- | :--- |
| `opencode.exe` 运行时 | 字符串 `Bun v1.4.4`；静态导入 `KERNEL32!SetConsoleOutputCP` 等 |
| `opencode.exe --version` 期间的代码页 | 启动约 47 ms 后输出/输入 936 → 65001，约 375 ms 退出时恢复 936，退出码 0 |
| 严格 UTF-8 解码拆分序列 | `61 62 E2 94` → 0（错误 1113）；`8C 61 62` → 0 |
| GBK 被误判为 UTF-8 | `C6 BD` → U+01BD；`D0 A1 C6 BD` → U+0421 U+01BD |
| 用户态 936 转换孤立字节 | `D6` → `?`；`D0 61` → U+884B |
| 原生 `WriteConsoleA` 跨调用前导字节 | `D6` 后接 `D0 61 62` → 屏幕为 `4E2D 0061 0062` |
| 936 下 `WriteConsoleA` 返回值 | GBK `中文ab` → 6（字节数） |
| 非严格 UTF-8 解码 | `41 FF 42` → `0041 FFFD 0042`；U+1F600 → `D83D DE00` |
| 大块写入 | `WriteConsoleW` 60000 WCHAR 失败（错误 8），30000 成功；`WriteConsoleA` 60000 字节成功；切片辅助函数 120000 字节成功 |
| 65001 + 点阵字体 | `WriteConsoleW` 写非 ASCII 失败（错误 31），所以规则排除“真实代码页为 65001” |
| 第五节 1.1 代码块（仓库外，配桩函数） | 判定规则四种状态、整块写入、1+1+1 字节拆分、UTF-8 中文双宽（光标 X = 5）、非法字节、大块写入：全部通过 |
| Test 8 对修订逻辑（仓库外，配桩函数） | 全部 `[PASS]`，failures = 0 |
| Test 8 对现有 `x64\Release\KxBase.dll`（旧代码） | 9 个 `[FAIL]`（钩子检查、8a–8f），8g 全部 `[PASS]`；8a 回读 `9239 003F ... 923C 56E3 67B2 923B`，与截图乱码一致 |

备注：仓库构建 `D:\project\VxKex-NEXT\x64\Release\KxBase.dll`（SHA256 `491F6E30…55A2`，2026-11-01 16:05）与已安装的 `C:\Program Files\VxKex\Kex64\KxBase.dll`（SHA256 `BC9E19AB…65B6`，2026-11-01 16:04）大小相同但哈希不同；上面的基线是对仓库构建测得的，未对已安装的 DLL 做任何操作。

---

## 附 C：2026-11-13 实施与测试记录

按第五节步骤 0–8.4 实施，源码改动与文档代码逐字一致（`git diff --stat`：`console.c` +290、`file.c` 1 行条件、`kxbasep.h` +7、`KxBase.h` +1、`kxbase.def` +1、`forwards.c` 1 行注释、`ansi-features-test.c` +185）。`conansi.c` 未改。

测试在新开的最小化控制台中运行（`start /wait`），真实退出码写入日志。

| 项目 | 结果 |
| :--- | :--- |
| 红色基线（只加 Test 8，旧 DLL `491F6E30…55A2`），初始 CP 437 | 9 个 FAIL，全部在 Test 8（钩子检查、8a–8f），8g 全部 PASS；退出码 1 |
| 红色基线，初始 CP 936 | 同上 9 个 + Test 2 第 324 行（936 下 `▒` 双宽，既有问题） |
| KxBase x64 构建（`kxbase.vcxproj`，`/W3 /WX`） | 退出码 0，无警告 |
| `dumpbin /exports` | `SetConsoleOutputCP = Ext_SetConsoleOutputCP`（不再转发）；`GetConsoleOutputCP`、`SetConsoleCP` 仍转发 kernel32 |
| 修复后，初始 CP 437 | `ALL TESTS PASSED SUCCESSFULLY! (failures = 0)`，退出码 0 |
| 修复后，初始 CP 936 | 只剩 Test 2 第 324 行（同基线）；Test 8 共 39 项全部 PASS，8a 回读 `250C 0020 Connect 25C7 25A0 25A0`，8d 光标 X = 5 |
| 回滚开关 `VXKEX_NO_CONSOLE_UTF8=1`，初始 CP 437 | Test 8 退回旧行为：8 个 FAIL（钩子检查通过，8a–8f 失败），8g 通过 |
| `build-installer.cmd`（x64 + Win32 全量构建 + 打包） | 退出码 0；ConPTY 回归 `RESULT failures=0` |
| 打包后最终 DLL 重跑测试 | 437：failures = 0；936：只剩 Test 2 第 324 行 |

产物 SHA256：

- `D:\project\VxKex-NEXT\KexSetup_Release.exe`、`D:\project\VxKex-NEXT\KexSetup_Release_1.2.3.2463_20261113_2210.exe`（5,037,083 字节）：`ED5C3F15493D181DC473554C8370968FCE6E824090886330EC5A2F5CD4DEEA9A`
- `D:\project\VxKex-NEXT\x64\Release\KxBase.dll`（164,352 字节）：`4B7B08674562E0C46DB11CF61A053240AC374D5A074E1037F1E3DDAA9EC24947`
- `D:\project\VxKex-NEXT\Release\KxBase.dll`（Win32，153,600 字节）：`14F22A0FB7FED0F7693C1787C4BF68A55271F026D97B426B76143D67BDAAEED3`

上一版安装包：`D:\project\VxKex-NEXT\KexSetup_Release_1.2.3.2463_20261101_1605.exe`。

说明：`build-installer.cmd` 第 4 步会改写已跟踪的 `docx\conpty-exports-20261017.txt`、`conpty-test-build-20261017.txt`、`conpty-test-run-20261017.txt`。运行结果已另存后用 `git restore` 恢复，未混入本次改动。

实施时未做（后续见附 D）：安装部署、`opencode auth login` 真实终端验收、Win32 离线测试。

源码、测试与文档提交为 `33fd5aa`（`fix: 应用声明 UTF-8 而控制台为 936 时按 UTF-8 解码窄字符输出`）。

---

## 附 D：2026-11-13 部署验收结果

用户以管理员身份安装 `KexSetup_Release.exe`（SHA256 `ED5C3F15493D181DC473554C8370968FCE6E824090886330EC5A2F5CD4DEEA9A`），新开 VS Code 集成终端实测，结论为“都正常”：

| 第六节验收项 | 结果 |
| :--- | :--- |
| `opencode auth login` 框线与图标 `┌ │ └ ◇ ■` 显示 | 正常，不再出现 `鈹?`、`鈼?`、`鈻?枲` |
| 框线间距、密码掩码 `■` 宽度与排版 | 可接受 |
| TUI 中文输入“继续”不夹杂 `s` | 正常，无回归 |
| 斜杠菜单 `/` 展开与 `Esc` 退出后居中、无残影 | 正常，无回归 |
| 问答菜单焦点行高亮 | 正常，无回归 |

据此，第八节第 1、2、4 项风险已由真实终端验收覆盖：真实 OpenCode 进程经 VxKex 导入重写后确实调用到 `Ext_SetConsoleOutputCP`，`auth login` 首屏输出走了新路径，双宽符号排版可接受。仍未覆盖的项：Win32（32 位）版本的离线测试与真实程序验证；多线程交错写入被拆开的 UTF-8 字符等边角情形（第八节第 5–7 项）。
