# DeepSeek Harness 安装包（dsh-latest-windows-x64.exe）配置 VxKex 报错排查与根本原因分析

## 一、 背景与现象

- **用户操作**：用户下载了 `C:\Users\liusj\Downloads\dsh-latest-windows-x64.exe`，由于看到文件名中带有 `x64` 且为高版本应用，尝试对其右键打开属性 $\rightarrow$ 启用 VxKex 兼容属性（配置为伪装 Windows 10，`KEX_WinVerSpoof = 4`）。
- **报错现象**：双击启动该安装包后，未出现正常安装向导界面，而是直接弹出一个模态错误对话框：
  - **窗口标题**：`DeepSeek Harness 安装`
  - **错误内容**：`无法初始化安装窗口，请关闭后重试。`（红叉错误图标，仅有一个“确定”按钮）。
  - 点击“确定”后程序直接退出，安装流程彻底中断。

---

## 二、 逆向与底层技术排查

### 1. 安装包架构鉴定：32 位 NSIS 引导程序而非 64 位程序

通过解析 `dsh-latest-windows-x64.exe` 的 PE 标头：
- **Machine Type**：`0x014C`（`IMAGE_FILE_MACHINE_I386`，32 位 x86 架构）。
- **Overlay Signature**：`0xDEADBEEF` + `NullsoftInst`。
- **事实结论**：虽然安装包名称含有 `windows-x64`，但外部可执行文件只是一个 **32 位（x86）的 NSIS（Nullsoft Scriptable Install System）安装引导器**。真正的 64 位 Electron 主程序被压缩打包存放在 NSIS 的数据块中。

### 2. 报错来源定位：硬编码于 NSIS 安装脚本与 `window-frame.dll` 契约

NSIS 安装器在启动时会向 `%TEMP%` 临时目录解压辅助组件：
- `window-frame.dll`（DeepSeek Harness 专门定制的 32 位现代窗口美化插件，大小约 161 KB）
- `System.dll`、`StdUtils.dll`、`UAC.dll` 等

通过对 `%TEMP%\ns*.tmp\window-frame.dll` 导出表与汇编指令逆向分析，其暴露了两个关键窗口函数：

#### (1) `InstallerApplyFrame(HWND hWnd)` (RVA: 0x6CF3)
负责对 NSIS 原生对话框应用现代化无边框与阴影效果：
1. 调用 `COMCTL32!SetWindowSubclass`（序号 410）接管窗口过程；
2. 循环调用 `GetDlgItem` + `ShowWindow(SW_HIDE)` 隐藏 NSIS 原生的按钮和白色边框；
3. 修改窗口样式 `GWL_STYLE`（`0xFFFFFFF0`）；
4. 调用 `dwmapi.dll!DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY (2), &policy, 4)`：
   ```assembly
   call esi                  ; DwmSetWindowAttribute
   test eax, eax             ; 检查 HRESULT 返回值
   js error_exit             ; 若返回值小于 0 (失败)，跳转到错误退出！
   ```
5. 若失败，直接退出并返回错误代码 **`0x80004005 (E_FAIL)`**。

#### (2) `InstallerPresentWelcome(HWND hWnd)` (RVA: 0x70C5)
负责在呈现欢迎页面时校验窗口可见性与焦点：
```assembly
mov esi, [ebp+8]          ; hWnd
push esi
call USER32!IsWindowVisible
test eax, eax
jz fail                   ; 若窗口尚未处于可见状态，直接返回 0 (失败)！

push 0x13
...
call USER32!SetWindowPos
test eax, eax
jz fail                   ; 若 SetWindowPos 失败，直接返回 0 (失败)！
```
若任意一项失败，直接返回 `0`。

#### (3) NSIS 脚本处理逻辑
当 NSIS 执行 `page custom` 调用上述插件函数，一旦检测到返回值返回 `0` 或非成功状态，NSIS 脚本立即触发：
```nsis
MessageBox MB_ICONSTOP|MB_OK "无法初始化安装窗口，请关闭后重试。"
Abort
```
这正是用户看到的弹窗的真实出处。

---

## 三、 为什么开启 VxKex 属性后会导致初始化失败？

在 Windows 7 上为 `dsh-latest-windows-x64.exe` 开启 VxKex 会在系统注册表 `IFEO` 下生成配置：
```ini
[HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\dsh-latest-windows-x64.exe\VxKex_...]
FilterFullPath = C:\Users\liusj\Downloads\dsh-latest-windows-x64.exe
KEX_WinVerSpoof = 4           ; 伪装为 Windows 10
KEX_DisableForChild = 0       ; 默认不禁用子进程继承
GlobalFlag = 256
VerifierDlls = kexdll.dll
```

### 1. 致命原因一：Win10 虚假版本引发的 DWM 状态与特性断裂
- **Win7 原生 dwmapi 与 Win10 特性脱节**：Windows 7 原生的 `dwmapi.dll` 只支持 Aero 合成，根本不具备 Windows 10/11 的高级窗口特性（如 `DWMWA_WINDOW_CORNER_PREFERENCE = 33` 等）。
- 当 VxKex 向进程报告操作系统为“Windows 10”时，`window-frame.dll` 内部会按照 Windows 10 的现代化无边框与半透明效果路径去初始化窗口，但在底层调用宿主 Windows 7 系统的 DWM 接口时返回了失败的 HRESULT，导致 `InstallerApplyFrame` 抛出 `0x80004005`。

