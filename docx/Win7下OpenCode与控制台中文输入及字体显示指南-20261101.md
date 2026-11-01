# Windows 7 下 OpenCode 与控制台中文输入及字体显示排查指南（2026-11-01）

## 1. 适用范围与背景

本指南面向在 **Windows 7 SP1 x64（配置 VxKex NEXT 兼容层）** 环境下使用现代 AI 终端工具（包括 OpenCode 2、Claude Code、Aider 等）的用户与开发者。

针对以下用户常遇到的困惑与异常现象提供完整机理解释与操作解决方案：
1. **现象一（中文无法输入）**：在独立控制台（PowerShell / CMD）中运行 OpenCode 或命令行时，按 `Shift` 或 `Ctrl+空格` 无法调出中文输入法，候选框不出现，只能输入英文字母；
2. **现象二（宋体/新宋体在菜单中消失）**：OpenCode 启动后，右键控制台“属性 -> 字体”，候选列表中只剩 `Consolas`、`Lucida Console` 和 `点阵字体`，启动前的“新宋体/宋体”不见了；
3. **现象三（终端表现不一致）**：在 **VS Code 内置终端** 中运行 OpenCode 时中文输入完全正常，但在独立的 PowerShell / CMD 窗口中却打不出中文。

---

## 2. 根本原因深度分析

### 2.1 为什么 VS Code 终端中文输入完全正常？
- **现代前端事件驱动**：VS Code 的集成终端底层由 Chromium / Electron 内核与 Web `xterm.js` 驱动，其输入链路直接对接 Windows 桌面级标准输入法事件（`CompositionEvent` / DOM `input`）；
- **无视 Win7 远古限制**：输入法候选框由系统现代桌面直接弹出，选词后的完整 UTF-8 字符串通过 IPC 管道直接传递给后端伪终端。因此，**VS Code 终端完全不受 Windows 7 `conhost.exe` 历史遗留缺陷的影响**，无论当前字体是 Consolas 还是代码页是 65001，中文输入体验均完美流畅。

---

### 2.2 为什么独立控制台（conhost.exe）无法输入中文？
Windows 7 系统原生的黑底白字控制台宿主（`conhost.exe`）拥有 15 年以上历史，在中文输入法调度上存在两项硬性底层机制：

#### ① 机制一：代码页对输入法（IME）的硬性屏蔽
- Windows 7 的控制台输入法驱动模块（`conime.exe` / `imm32.dll`）被硬编码设计为**仅对双字节东亚代码页（DBCS，如 CP936 简体中文、CP950 繁体中文）启用输入法浮窗捕获**；
- 当控制台处于 **`CP65001 (UTF-8)`** 时，Windows 7 的 `conhost.exe` 会彻底将输入法置于禁用/离线状态。此时击键直接作为 ASCII 扫描码送入输入缓冲区，因此用户无法切出输入法，只能打出英文。

#### ② 机制二：英文字体对输入法浮窗的硬性屏蔽
- 在 Windows 7 控制台中，如果当前生效的字体是西文字体（如 **`Consolas`**、`Lucida Console`）或点阵字体（Raster Fonts），控制台会判定当前不具备中文字形承载能力，进而直接拦截并关闭输入法上下文（IME Context）；
- **只有当控制台字体明确选中为东亚专用 TrueType 字体（即“新宋体” / NSimSun）时，Win7 的控制台才会激活拼音输入法浮窗**。

---

### 2.3 为什么运行 OpenCode 时“新宋体”会在菜单中消失？
- **OpenCode 自动切换 UTF-8**：OpenCode 包含复杂的现代化 TUI（Unicode 盲文加载条、彩色高亮、半角/全角混合艺术字），在进程启动之初会自动调用 Windows API：
  ```c
  SetConsoleOutputCP(65001); // 切换当前控制台为 UTF-8
  ```
