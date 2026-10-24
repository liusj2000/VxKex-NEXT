# pwsh 字符双写黑屏与 VS Code 终端滚动重行排查解决记录

**日期**：2026-10-24  
**目标环境**：Windows 7 专业版 SP1 x64 (Build 7601)  
**涉及模块**：`KxBase.dll` (ANSI/VT 引擎 `conansi.c`)、`PowerShell Profile`、`VS Code 集成终端 (winpty)`、`opencode2-node`

---

## 一、问题背景与现象汇总

用户反馈了以下与终端显示、中文输入及程序运行密切相关的异常现象：

### 现象 1：独立控制台 `pwsh` 中文字符双写、0x1F 报错与 OpenCode 启动黑屏退出
- 在独立命令行（conhost）中运行 PowerShell 7 (`pwsh`) 时，所有中文字符均出现连续双写现象：
  - `Microsoft Windows [版版本本 6.1.7601]`
  - `版版权权所所有有 (c) 2009 Microsoft Corporation。。保留留所所有有权权利利。。`
  - `尝尝试试对对 'FileSystem' 提提供供程程序序执执行行 InitializeDefaultDrives 操操作作`
- 控制台伴随输出红字错误：
  - `out-lineoutput: ... 发生 Win32 内部错误 "写入设备时发生错误。" 0x1F。`
- 在该环境下执行 `opencode2-node`，屏幕瞬间被清空，随后直接退回 `PS C:\Users\liusj>` 提示符，显示为纯黑屏状态，无法正常进入 OpenCode2 的交互式 TUI 界面。

### 现象 2：VS Code 集成终端鼠标向上滚动时出现历史状态行大量堆叠重行
- 在 VS Code 内置终端中运行 `opencode2-node` 交互提问或显示进度时，当用户使用鼠标滚轮向上滚动终端视口，上方历史区域出现了大量重复堆叠的状态栏文字：
  ```text
  Project version c 提交版本管理 • Current project s 项目状态检查 .: 检查项目
  Project version c 提交版本管理 • Current project s 项目状态检查 .: 检查
  Project vers 提交版本管理 • Current proj 项目状态检查 检查项目状态 +
  Project versi 提交版本管理 • Current proje 项目状态检查 检查项目状态 +
  ```

### 现象 3：OpenCode 中文输入错位重叠、夹杂英文字母（`继s续`）
- 在 VS Code 终端运行 `opencode2-node` 时，用户尝试在输入框中输入中文“继续”：
  - 屏幕上原本的默认占位提示符为 `Ask anything... "Fix a TODO in the codebase"`；
  - 输入“继续”后，汉字未能完整覆盖前面的提示词，而是在中间夹杂了残留的英文字母 `'s'`，显示为 **`继s续`**；
  - 汉字与后面的占位符 `anything...` 发生碰撞重叠，且光标陷在字符中间。

### 现象 4：独立控制台点阵字体对方块图形乱码为 `aa`、`aaaa` 与 `~`
- 在独立 `pwsh` 窗口运行 `opencode2-node` 时，顶部的 OpenCode Logo 阴影方块与边框线显示为大量的 `'aa'`、`'aaaa'`、`'~'` 字母和标点，且版本提示信息 `/update to install ...` 与 `te to install ...` 发生了重叠覆盖。

---

## 二、底层根因深度溯源

### 1. 字符双写与 0x1F 黑屏闪退根因
1. **历史候选 DLL 的双格注入缺陷导致字符双写**：
   在上一轮针对 UTF-8 汉字单格宽度的修复中，候选 `KxBase.dll` (`b19e1d7d...`) 在 `01-Extended DLLs/KxBase/conansi.c` 中的 `WriteConsoleUnicodeText` 试图通过 Win32 控制台底层的 `WriteConsoleOutputW` 将每个 BMP 宽字符（CJK 汉字）同时填充到两个相邻的 `CHAR_INFO` 单元格中，并分别赋予 `COMMON_LVB_LEADING_BYTE` 与 `COMMON_LVB_TRAILING_BYTE` 属性：
   ```c
   Cells[0].Char.UnicodeChar = Buffer[Total];
   Cells[0].Attributes = Attrib | COMMON_LVB_LEADING_BYTE;
   Cells[1].Char.UnicodeChar = Buffer[Total];
   Cells[1].Attributes = Attrib | COMMON_LVB_TRAILING_BYTE;
   WriteConsoleOutputW(ConsoleHandle, Cells, ...);
   ```
   **底层机制断裂**：Windows 7 原生 `conhost.exe` 的光栅化绘制器并不支持通过 `WriteConsoleOutputW` 写入的 `COMMON_LVB` 宽字符合并显示，而是直接将 `Cells[0]` 与 `Cells[1]` 视作两个独立的字符进行绘制。由于两个单元格都被填入了同一个字，导致屏幕上每一个汉字都被忠实地打印了两次（`版版本本`、`尝尝试试对对`）。
