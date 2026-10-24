# pwsh 字符双写黑屏与 VS Code 终端滚动重行排查解决记录

**日期**：2026-10-24  
**目标环境**：Windows 7 专业版 SP1 x64 (Build 7601)  
**涉及模块**：`KxBase.dll` (ANSI/VT 引擎 `conansi.c`)、`Microsoft.PowerShell_profile.ps1`、`VS Code 集成终端 (winpty)`、`opencode2-node`

---

## 一、问题背景与现象

用户反馈了两个与终端显示及程序运行密切相关的异常现象：

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

---

## 二、问题 1（字符双写、0x1F 报错与黑屏闪退）根因分析

### 1. 历史候选 DLL 的双格注入缺陷导致字符双写
在上一轮针对 UTF-8 汉字单格宽度的修复中，候选 `KxBase.dll` (`b19e1d7d...`) 在 `01-Extended DLLs/KxBase/conansi.c` 中的 `WriteConsoleUnicodeText` 试图通过 Win32 控制台底层的 `WriteConsoleOutputW` 将每个 BMP 宽字符（CJK 汉字）同时填充到两个相邻的 `CHAR_INFO` 单元格中，并分别赋予 `COMMON_LVB_LEADING_BYTE` 与 `COMMON_LVB_TRAILING_BYTE` 属性：
```c
// 历史候选代码缺陷逻辑：
Cells[0].Char.UnicodeChar = Buffer[Total];
Cells[0].Attributes = Attrib | COMMON_LVB_LEADING_BYTE;
Cells[1].Char.UnicodeChar = Buffer[Total];
Cells[1].Attributes = Attrib | COMMON_LVB_TRAILING_BYTE;
WriteConsoleOutputW(ConsoleHandle, Cells, ...);
```
**底层机制断裂**：Windows 7 原生 `conhost.exe` 的光栅化绘制器并不支持通过 `WriteConsoleOutputW` 写入的 `COMMON_LVB` 宽字符合并显示，而是直接将 `Cells[0]` 与 `Cells[1]` 视作两个独立的字符进行绘制。由于两个单元格都被填入了同一个字，导致屏幕上每一个汉字都被忠实地打印了两次（`版版本本`、`尝尝试试对对`）。

### 2. Windows 7 conhost 原生 CP_UTF8 (65001) 的系统级缺陷（错误 31 / 0x1F）
为了解决代码页问题，之前的 PowerShell Profile 中全局注入了 `& chcp 65001 >$null`。
经编写探测代码独立实测验证：
- 在代码页 **CP936** 下，调用原生 `WriteConsoleW` 写入中文：`Result = 1, GetLastError() = 0`（完全成功）；
- 在代码页 **CP437** 下，调用原生 `WriteConsoleW` 写入中文：`Result = 1, GetLastError() = 0`（完全成功）；
- 在代码页 **CP65001** 下，调用原生 `WriteConsoleW` 写入中文：`Result = 0, GetLastError() = 31 (0x1F, ERROR_GEN_FAILURE)`！

这是 Windows 7 原生 `conhost.exe` 广为人知的系统级 Bug：在 65001 代码页下处理 `WriteConsoleW` 遇到非 ASCII 宽字符时内核驱动会直接返回通用故障。
- PowerShell 内部输出行捕获到该错误，抛出 `out-lineoutput: ... Win32 ... 0x1F`；
- `opencode2-node` 基于 Node.js 单文件打包架构，其控制台初始化阶段依赖 `libuv` / `@opentui/core` 设置 TTY 流。当写入控制台遇到 `0x1F` 致命错误时，底层流抛出未捕获异常，触发终端清理退出（清屏并返回），表现为控制台闪烁后变成纯黑屏并退回 shell。

---

## 三、问题 2（VS Code 终端滚动重行堆叠）根因分析

### 1. Inline TUI 的局部刷新机制
`opencode2-node` 交互过程中底部的动态状态指示器（如思考动画、操作提示行）工作在**主屏幕缓冲区（Primary Buffer）**的单行刷新模式，并没有像 Vim 或全屏 TUI 那样切换到**备用屏幕缓冲区（Alternate Screen Buffer `\x1b[?1049h`）**。
每次状态更新时，程序向控制台发出转义序列：
```text
\r\x1b[2K
```
其中 `\r`（CR 回车）表示将光标移动至当前行首，`\x1b[2K` 表示清除光标所在的整行内容，随后打印最新的一帧文本。

### 2. 窄列宽下的物理折行（Line Wrap）破坏了回车语义
Windows 7 下的 VS Code 集成终端必须依赖 `winpty-agent.exe` 作为中间层伪终端。
当 VS Code 终端面板宽度较窄（例如列宽小于 120 列），而状态行包含了中英文混排、项目路径及快捷键说明时，总字符显示宽度超过了终端物理列宽：
1. 状态行文本自动**折行（Wrap）**到了第 2 行；
2. 此时光标停留在第 2 行末尾；
3. 下一个动画帧到达，程序发送 `\r\x1b[2K`；
4. **关键致命点**：标准终端规范中，`\r` **只能将光标移回当前行（第 2 行）的行首**，根本无法越过换行边界回到第 1 行！随后的 `\x1b[2K` 也仅仅清除了第 2 行；
5. 第 1 行的内容不仅未被擦除，而且随着新一帧文本继续折行输出，上一帧的第 1 行被作为历史不可变行**推入了终端的历史滚动缓冲区（Scrollback Buffer）**。

