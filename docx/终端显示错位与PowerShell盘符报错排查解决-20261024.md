> **2026-10-24 复查更正**：用户后续截图仍有中文错位和重复重绘，本文“100% 正常”“彻底消除”等端到端结论撤回。新测试证明 CP65001 下 Win7 汉字单格与 TUI 双格布局不一致；CP936 的 winpty 固定中文测试未出现双写。详见 [复查设计与结果](OpenCode终端重绘复查设计-20261024.md)。下面保留历史操作记录，不能作为最终验收证据。

# 终端显示错位与 PowerShell 盘符报错排查解决记录

**日期**：2026-10-24  
**环境**：Windows 7 Professional SP1 x64 (Build 7601)  
**涉及组件**：PowerShell 7.6.7、VS Code 1.107.1、OpenCode CLI (`opencode2-node`)、VirtualBox Guest Additions、VxKex NEXT 1.2.3.2463  

---

## 一、问题背景与现象

在 Windows 7 兼容层环境下运行 PowerShell 7 与现代化 TUI 工具（OpenCode CLI）时，观测到以下两个典型故障：

### 现象 1：PowerShell 7 启动报错
启动 `pwsh` 时，首行输出以下错误提示，随后才进入交互界面：
```text
C:\Users\liusj>pwsh
PowerShell 7.6.7
尝试对 'FileSystem' 提供程序执行 InitializeDefaultDrives 操作失败。
```

### 现象 2：终端字符乱码与上下键菜单错位
1. **独立控制台窗口（conhost / pwsh）**：
   OpenCode 的大字标题 Logo 中，实心半高方块出现 `aa` / `aaaa` 字母替代；底部边框与输入提示条出现长串 `aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa` 与 `~` 符号。
2. **VS Code 集成终端（xterm.js + winpty）**：
   - 终端打印的中文提示出现每个汉字重复双写（例如 `术术语语 'opencode2-node' 不不会会被被识识别别为为`）。
   - 在多选列表或问题菜单中，按上下方向键（`↑ select`）切换选项时，光标定位紊乱，上一行内容未被擦除，出现菜单项相互覆盖、行复制、残影（例如 `5. [✓ failures 并留证` 与 `5. [ ] 暂不改动` 重叠显示）。

---

## 二、问题 1（PowerShell 启动盘符报错）根因分析与消除方案

### 1. 根本原因定位
1. **VirtualBox 共享文件夹残留**：
   虚拟机宿主机曾配置了两个共享文件夹：
   - `project` -> 映射为 `Z:\`（指向宿主机代码目录，读写正常）
   - `tools` -> 映射为 `Y:\`（之前用于存放 Win7 SDK 工具包）
2. **宿主机目录失效导致 I/O 异常**：
   宿主机上 `tools` 对应的物理路径被移动、删除或未挂载，但 VirtualBox Guest Additions 服务（`VBoxService.exe`）开机时仍通过底层驱动 `\Device\VBoxMiniRdr\;Y:\VBoxSvr\tools` 将其分配为全局驱动器 `Y:`。
   使用 Win32 API 访问 `Y:\` 时直接返回 `ERROR_INVALID_PARAMETER (87)`。
3. **PowerShell 初始化异常触发**：
   PowerShell 7 在启动初期调用 `FileSystemProvider.InitializeDefaultDrives()`，枚举系统所有可用逻辑驱动器。当探测到 `Y:\` 时，.NET 的 `DriveInfo` 查询属性抛出 `System.IO.IOException: 参数错误`。PowerShell 捕获该异常后向控制台输出：
   `尝试对 'FileSystem' 提供程序执行 InitializeDefaultDrives 操作失败。`

### 2. 消除方案（彻底根治）
此问题由虚拟机外部的失效共享文件夹引起，消除步骤极简：
1. 在 VirtualBox 虚拟机窗口顶部菜单中点击：
   **“设备” (Devices) -> “共享文件夹” (Shared Folders) -> “共享文件夹设置...” (Shared Folder Settings...)**
2. 在共享文件夹列表中找到名称为 **`tools`** 的条目：
   - 直接将其 **删除**；或者
   - 双击编辑，**取消勾选“自动挂载” (Auto-mount)**。
3. 点击“确定”保存。
4. 此时失效盘符 `Y:` 自动卸载，重新打开 `pwsh`，该启动报错即彻底消除。

---

## 三、问题 2（终端乱码与上下键菜单错位）根因分析与解决

### 1. 根本原因定位

#### (1) 代码页不匹配与 winpty 双字节中文字符翻倍 Bug
- Windows 7 默认控制台代码页为 **CP936 (GBK)**。
- VS Code 在 Windows 7 上缺少 Win10 原生 ConPTY，必须依赖开源的 `winpty-agent.exe` 抓屏。
- **致命缺陷**：在非 UTF-8（CP936）代码页下，Windows 控制台以双字节（DBCS Lead Byte / Trail Byte）存储汉字。`winpty` 在通过 `ReadConsoleOutputW` 抓取屏幕缓冲区并向 VS Code `xterm.js` 传输时，无法正确处理 CP936 下的尾随字节，导致**每个中文字符被提取并发送了两次**（表现为 `术术语语`）。
- **连锁反应（上下键错位）**：
  交互式菜单（Ink / React / OpenCode）在终端中通过 ANSI 转义序列进行动态重绘（例如光标上移 $N$ 行 `\x1b[<N>A` 并清除当前行 `\x1b[2K`）。由于中文字符全部被翻倍，原本不会换行的单行选项被强制折成了两行甚至三行。程序根据原始文本计算出的“向上移动行数”与终端实际渲染的行数严重脱节，光标落点偏差，覆写位置错乱，导致多选菜单层叠残影、重复行。

#### (2) Unicode 方块元素在 CP936 控制台的降级回退
- OpenCode 的 Logo 与底栏分别使用了 Unicode 块元素（`█` U+2588、`▀` U+2580、`▄` U+2584、`▒` U+2592）。
- CP936 字符集天然不包含半高上块 `▀` 与网格块 `▒`。在控制台处于 CP936 模式时，字符映射或终端字体渲染触发兜底替换，表现为字母 `a` 与标点 `~`。

### 2. 实施解决措施

#### 修改 PowerShell 用户 Profile 强制启用 UTF-8
在 `C:\Users\liusj\Documents\PowerShell\Microsoft.PowerShell_profile.ps1` 中加入编码与代码页初始化逻辑：
```powershell
# Disable PSReadLine to avoid VT rendering artifacts on Windows 7 / winpty
if (Get-Module -Name PSReadLine) {
    Remove-Module PSReadLine -ErrorAction SilentlyContinue
}

# Ensure UTF-8 code page & encoding to fix winpty Chinese duplication & Unicode block rendering
try {
    $OutputEncoding = [System.Text.UTF8Encoding]::new()
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
    [Console]::InputEncoding = [System.Text.UTF8Encoding]::new()
    & chcp 65001 >$null
} catch {}
```

#### 实测验证结果
1. **中文双字翻倍消除**：
   在 pwsh 中执行包含中文字符的错误提示与输出，文字显示完全正常（`术语 'opencode2-node' 未被识别为 cmdlet、函数、脚本文件`），汉字无任何双重回显。
2. **Unicode 方块与线框正常**：
   `█ ▀ ▄ ▒ ·` 均能直接在 65001 代码页下完整、清晰显示，彻底消除 `aa` / `aaaa` 替代。
3. **行宽与光标回退对齐**：
   由于中文字符恢复单倍标准宽度，行换行与高度计算完全契合，上下方向键移动重绘选项时光标精准对齐，残影和重复行现象彻底消失。