2. **Windows 7 conhost 原生 CP_UTF8 (65001) 的系统级缺陷（错误 31 / 0x1F）**：
   为了解决代码页问题，之前的 PowerShell Profile 中全局注入了 `& chcp 65001 >$null`。
   经实测验证：
   - 代码页 **CP936**：调用原生 `WriteConsoleW` 写入中文：`Result = 1, GetLastError() = 0`（成功）；
   - 代码页 **CP437**：调用原生 `WriteConsoleW` 写入中文：`Result = 1, GetLastError() = 0`（成功）；
   - 代码页 **CP65001**：调用原生 `WriteConsoleW` 写入中文：`Result = 0, GetLastError() = 31 (0x1F, ERROR_GEN_FAILURE)`！
   这是 Windows 7 原生 `conhost.exe` 的已知系统 Bug：在 65001 下处理 `WriteConsoleW` 遇到非 ASCII 宽字符时内核驱动会直接返回通用故障。PowerShell 抛出 `out-lineoutput ... 0x1F`，`opencode2-node` 在初始化 TTY 流时由于遇到 `0x1F` 致命错误无法注册标准流，执行清屏后直接退回命令行。

### 2. VS Code 终端滚动重行堆叠根因
1. **Inline TUI 的局部刷新机制**：`opencode2-node` 交互底部的动态状态指示器工作在主屏幕缓冲区（Primary Buffer）的单行刷新模式，使用 `\r\x1b[2K`（回车 + 清除当前行）高频刷新。
2. **窄列宽物理折行破坏回车语义**：VS Code 集成终端依赖 `winpty-agent.exe`。当终端面板宽度较窄（小于 120 列），状态行文本超长自动**折行至第 2 行**。根据标准终端规范，`\r` **只能将光标移回当前行（第 2 行）行首**，无法跨行返回第 1 行；随后的 `\x1b[2K` 也仅清除了第 2 行。
3. **滚动历史缓冲区泄漏**：第 1 行的内容不仅未被擦除，且随着新动画帧继续折行，上一帧未被擦除的第 1 行被作为历史不可变行推入了终端的历史滚动缓冲区（Scrollback Buffer）。每秒产生约 10 行残影帧，当用户鼠标滚轮向上滚动时，`xterm.js` 调出滚动缓冲区，显露出满屏重复堆叠的动画帧。

### 3. OpenCode 中文输入错位与夹杂英文字母（`继s续`）根因
1. **默认占位提示符分析**：
   输入框未输入时的原始占位文本是：
   ```text
   Ask anything... "Fix a TODO in the codebase"
   ```
   其前 4 个字符为：Col 0: `'A'`, Col 1: `'s'`, Col 2: `'k'`, Col 3: `' '`（空格）。
2. **Win7 conhost 在 65001 (UTF-8) 下的单列宽缺陷**：
   Windows 7 控制台在 `CP65001` 下**没有对中文字符做全角双宽适配**（该特性直到 Win10 1903 才加入控制台）。在 Win7 65001 下，`WriteConsoleW` 写入一个汉字 `继` 时，控制台光标**只向右移动了 1 列**（停留在 Col 1）！