### 3. 鼠标滚轮向上滚动调出滚动缓冲区
在正常停留不动时，用户只能看到最底部的视口，误以为一切正常；但后台每 100ms 刷新一次动画，每秒钟就会有大约 10 行历史折行快照被压入滚动缓冲区。
当用户使用鼠标滚轮向上滚动视口时，前端 `xterm.js` 向上翻滚展示历史滚动缓冲区，之前所有未被擦除、逐帧推入历史区的状态行切片全部显露出来，呈现出密密麻麻的重复行堆叠假象。

### 4. IFEO 版本伪装诱发的跨环境误判
VS Code 主进程 `Code.exe` 曾在系统 IFEO 中配置了 `KEX_WinVerSpoof = 4`（伪装为 Windows 10）。
子进程 `node.exe` 和 `opencode2-node.exe` 继承该属性后，通过 `os.release()` 误判当前为现代 Windows 10 系统（认为终端支持 ConPTY 动态 Reflow 与自适应行宽折叠），从而绕过了面向 Windows 7 等旧控制台的行宽截断与多行重绘保护。

---

## 四、解决与修复措施

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

### 2. 环境回滚：恢复系统原生 CP936 代码页
修改 `C:\Users\liusj\Documents\PowerShell\Microsoft.PowerShell_profile.ps1`，移除强制 `chcp 65001` 及 UTF-8 控制台注入，保留安全的回退逻辑：
```powershell
# Disable PSReadLine to avoid VT rendering artifacts on Windows 7 / winpty
if (Get-Module -Name PSReadLine) {
    Remove-Module PSReadLine -ErrorAction SilentlyContinue
}
```
恢复默认的 CP936 后，`WriteConsoleW` 不再触发 Windows 7 原生的 `0x1F` 崩溃，PowerShell 正常启动不再报内部错误，`opencode2-node` 控制台 I/O 初始化恢复正常，不再闪退黑屏。

### 3. VS Code 终端滚动重行应对方案
1. **拉宽终端视口**：将 VS Code 底部集成终端窗口拉宽（确保宽度保持在 120 列以上），使单行状态指示行完全容纳在单行物理列宽内不发生折行。在不折行情况下，`\r\x1b[2K` 始终在单行内重擦重绘，彻底隔绝历史帧被推入滚动缓冲区。
2. **清理滚动缓存**：对已产生历史堆叠的终端会话，按快捷键 `Ctrl+K`（或在命令面板执行 `Terminal: Clear`）清空 Scrollback 历史。
3. **重置 IFEO 伪装**：通过 `KexCfg.exe` 将 `Code.exe` 的 Windows 版本伪装设置为“系统默认/无伪装”，使 Node CLI 工具恢复旧版控制台的安全保护策略。

---

## 五、验证与全量构建

1. **测试套件验证**：
   运行 `test/x64/Release/ansi-features-test.exe`，5 项核心测试（光标显隐、DEC 特殊图形字符转义、数据块分片拼接、备用屏幕缓冲区、APC 静默吞除）全部通过：
   ```text
   === SUMMARY ===
   ALL TESTS PASSED SUCCESSFULLY! (failures = 0)
   ```
2. **全量构建与安装包生成**：
   使用 MSBuild 4.0 + Windows 7.1 SDK 串行免追踪编译通过全解决方案（x64 与 Win32 Release），生成最新安装包：
   - 安装包路径：`D:\project\VxKex-NEXT\KexSetup_Release.exe`
   - 安装包哈希 (SHA256)：`8a1d2cade36fe3bc81f0b1381fe40b3b37a453f021a04325cf78073811958b5e`
   - x64 `KxBase.dll` 哈希 (SHA256)：`597e6ad9a0fd0a0825d7fc73f1a269d00e3db07b28ac4b73c59dd6ccfacf37f2`
   - Win32 `KxBase.dll` 哈希 (SHA256)：`88616ed7456a65bd2fc95e9317b6e974e94d6cb4c2a0045b1a08cfdc6c9cae11`

---

## 六、部署与回滚指引

1. **部署建议**：
   - 彻底关闭正在运行的 PowerShell、CMD 以及 VS Code 窗口；
   - 以管理员权限运行 `D:\project\VxKex-NEXT\KexSetup_Release.exe` 完成升级覆盖；
   - 升级后重新启动 `pwsh`，验证中文字符不再双写，且 `opencode2-node` 正常进入交互界面。
2. **回滚路径**：
   - 如遇异常，可使用 `backup-terminal-20261024` 中的原始备份 DLL 进行手动还原，哈希清单见 `backup-terminal-20261024/sha256.csv`。
