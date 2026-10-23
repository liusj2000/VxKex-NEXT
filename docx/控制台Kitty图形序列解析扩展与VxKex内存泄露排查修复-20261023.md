# 控制台 Kitty 图形序列解析扩展与 VxKex 内存泄露排查修复（2026-10-23）

## 一、用户现场问题复盘与定位

用户在 Windows 7 SP1 x64（VxKex NEXT 兼容层环境）下运行 `opencode2-node` 时，在两种终端环境中分别观察到以下现象：

### 1. 独立终端（pwsh / cmd）环境（用户图 1、图 2）
- **现象 1（图 1）**：图标和框线未渲染为阴影块，显示为连续字母 `'aaaaaaa'`，项目符号显示为 `'~'`。
  - **根本原因**：新编译的 `KxBase.dll` 尚未部署。实测当前系统中 `C:\Program Files\VxKex\Kex64\KxBase.dll` 的 SHA256 哈希仍为补丁前的旧版本（`b2df85e4...`），DEC Line Drawing 特性尚未在运行环境生效。
- **现象 2（图 2）**：命令提示符处泄漏输出字符串 `q,t=d,f=24;AAAA`。
  - **根本原因**：现代交互式 TUI 应用在启动时会自动探测终端的现代图形能力，向终端发送了探测 Kitty 图形协议的标准序列：
    `\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\`
    `\x1b_` 属于 ANSI 标准中的 **APC（Application Program Command，应用程序命令）**。原 `conansi.c` 解析器仅处理了 CSI（`[`）和 OSC（`]`），未处理 `_`（APC），导致进入 `default` 分支只跳过了前 2 个字符（`\x1b_`），未消费后续参数，使 `q,t=d,f=24;AAAA` 作为普通文本残留输出到了控制台屏幕。

### 2. VS Code 内置终端环境（用户图 3、图 4、图 5）
- **现象 1（图 3）**：中文字符出现全链路机械双倍重复（“术术语语 'opencode2-node' 不不会会被被识识别别为为...”）。
  - **根本原因**：终端标签显示为 `PowerShell Extension`（即 Windows PowerShell 5.1）。系统默认 ExecutionPolicy 为 `Restricted`，启动时拦截了用户目录下的 `profile.ps1`，导致我们配置的 `chcp.com 65001` 与 `Remove-Module PSReadLine` 未能执行，终端回退至 GBK（CP936）。Win7 下 VS Code 的伪终端代理 `winpty-agent.exe` 在 CP936 下抓屏时未能识别 CJK 宽字符的双单元格占位，将每个汉字重复抓取两次发送给前端 xterm.js。
- **现象 2（图 4、图 5）**：Logo 顶部错位、菜单弹窗（“提交版本管理”）与底层内容（“Project version control...”）严重重叠残影、右侧边界出现 `♦` 符号。
  - **根本原因**：Windows 7 内核不存在 Win10 原生伪终端（ConPTY，Win10 1809 引入），VS Code 在 Win7 下必须降级依赖开源的 `winpty-agent.exe` 抓屏（Screen Scraping）。现代全屏交互式 TUI（Ink / React）高频重定位光标、频繁切换视口，winpty 的屏幕差量对比（diff）在 Windows 7 下无法及时准确同步备用屏幕缓冲区，导致旧界面残留在背景中，且宽字符对齐错位溢出占位符号。

---

## 二、源码修复与扩展

### 1. KxBase 扩展支持 APC / DCS / PM 字符串序列拦截 (`01-Extended DLLs\KxBase\conansi.c`)
- 在 `ProcessEscapeSequenceAorW` 中扩展字符串序列处理范围：
  ```c
  case ']': // OSC (Operating System Command)
  case '_': // APC (Application Program Command, e.g. Kitty graphics query)
  case 'P': // DCS (Device Control String)
  case '^': // PM  (Privacy Message)
  ```
- 遵循 ECMA-48 与 VT100 标准，将这些由 ST（`\x1b\`）或 `BEL`（`\x07`）结尾的控制字符串完整消费吞除；
- 将 `MAX_PENDING_ESCAPE_CCH` 跨数据块暂存缓冲区由 64 扩展至 256 字符，确保较长的探测参数跨 chunk 时也能无缝暂存与消费。

### 2. 全量排查并修复 VxKex 中的 3 处内存与资源泄漏

#### ① `KexDll/dllnotif.c`：DLL 映射全路径缓冲区堆内存泄漏（高频核心泄漏点）
- **位置**：`KexDll\dllnotif.c` 的 `Ext_NtMapViewOfSection`。
- **原因**：每次进程通过 `NtMapViewOfSection` 映射任何 DLL 时，都会调用 `GetDllFullName`，该函数通过 `SafeAlloc` 在进程堆中为 `PathBuffer` 分配了内存。但在回调 `KexDllNotificationCallback` 执行完毕后，函数直接退出，**从未调用 `SafeFree(FullDllName.Buffer)`**！
- **修复**：
  ```c
  if (FullDllName.Buffer) {
      SafeFree(FullDllName.Buffer);
  }
  ```

#### ② `01-Extended DLLs/KxCryp/bcrypt.c`：对称密钥对象重复分配导致永久内存泄漏
- **位置**：`01-Extended DLLs\KxCryp\bcrypt.c` 的 `Ext_BCryptGenerateSymmetricKey`。
- **原因**：外层先执行了 `PKEY_OBJECT NewKey = SafeAlloc(KEY_OBJECT, 1);`；而在分配 `NewSecret` 成功后，内层代码**又重复声明并分配了一次** `PKEY_OBJECT NewKey = SafeAlloc(KEY_OBJECT, 1);`，外层的指针被完全遮蔽且从未被释放！每次创建对称密钥必白白泄露一个堆块。
- **修复**：移除外层多余的重复分配，重构分配逻辑，失败时自动回收 `NewSecret`。

#### ③ `01-Extended DLLs/KxCom/uri.c`：COM 接口引用计数未释放导致的对象泄漏
- **位置**：`01-Extended DLLs\KxCom\uri.c` 的 `CUriRuntimeClass_Release`。
- **原因**：当 `IUriRuntimeClass` 引用计数归零时，析构代码仅调用了 `SafeFree(This)`，**从未调用底层 Win32 COM 对象 `This->Uri->lpVtbl->Release(This->Uri)`**。底层 Win32 COM `IUri` 对象直接被遗弃在内存中。
- **修复**：在析构时调用 `This->Uri->lpVtbl->Release(This->Uri)`。

---

## 三、自动化验证与全量打包

1. **自动化测试**：
   在 [`test\ansi-features-test.c`](file:///d:/project/VxKex-NEXT/test/ansi-features-test.c) 中新增 Test 5（APC Kitty graphics query suppression），验证针对 `\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\` 的拦截吞除效果。
   - 运行测试结果：**5 项测试全部 PASS，0 失败**。
2. **全工程构建与全量打包**：
   - 使用 MSBuild 4.0 + Windows 7 SDK 7.1 工具链编译通过了 `VxKex.sln`（Release x64 与 Win32）。
   - 打包生成最新安装器：[`D:\project\VxKex-NEXT\KexSetup_Release.exe`](file:///d:/project/VxKex-NEXT\KexSetup_Release.exe)。
   - 安装器 SHA256：`939e3b171f00127a47986d98f8cbba044b605fd725ad3aef00d22d49342cd400`。
   - `x64\Release\KxBase.dll` SHA256：`b988aaebb453b08c19ce5f361539b3e9554fb41beda80e72f298b6eb8857c3e7`。

---

## 四、部署与使用建议

1. **部署最新补丁**：
   - 方式 A（全量安装）：以管理员权限运行 `D:\project\VxKex-NEXT\KexSetup_Release.exe` 点击更新；
   - 方式 B（热更 64 位 DLL）：
     ```powershell
     Copy-Item "D:\project\VxKex-NEXT\x64\Release\KxBase.dll" "C:\Program Files\VxKex\Kex64\KxBase.dll" -Force
     ```
2. **解决 VS Code 终端“术术语语”中文重复**：
   以管理员身份在 PowerShell 中执行一次：
   ```powershell
   Set-ExecutionPolicy RemoteSigned -Scope CurrentUser
   ```
   允许 PowerShell 启动时正常加载全局 `profile.ps1`，激活 UTF-8 与无乱码环境。
3. **推荐使用姿态**：
   对于 `opencode2-node` 等高度依赖现代全屏虚拟终端的复杂 TUI 工具，**强烈建议直接在系统独立的命令提示符 / pwsh 窗口中运行**。配合新版 `KxBase.dll`，界面字符、阴影框线和弹出层均由 VxKex 原生内嵌引擎精准转译，能获得丝滑纯净的交互体验，彻底避开 Win7 下 VS Code `winpty` 抓屏的先天架构缺陷。