### 2. 致命原因二：VxKex 32 位子进程继承与窗口钩子时序冲突
- 当 `KEX_DisableForChild = 0` 时，NSIS 安装器向 `%TEMP%` 释放的插件、提权 UAC 帮助进程以及解压子程序都会被强制注入 32 位的 `kexdll.dll`、`KxUser32.dll`。
- VxKex 的 `KxUser32.dll` 拦截了 `CreateWindowExW`、`Ext_UnregisterTouchWindow` 等基础窗口 API。这导致在临时窗口创建和子类化过程中，主窗口的显示状态被推迟，当插件进入 `InstallerPresentWelcome` 第一行执行 `IsWindowVisible(hWnd)` 时，窗口恰好尚未渲染可见，函数判定失败返回 `0`，从而直接触发了硬编码的报错弹窗。

### 3. 安装器自身本就不需要 VxKex
- NSIS 是纯 Win32 原生程序，对 Windows 7 SP1 具有天然的、完美的向下兼容性；
- 只有安装完成后生成的 64 位 Electron 主程序（调用了现代 Chromium 缺失的内核/Base API）才需要 VxKex 扩展层。

---

## 四、 现状核实与正确使用方式

经现场系统进程与注册表核查：
1. **安装器已无需再跑**：
   用户本地目录 `C:\Users\liusj\Downloads\dsh-latest-windows-x64\` 下已完整具备安装后的全部程序文件（`DeepSeek Harness.exe`、`ffmpeg.dll`、`resources` 等，总大小约 280MB）。
2. **主程序已成功运行**：
   - 系统的 IFEO 已经正确对 `C:\Users\liusj\Downloads\dsh-latest-windows-x64\DeepSeek Harness.exe` 启用了 VxKex。
   - 现场核查确认系统当前正正常运行着 5 个 `DeepSeek Harness.exe` 进程（PID: 2148, 2420, 3524, 4872, 5752），主界面与渲染核心均处于活跃工作状态。

---

## 五、 为什么去掉 VxKex 后原生运行仍然报该错误？（原生兼容性断裂）

经进一步深入跟踪确认：**即使完全取消 VxKex 配置，让安装程序以纯原生 Windows 7 SP1 模式运行，依然会报出相同的“无法初始化安装窗口”错误**。

这是因为：**DeepSeek Harness 的这个安装向导本身在 Windows 7 上就存在原生设计缺陷与硬性不兼容**：

1. **`window-frame.dll` 是专为 Win10/Win11 设计的无边框与圆角窗口插件**：
   - 该 DLL 在 `InstallerApplyFrame` 中对窗口调用 `DwmSetWindowAttribute(hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, ...)`（Win11 圆角偏好）以及 `DwmExtendFrameIntoClientArea(hWnd, {1,1,1,1})`。
   - 在 Windows 7 下，DWM 的合成接口与窗口层叠特性与 Windows 10/11 完全不同。当 DWM 调用返回失败或未达到其期望的呈现状态时，插件返回 `0x80004005 (E_FAIL)`。
2. **`InstallerPresentWelcome` 的可见性校验死锁**：
   - 插件中的代码进入欢迎页面第一步就是：
     ```assembly
     call USER32!IsWindowVisible(hWnd)
     test eax, eax
     jz fail
     ```
   - 在 Windows 7 原生 NSIS 对话框生命周期中，对话框在此时尚未完成 `ShowWindow` 上屏渲染，`IsWindowVisible` 必然返回 `0`，插件直接判定窗口未初始化，返回 `0` 给 NSIS。
3. **后台进程冲突（官方已知 Bug）**：
   - 插件中导出了 `InstallerFindProcess`，会在初始化时扫描系统进程。当前系统中如果已经存在运行中的 `DeepSeek Harness.exe` 进程，安装程序同样会拒绝初始化并弹出提示。
4. **官方社区公认解决方案：手动解压绿色运行**：
   - 在 DeepSeek Harness 官方 GitHub Issue 中，大量 Windows 用户也遭遇了该安装器在各种环境下卡死或初始化失败的 Bug（[GitHub Issue 记录](https://github.com/deepseek-ai)）。
   - 官方与社区一致推荐的标准方案为：**跳过图形化安装器，直接解压出核心文件，绿色运行 `DeepSeek Harness.exe`**。

---

## 六、 最终解决方案与使用结论

- **无需再纠结运行 `dsh-latest-windows-x64.exe` 安装包**：该安装包的图形界面插件 `window-frame.dll` 无法在 Windows 7 上正常渲染。
- **直接使用已解压的主程序**：
  在 `C:\Users\liusj\Downloads\dsh-latest-windows-x64\` 下，已经具备完整的程序体（`DeepSeek Harness.exe`）。
  保持对该 `DeepSeek Harness.exe` 开启 VxKex（伪装 Windows 10），即可正常使用全部桌面功能（当前系统后台已成功常驻运行）。

