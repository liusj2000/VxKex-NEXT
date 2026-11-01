# VS Code 同步失败与 Git 凭据卡死排查解决指南 (2026-11-01)

## 1. 现象描述

在 Windows 7 SP1 下使用 VS Code 点击左下角状态栏的 **“同步变更 (Sync Changes)”** 或进行 `git push` 时，出现以下现象：
1. VS Code 状态栏同步转圈图标长时间一直旋转（无任何反应或弹窗）；
2. 持续卡死长达约 **8 分钟（467 秒）**；
3. 最终同步失败，后台报错中断或无声退出，本地提交无法推送到 GitHub 远程仓库。

---

## 2. 根因深度剖析

通过查阅 VS Code 内部日志文件（位于 `D:\VSCode-win32-x64-1.107.1\data\user-data\logs\...\window1\exthost\vscode.git\Git.log`），捕捉到了底层真实的错误调用栈：

```log
2026-11-01 17:49:06.319 [info] > git push origin main:main [467012ms]
2026-11-01 17:49:06.319 [info] d:\VSCode-win32-x64-1.107.1\resources\app\extensions\git\dist\askpass.sh: fork: retry: Resource temporarily unavailable
d:\VSCode-win32-x64-1.107.1\resources\app\extensions\git\dist\askpass.sh: fork: retry: Resource temporarily unavailable
d:\VSCode-win32-x64-1.107.1\resources\app\extensions\git\dist\askpass.sh: fork: retry: Resource temporarily unavailable
d:\VSCode-win32-x64-1.107.1\resources\app\extensions\git\dist\askpass.sh: fork: retry: Resource temporarily unavailable
d:\VSCode-win32-x64-1.107.1\resources\app\extensions\git\dist\askpass.sh: fork: Resource temporarily unavailable
error: unable to read askpass response from 'd:\VSCode-win32-x64-1.107.1\resources\app\extensions\git\dist\askpass.sh'
bash: line 1: /dev/tty: No such device or address
error: failed to execute prompt script (exit code 1)
fatal: could not read Username for 'https://github.com': No such file or directory
```

### 核心因果链条：
1. **Pull 成功 vs Push 需认证**：
   - 远程仓库是公开仓库，拉取代码（`git pull`）为只读操作，匿名即可完成（日志显示耗时仅 2.4 秒）。
   - VS Code 的“同步（Sync）”由两步组成：`git pull` 拉取最新变更，紧接着执行 `git push` 推送本地提交。
   - `git push` 必须向 GitHub 进行身份验证与写权限核验。

2. **缺少全局凭据管理器（Credential Helper）**：
   - 检查本地及全局 Git 配置（`git config --list --show-origin`），发现原环境中未配置任何 `credential.helper`。
   - Git 在执行推送时找不到本地已保存的密码/Token，不得不调用 VS Code 注入的环境变量 `GIT_ASKPASS` 寻求弹窗或外部交互。

3. **VS Code `askpass.sh` 在 Win7 下触发 MSYS2 `fork()` 严重缺陷**：
   - VS Code 将 `GIT_ASKPASS` 指向了扩展自带的 Shell 脚本 `askpass.sh`。
   - 该脚本内部执行了命令替换 `VSCODE_GIT_ASKPASS_PIPE=\`mktemp\``。
   - 在 Windows 7 环境下，Git for Windows 内置的 MSYS2 bash 其 `fork()` POSIX 仿真在非纯净环境（存在系统级兼容层、动态注入模块或内存地址空间布局差异）下，无法在子进程中精准复制父进程内存映射，从而直接抛出 `Resource temporarily unavailable (EAGAIN)`。
   - MSYS2 内置了指数重试退避机制，导致该子进程调用足足卡死死等了 **467 秒（7.8 分钟）**，最后超时退出，整个同步流程随之崩溃。

---

## 3. 彻底解决方案

要彻底解决此问题，核心原则是**绕开 VS Code 脆弱的 `askpass.sh` 机制，改用 Windows 原生支持的高效凭据或密钥认证**。

### 方案 A：启用 Windows 原生凭据管理器（推荐，最简单）

Windows 自带原生的“凭据管理器（Windows Credential Manager）”，Git for Windows 附带了原生编译的 `git-credential-wincred.exe`，完全走 Win32 原生 API（`CredReadW`/`CredWriteW`），零依赖、绝不触发 MSYS2 fork，毫秒级响应。

#### 步骤 1：全局开启 wincred（已为您配置完成）
```powershell
git config --global credential.helper wincred
```

#### 步骤 2：录入 GitHub 凭据（Token）
由于 GitHub 自 2021 年起已停用账号密码推送，必须使用 **Personal Access Token (PAT)**：
1. 打开浏览器登录 GitHub，进入 [Personal Access Tokens (classic)](https://github.com/settings/tokens)。
2. 点击 **Generate new token (classic)**，填写 Note（如 `Win7-VSCode`），勾选 **`repo`** 权限（完整仓库控制权限），点击生成。
3. 复制生成的 Token（形如 `ghp_xxxxxxxxxxxxxxxxxxxx`）。
4. 在 CMD 或 PowerShell 中使用 Windows 原生 `cmdkey` 命令将凭据写入系统凭据库：
   ```cmd
   cmdkey /generic:git:https://github.com /user:liusj2000 /pass:ghp_你的Token字符串
   ```

> **效果**：录入后，无论是 VS Code 界面点击“同步变更”，还是在终端执行 `git push`，Git 都会直接调用 Windows 原生 API 瞬间完成身份鉴权，不再调用任何 `askpass.sh`，彻底根除转圈卡死。

---

### 方案 B：使用 SSH 密钥认证（免密码、极速）

如果您更习惯使用 SSH 密钥协议：

#### 步骤 1：生成本地密钥对
在 PowerShell 7 中运行：
```powershell
ssh-keygen -t ed25519 -C "liusj2000@users.noreply.github.com"
```
（一路按回车保持默认路径即可）。

#### 步骤 2：添加公钥到 GitHub
1. 输出公钥内容并复制：
   ```powershell
   Get-Content ~\.ssh\id_ed25519.pub
   ```
2. 登录 GitHub 打开 [SSH and GPG keys](https://github.com/settings/keys)，点击 **New SSH key**，粘贴公钥并保存。

#### 步骤 3：切换当前仓库远程地址为 SSH
```powershell
git remote set-url origin git@github.com:liusj2000/VxKex-NEXT.git
```

> **效果**：切换为 SSH 后，推送走 OpenSSH 协议通道，无需任何 HTTP 凭据与 Askpass 流程，同步即点即成。