- **注册表代码页绑定规则**：
  在 Windows 注册表 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Console\TrueTypeFont` 中，微软官方预设的字体绑定关系如下：
  ```text
  0   = Lucida Console (通用西文字体)
  00  = Consolas       (通用西文字体)
  936 = *新宋体        (仅当代码页为 936 时显示)
  932 = *ＭＳ ゴシック  (仅当代码页为 932 时显示)
  950 = *細明體        (仅当代码页为 950 时显示)
  ```
- 当 OpenCode 启动将代码页置为 `65001` 后，控制台属性对话框重新枚举字体，发现当前代码页不是 `936`，**按照系统规则自动将绑在 936 下的“新宋体”过滤隐藏**，因而字体面板中只剩通用的 Consolas 与 Lucida Console。

---

## 3. 推荐使用方案与操作指引

基于上述机制，针对在 Windows 7 下使用现代 AI 命令行工具，提供以下针对性方案：

### 方案 A（强烈推荐，体验最佳）：直接使用 VS Code 内置终端
- **优势**：
  1. 同时兼备 **Consolas 的高精度字符排版**（`OPENCODE` 顶部方块艺术字、线框完全不乱码、不裂开）；
  2. 享有 **无障碍的原生中文输入体验**（随时按 Shift 即可中英文无缝切换，候选词跟随顺畅）；
  3. 享受 VxKex NEXT 对 `KxNet`（过滤 `MSG_PUSH_IMMEDIATE` 解决 Bun 网络）与 `KxBase`（SGR 38/48 高亮量化、`QueryInterruptTimePrecise`）提供的全部内核级增强。
- **推荐操作**：在 VS Code 中直接打开项目目录，按快捷键 <kbd>Ctrl</kbd> + <kbd>`</kbd> 调出集成终端，直接运行 `opencode` 或 `claude` 即可。

---

### 方案 B（若必须在独立 PowerShell / CMD 窗口中输入中文）：

#### 步骤 1：确保注册表已开启控制台 IME 支持
在 PowerShell（管理员或当前用户均可）中执行以下命令，确保控制台允许输入法驻留：
```powershell
Set-ItemProperty -Path "HKCU:\Console" -Name "EnableIME" -Value 1 -Type DWord
```

#### 步骤 2：恢复代码页为 CP936
在终端中执行：
```powershell
chcp 936
```

#### 步骤 3：在控制台属性中切回“新宋体”
1. 点击控制台窗口左上角图标（或右键标题栏）-> 选择 **“属性 (Properties)”**；
2. 切换到 **“字体 (Font)”** 选项卡；
3. 在字体列表中选中 **“新宋体”**（字号推荐选 **14** 或 **16**）；
4. 点击“确定”保存；
5. 此时控制台已重新关联东亚中文字形，按下 `Shift` 或 `Ctrl+空格` 即可正常输入中文汉字。

> [!TIP]
> **字体与排版权衡说明**：
> - **选 Consolas**：OpenCode 的 ASCII 艺术字（`█ ▄ ▀`）排版平整紧凑，但 Win7 原生独立控制台下无法调用输入法；
> - **选新宋体**：可正常调用中文输入法，但部分半角/全角特殊阴影方块可能因字形宽度差异略有拉伸；
> - **如需“既要 Consolas 的完美等宽，又要流畅输入中文”**，最佳实践即为 **方案 A（VS Code 内置终端）**。

---

## 4. 常见问题速查 (FAQ)

### Q1: 退出 OpenCode 后，为什么我的终端代码页没有自动还原？
**A**: 某些基于 Bun 或 Node 的 CLI 工具退出时，未显式在退出钩子中调用还原代码页的 API。只需在命令行输入 `chcp 936` 即可手工秒级恢复。

### Q2: 为什么输入中文后，有时候命令行光标前会出现多余的英文字母（如“继s续”）？
**A**: 这正是因为在 `CP65001 (UTF-8)` 下，Win7 conhost 将汉字按单列宽推进，导致覆盖终端提示词（如 `Ask `）时跳过了第 1 列的 `s`。彻底还原为 `CP936`（双列宽推进）即可彻底消除字母夹杂。