3. **两端列宽模型不一致导致 `'s'` 残留**（本次复核实测，测量数据见第四节）：
   - OpenCode 前端（Node.js）按 Unicode 标准判定汉字 `继` 为双宽（占用 2 列，应当覆盖 Col 0 和 Col 1）；
   - 但 Win7 控制台在 CP65001 下的宽字符推进并不可靠：`WriteConsoleW` 直接失败返回 `31 (0x1F)`；改走 `WriteConsoleA` 写 UTF-8 字节时，原生 conhost 实测每个汉字只推进 **1 列**（原生 python 进程 `X=2`），在 VxKex 注入的 `pwsh` 进程内则按字节推进 **3 列**（`X=6`）；
   - 只要控制台实际推进列数与前端假定的 2 列不一致，前端按自身宽度模型重新定位后必有某一列未被覆盖。实测复现：CP65001 下前端在 0 列写 `继`、在 2 列写 `续`，Col 1 的 `'s'` 原样残留 → **`继s续`**；完全相同的写序列在 CP936 下得到正确的 `继续`（`X=2 → X=4`）；
   - 同时，汉字总推进宽度与前端预期不符时，后面的 `anything...` 不会被正确推移，汉字与占位符直接碰撞重叠，光标卡在文字中间。

### 4. 独立控制台字符方块变成 `aa`、`aaaa` 与 `~` 根因
- 独立控制台（`conhost.exe`）当前窗口使用的是系统默认的 **点阵字体（Raster Fonts）**。
- 点阵字体字符集极小，不包含 Unicode 扩展阴影方块与框线字符（`▀`、`▒`、`─`、`·`），系统底层自动将其降级替换为字母 `'a'`、`'aaa'` 和 `'~'`；并且点阵字体的字宽不均匀引起光标坐标失真，导致版本提示信息发生重叠。

### 5. 启动报错 `InitializeDefaultDrives` 发生时序与根因
- 用户在新建终端标签页时看到的：
  ```text
  PowerShell 7.6.7
  尝试对 'FileSystem' 提供程序执行 InitializeDefaultDrives 操作失败。
  ```
- **时序断裂**：该报错发生在 PowerShell 核心引擎（.NET CLR）初始化阶段，**早于任何 `$PROFILE` 脚本的加载**。
- **根本来源**：VirtualBox 虚拟机后台自动挂载了宿主机的 `tools` 共享文件夹为 `Y:` 盘，但宿主机上该物理路径已失效或不存在。PowerShell 启动时枚举所有系统逻辑驱动器，在查询 `Y:` 盘时操作系统抛出 `ERROR_INVALID_PARAMETER (87)`。

---

## 三、解决与修复措施

### 1. 源码修复：彻底移除 `KxBase` 错误双格输出
在 `01-Extended DLLs/KxBase/conansi.c` 中：
- 重构 `WriteConsoleUnicodeText`，彻底移除人为构造双格 `CHAR_INFO` 与 `WriteConsoleOutputW` 调用，直接透传原生 `WriteConsoleW`：
  ```c
  STATIC BOOL WriteConsoleUnicodeText(
      IN HANDLE ConsoleHandle,
      IN PCWSTR Buffer,
      IN ULONG CchToWrite,
      OUT PULONG CchWrittenOut OPTIONAL)
  {
      return WriteConsoleW(ConsoleHandle, Buffer, CchToWrite, CchWrittenOut, NULL);
  }
  ```
- 从而确保所有 Unicode 字符（无论 ASCII 还是 CJK）由操作系统控制台以原生字符宽度正常上屏，彻底消除 `版版本本` 等字符双写问题。
- 构建生成最新安装包 `D:\project\VxKex-NEXT\KexSetup_Release.exe`（SHA256: `8a1d2cade36fe3bc81f0b1381fe40b3b37a453f021a04325cf78073811958b5e`），系统目录 `C:\Program Files\VxKex\Kex64\KxBase.dll` 已更新部署完毕。

### 2. 环境修复：全面回滚 Profile 代码页至原生 CP936
- 在 Windows 7 下，**只有原生代码页 CP936 (GBK)** 才能让控制台正确将汉字判定为 **2 列宽**：
  实测在 CP936 下，写入“继续”严格推进 4 列（`Cursor X = 4`），完整覆盖开头的 `Ask `（4 字符），彻底杜绝中间夹杂字母 `'s'`（`继s续`）与占位符重叠。
- 净化所有 PowerShell 配置文件（移除了所有的强制 `chcp 65001`），仅保留安全的 PSReadLine 禁用逻辑：
  - `C:\Users\liusj\Documents\PowerShell\Microsoft.PowerShell_profile.ps1`
  - `C:\Users\liusj\Documents\PowerShell\profile.ps1`
  - `C:\Users\liusj\Documents\WindowsPowerShell\Microsoft.PowerShell_profile.ps1`
  - `C:\Users\liusj\Documents\WindowsPowerShell\profile.ps1`
  ```powershell
  # Disable PSReadLine to avoid VT rendering artifacts on Windows 7 / winpty
  if (Get-Module -Name PSReadLine) {
      Remove-Module PSReadLine -ErrorAction SilentlyContinue
  }
  ```

### 3. 独立控制台字体设置（消除 `aa`、`aaaa` 乱码）
- 在独立命令行窗口的**左上角小图标**点击 -> **“属性 (Properties)”** -> **“字体 (Font)”**；
- 将字体从 **“点阵字体”** 更改为 **Consolas**、**Lucida Console** 或 **新宋体**；
- 更改后，OpenCode 的阴影方块与边框线即刻恢复为原生实心方块和光滑边框，提示文本重叠完全消除。

### 4. 消除 `InitializeDefaultDrives` 启动报错
- 在 VirtualBox 窗口菜单栏中点击 **“设备” -> “共享文件夹” -> “共享文件夹设置...”**；
- 选中失效的 **`tools`** 共享文件夹，点击右侧的 **“-” (删除)** 按钮（或取消勾选“自动挂载”）；
- 确定保存后 `Y:` 盘即刻卸载，PowerShell 启动报错彻底消失。

### 5. VS Code 终端滚动重行应对方案
- **拉宽终端视口**：将 VS Code 底部集成终端窗口拉宽至 120 列以上，使单行动态状态指示行不发生物理折行，彻底杜绝历史帧漏入滚动区；
- **清理历史堆叠**：按快捷键 `Ctrl+K`（或输入 `Clear-Host`）一键清空 Scrollback 历史缓存。

---

## 四、本次复核验证（2026-10-24 实测记录）

### 1. 自动化回归测试

`test\x64\Release\ansi-features-test.exe`（显式 `LoadLibraryW` 加载 `D:\project\VxKex-NEXT\x64\Release\KxBase.dll` 后调用其导出）实际运行：

```text
WriteConsoleW CJK: written=4 (expected 4)
Distinct glyphs found: 4 (expected 4, no duplicates)
Cursor after CJK write: X=8 (full-width chars take 2 cells -> 8)
WriteConsoleA CP936: written=4 (expected 4)
Distinct glyphs found: 2 (expected 2, no duplicates)
Cursor after CP936 write: X=4 (full-width chars take 2 cells -> 4)
=== SUMMARY ===
ALL TESTS PASSED SUCCESSFULLY! (failures = 0)
TEST_EXIT=0
```

结论：当前 `KxBase` 的 `WriteConsoleW` 对 CJK 只写入一次（无重复字形），且按 2 列宽推进；`WriteConsoleA` + CP936 路径同样无重复并按 2 列宽推进。

### 2. 宽字符推进宽度实测

两次独立探测（脚本为临时验证用，留在 `C:\Users\liusj\AppData\Local\Temp\opencode\`，未纳入仓库）：

| 宿主进程 | 代码页 | 写入方式 | 写入内容 | 返回 | 光标推进 | 回读屏幕 |
| --- | --- | --- | --- | --- | --- | --- |
| 原生 `python.exe`（无 VxKex 注入） | 936 | `WriteConsoleW` | `继续` | 成功，`GetLastError()=0` | 2 列/汉字 → `X=4` | `继续anything...` |
| 原生 `python.exe` | 65001 | `WriteConsoleW` | `继续` | **失败，`31 (0x1F)`** | 无推进 | 原内容不变 |
| 原生 `python.exe` | 65001 | `WriteConsoleA`(UTF-8 字节) | `继续` | 成功 | 1 列/汉字 → `X=2` | `继续k anything...`（`k `、空格残留） |
| VxKex 注入的 `pwsh` | 936 | `WriteConsoleW`，前端按 0/2 列自行定位 | `继` + `续` | 成功 | `X=2` → `X=4` | `继续anything...`（正确） |
| VxKex 注入的 `pwsh` | 65001 | 同上 | `继` + `续` | **失败，`31 (0x1F)`** | 无推进 | 未写入，占位符原样保留 |
| VxKex 注入的 `pwsh` | 65001 | `WriteConsoleA`(UTF-8 字节) | `继续` | 成功 | 3 列/汉字 → `X=6` | 按字节各占一格，字形错乱 |

结论：CP936 是唯一在原生/VxKex 两条路径上都稳定给出「1 汉字 = 2 列」的代码页；CP65001 在不同进程、不同 API 下推进 1 列、3 列或直接失败，前端按 2 列计算必然错位，这就是真实界面出现 `继s续`、占位符碰撞与光标卡在字中的直接原因。

### 3. 交付与部署一致性核验

用 `01-Development Utilities\7zSfx\7zr.exe x` 从安装包解出文件后逐项比对：

| 项目 | 路径 | SHA256 |
| --- | --- | --- |
| 安装包 | `D:\project\VxKex-NEXT\KexSetup_Release.exe` | `8a1d2cade36fe3bc81f0b1381fe40b3b37a453f021a04325cf78073811958b5e` |
| 包内 x64 `KxBase.dll` = 已安装版本 | `C:\Program Files\VxKex\Kex64\KxBase.dll` | `fb5ede2194327022023dba29290d1bb7371a027573851f6a9d85c34a0513a94a`（160,768 字节，13:58:48） |
| 包内 x86 `KxBase.dll` = 已安装版本 | `C:\Program Files\VxKex\Kex32\KxBase.dll` | `88616ed7456a65bd2fc95e9317b6e974e94d6cb4c2a0045b1a08cfdc6c9cae11`（150,528 字节，13:59:39） |

打包完成后的第 4 步（`test\conpty-review-build.cmd`）又单独重编了一次 `KxBase`，使 `D:\project\VxKex-NEXT\x64\Release\KxBase.dll` 变为 `597e6ad9a0fd0a0825d7fc73f1a269d00e3db07b28ac4b73c59dd6ccfacf37f2`。与已安装版本逐字节比对，仅 6 个字节不同：PE 头部校验和等 3 字节，`.rdata` 内嵌构建时间戳/长度元数据 3 字节（两处相差 101 秒，与两次编译间隔一致），**`.text` 代码段完全一致**。

> 更正：上一轮文档把 `597e6ad9…` 记为交付的 x64 `KxBase.dll` 哈希，实际交付并部署的是 `fb5ede21…`；`597e6ad9…` 只是打包后的重编产物。

### 4. 未验证与边界

- `opencode2-node` 在 CP936 下的真实中文输入、上下键选择需在交互窗口内人工复验；本报告不宣称已自动验证 UI。
- VS Code 集成终端滚动重行只给出规避手段（拉宽 ≥ 120 列、`Ctrl+K`），未从 `xterm.js` / `winpty` 侧修复。
- 本文只覆盖 BMP 宽字符与 ANSI/VT 行为，不构成完整 Unicode、ConPTY 或备用屏幕缓冲区实现。
- Antigravity 120 秒启动超时与本问题无关，仍待重载日志确认。

---

## 五、部署与回滚指引

1. 部署（升级）：
   - 彻底关闭正在运行的 PowerShell、CMD、VS Code 及所有 `opencode2-node` 进程；
   - 以管理员权限运行 `D:\project\VxKex-NEXT\KexSetup_Release.exe` 完成覆盖升级；
   - 重开 `pwsh` 确认汉字单倍显示、无 `out-lineoutput ... 0x1F`；再运行 `opencode2-node` 确认输入不再夹杂字母、占位符无重叠。
2. 回滚：
   - 备份目录 `D:\project\VxKex-NEXT\backup-terminal-20261024\`，含原始安装包 `KexSetup_Previous.exe`、`KexSetup_Candidate.exe`、`System32\KexDll.dll`、`SysWOW64\KexDll.dll`、`VxKex\` 与 `Kex64\`/`Kex32\` 文件，哈希清单见 `sha256.csv`；
   - 需回滚时先停用相关进程，再用备份文件覆盖 `C:\Program Files\VxKex\` 下对应文件（需管理员），或直接运行备份的原始安装包。
3. 环境侧回滚：如需恢复 UTF-8 行为，只能在 Profile 中重新加入 `chcp 65001`，但会同时重现 `0x1F` 与 `继s续`，不建议。

