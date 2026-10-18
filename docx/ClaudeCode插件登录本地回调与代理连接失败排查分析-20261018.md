# Claude Code 插件登录本地回调与代理连接失败深度排查分析（2026-10-18）

## 一、现象与问题描述

在 Windows 7 SP1 x64（VxKex NEXT 1.2.3.2463）环境下，使用 VS Code 便携版（`D:\VSCode-win32-x64-1.107.1`）中的官方插件 **Claude Code**（`anthropic.claude-code-2.1.301-win32-x64`）进行身份认证时，用户在登录过程中连续遇到浏览器本地回调地址连接失败报错：

1. **第一次报错（端口 54495）**：
   ```text
   http://localhost:54495/callback?code=<REDACTED_OAUTH_CODE>&state=<REDACTED_OAUTH_STATE>
   ```
   浏览器显示：`无法访问此网站`、`ERR_CONNECTION_RESET`（错误代码 10054）。

2. **第二次报错（端口 49190）**：
   在针对性修复系统代理白名单后，重新发起登录，浏览器在授权通过后跳转至：
   ```text
   http://localhost:49190/callback?code=<REDACTED_OAUTH_CODE>&state=<REDACTED_OAUTH_STATE>
   ```
   浏览器依然报错，无法完成登录流程。

表面上看两次均表现为“浏览器无法打开 `http://localhost:<port>/callback`”，但经深入网络抓包与源码日志分析，**两次失败处于完全不同的故障层级**。

---

## 二、前后两次失败的排查与证据链对比

### 1. 阶段一故障：本地回调代理死循环陷阱（端口 54495）

- **现象机理**：
  - VS Code 插件在当前 Win7 本机启动临时 HTTP 端口（54495）等待接收授权回调；
  - Windows 系统配置了局域网代理服务器（`192.168.x.x:10809`），但注册表中的 `ProxyOverride` 默认仅为 `<local>`；
  - 现代 Chromium 内核浏览器对于带有端口号的主机名 `localhost:54495`，无法被旧式 `<local>` 规则豁免，因此浏览器将该 HTTP 请求**误转发至局域网远端代理机器 `192.168.x.x`**；
  - 远端代理服务器自身并未监听 54495 端口，立即强行切断连接并返回 TCP RST（`WinError 10054 ConnectionReset`）。
- **解决与验证**：
  在注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings` 中将 `ProxyOverride` 更新为：
  ```text
  ProxyOverride = localhost;127.0.0.1;<local>
  ```
  **验证结果**：代理死循环已被彻底解决。从系统连接表（`netstat`）可明确查看到后续浏览器的请求已经 100% 直连到了本机的 `127.0.0.1`，不再转发给代理。

---

### 2. 阶段二故障：Token 兑换网络阻断导致后台服务崩溃断开（端口 49190）

在解决代理白名单后，用户再次尝试登录（端口 49190），浏览器仍然报连接断开。通过核对插件全链路运行日志 `data/user-data/logs/.../Claude VSCode.log`，定位到了真实的崩溃过程：

#### ① 关键日志证据链（第 522 ~ 530 行）
```text
2026-10-18 13:44:41.548 [info] Received message from webview: {"type":"request","requestId":"e4edk48dz6","request":{"type":"submit_oauth_code","code":"<string>"}}
2026-10-18 13:44:41.574 [info] From claude: 2026-10-18T05:44:41.573Z [INFO] claude_authenticate flow ended: Error: connect ECONNREFUSED 192.168.x.x:10809
2026-10-18 13:44:41.574 [warning] claude_oauth_callback rejected: Error: connect ECONNREFUSED 192.168.x.x:10809
2026-10-18 13:44:41.574 [error] Error processing client request: Error: connect ECONNREFUSED 192.168.x.x:10809
2026-10-18 13:44:41.582 [info] Received message from webview: {"type":"interrupt_claude","channelId":"nhwyu8ilv3o"}
2026-10-18 13:44:41.618 [info] From claude: 2026-10-18T05:44:41.618Z [DEBUG] [uds-messaging] Shutting down
2026-10-18 13:44:41.628 [info] From claude: 2026-10-18T05:44:41.625Z [DEBUG] LSP server manager shut down successfully
```

#### ② 事故链条深度还原
1. 浏览器完成了在 `claude.ai` 的身份授权，带着授权码 `<REDACTED_OAUTH_CODE>...` 访问本地回调 `http://localhost:49190/callback`；
2. 此时浏览器确实连接到了本机的 `127.0.0.1:49190`；
3. 本机后台程序 `claude.exe` 收到 Code 后，需要执行 **OAuth Token 兑换**（即通过 HTTPS POST 请求 Anthropic 的 Token 端点，用 Code 换取长期凭证 Access Token）；
4. `claude.exe` 尝试通过配置的环境变量 `HTTP_PROXY=http://192.168.x.x:10809` 发起出站请求；
5. **底层网络请求瞬间失败**，抛出错误：`Error: connect ECONNREFUSED 192.168.x.x:10809`；
6. 认证流程异常中止后，`claude.exe` 认为授权已失败，立即执行 `Shutting down` 并**主动关闭了本地 49190 端口的 HTTP 监听**；
7. 正在等待 HTTP 响应的浏览器因为 TCP 连接被服务端主动粗暴切断，在前端渲染出 `ERR_CONNECTION_REFUSED` / `ERR_CONNECTION_RESET`。

---

## 三、`claude.exe` 连接代理报 `ECONNREFUSED` 的根本原因分析

为什么独立的 Python 和 Curl 都能正常使用该代理，而 `claude.exe` 连接代理却会报 `ECONNREFUSED`？

### 1. 运行时底层差异：Bun on Windows 7
通过日志分析（日志第 902 行明确输出 `[win32-kernel32] bun:ffi loaded`），`claude.exe` 并非由 Node.js 打包，而是由 **Bun** 运行时编译的原生二进制。
Bun 是一个专为 Linux 和现代 Windows 10/11 设计的运行时，其 Windows 网络栈（基于 `uSockets` 与异步 I/O）高度依赖 Windows 10+ 的非阻塞 IOCP/AFD（Async File Driver）语义。

### 2. 独立网络对比测试证据

为了隔离问题，我们在当前环境执行了一组对比测试：

| 测试对象 | 请求方式 | 目标 | 结果 | 结论 |
| :--- | :--- | :--- | :--- | :--- |
| **Python 3.11** | `urllib.request` 经 `192.168.x.x:10809` | `https://api.anthropic.com` | **成功连接**（返回 API 预期的 404） | 局域网代理服务与网络路由完全正常 |
| **Curl 8.10** | `curl -x http://192.168.x.x:10809` | `https://api.anthropic.com` | **成功握手**（返回 Cloudflare 响应） | HTTP CONNECT 代理隧道完全正常 |
| **`claude.exe`** | 内部 fetch 经 `192.168.x.x:10809` | 任意出站 HTTPS 请求 | **直接失败**（`connect ECONNREFUSED`） | 故障仅存在于 `claude.exe` 自身网络层 |

### 3. 端口转发抓包实验（定位到主动 RST）
我们在本地搭建了一个调试转发器（`127.0.0.1:18888 -> 192.168.x.x:10809`）并捕获底层 TCP 交互，结果令人震惊：
```text
Accepted connection from ('127.0.0.1', 49515)
[CLIENT->PROXY] Error: [WinError 10054] 远程主机强迫关闭了一个现有的连接。
```
**实验发现**：
- `claude.exe` 确实向代理端口发起了 TCP 连接且连接已成功建立；
- 但在连接建立后的一瞬间，**尚未发送任何 HTTP CONNECT 握手报文，`claude.exe` 内部便直接向代理发送了 TCP RST 复位包**；
- 随后 Bun 将此异常状态包装成了 `connect ECONNREFUSED` 向上抛出。
- **机理**：在 Windows 7 下（通过 VxKex 兼容运行），Bun 内部在处理非阻塞套接字的完成状态时，对 Windows 7 旧版 Winsock/AFD 的状态回调产生误判，错误地提前销毁了套接字。

---

## 四、衍生问题：服务端账号风控（`account_on_hold`）

在排查过程中，用户还遇到过以下错误：
```text
http://localhost:54105/callback?error=access_denied&error_description=account_on_hold&error_uri=https%3A%2F%2Fclaude.ai%2Frestricted
```
- **性质**：属于 Anthropic 云端业务风控，与本地网络/TLS 故障无关；
- **成因**：用户使用的代理出口 IP 被 Anthropic 识别为机房高风险 IP、多人共享节点或区域异常，导致账号被临时标记为 `account_on_hold`（挂起受限）。

---

## 五、三款主流 AI 插件在 Windows 7 下的网络与认证机制横向对比

| 插件名称 | 运行时底层 | 根证书库来源 | 代理支持机理 | OAuth 回调兼容性 | 最佳使用姿势 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Antigravity** | Node.js (Electron Exthost) | 静态编译 Mozilla 全量 CA 库 | 完美遵循 VS Code 内部代理配置 | 极高（单进程内部直接完成） | 网页登录 / 插件原生直接登录 |
| **Codex** | Rust (`reqwest` + `rustls`) | 外部注入 `SSL_CERT_FILE` | 遵循环境变量 `HTTP_PROXY` | 良好（回调只需直连 127.0.0.1） | 网页登录或 API Key 均可 |
| **Claude Code** | **Bun 单文件原生二进制** | 混合捆绑系统 CA | **Win7 下非阻塞代理隧道存在缺陷** | **脆弱（Token 兑换易触发崩溃）** | **强烈推荐 API Key 模式** |

---

## 六、彻底解决方案与操作指南

鉴于 `claude.exe`（Bun）在 Windows 7 环境下对 HTTP 代理隧道的兼容缺陷，走网页端 OAuth 登录会不可避免地触发本地 Token 兑换断开。**最稳健、最推荐的解决方案是改用 API Key 模式，彻底绕开 OAuth 流程**。

### 方案 A：使用 Anthropic API Key 直连（最推荐、彻底解决）

Claude Code 官方原生提供 API Key 优先鉴权机制。配置后插件将直接使用 API Key 通信，不再启动本地 HTTP 监听端口，也不再触发浏览器 OAuth 跳转。

1. **修改 VS Code 用户配置**：
   打开文件：`D:\VSCode-win32-x64-1.107.1\data\user-data\User\settings.json`；
2. 在 `claudeCode.environmentVariables` 列表中增加 `ANTHROPIC_API_KEY`：
   ```json
   "claudeCode.environmentVariables": [
       {
           "name": "HTTPS_PROXY",
           "value": "http://192.168.x.x:10809"
       },
       {
           "name": "HTTP_PROXY",
           "value": "http://192.168.x.x:10809"
       },
       {
           "name": "NO_PROXY",
           "value": "localhost,127.0.0.1"
       },
       {
           "name": "SSL_CERT_FILE",
           "value": "C:\\Program Files\\Git\\mingw64\\etc\\ssl\\certs\\ca-bundle.crt"
       },
       {
           "name": "ANTHROPIC_API_KEY",
           "value": "sk-ant-api03-你的完整API_KEY"
       }
   ]
   ```
3. **重载 VS Code 窗口**：
   按快捷键 `Ctrl + Shift + P`，输入并运行：
   `Developer: Reload Window`
4. 打开 Claude Code 面板，插件直接进入已激活对话状态，彻底免去登录失败困扰。

---

### 方案 B：使用 `setup-token`（适用于 Claude Pro / Team 网页订阅用户）

如果你使用的是 Claude 官方网页订阅而非 API Key，可利用官方为 Headless/远程服务器设计的长效凭据机制：

1. 在终端中直接运行：
   ```bat
   "D:\VSCode-win32-x64-1.107.1\data\extensions\anthropic.claude-code-2.1.301-win32-x64\resources\native-binary\claude.exe" setup-token
   ```
2. 按照控制台提示完成授权，生成长效 Token 后自动保存在本地配置中，绕开本地 Webview 端口回调。

---
claude官方补充说明：
命令本身基本正确，但第 2 步的说法有误：令牌不会自动保存。

第 1 步（命令）

claude setup-token 是官方子命令，直接调用插件自带的 claude.exe 也可以。要注意终端类型：

cmd：照你写的运行即可。
PowerShell：带引号的路径前面要加 &，否则会报错：
powershell
  & "D:\VSCode-win32-x64-1.107.1\data\extensions\anthropic.claude-code-2.1.301-win32-x64\resources\native-binary\claude.exe" setup-token

运行前请确认这个路径在你机器上真实存在（插件版本号不同，文件夹名也会不同）。

第 2 步（错误）

官方文档说明，该命令会引导您完成 OAuth 授权并将令牌打印到终端。它不会将令牌保存在任何地方；复制它并将其设置为 CLAUDE_CODE_OAUTH_TOKEN 环境变量。 
claude

所以正确的第 2 步应该是：

授权完成后，复制终端打印出的 sk-ant-oat01-... 令牌。令牌只显示这一次。
在 cmd 中设置为用户环境变量：
bat
   setx CLAUDE_CODE_OAUTH_TOKEN "sk-ant-oat01-你的令牌"
完全关闭并重新打开 VS Code，新的环境变量才会生效。

其他需要注意的地方

仍然需要浏览器授权一次：生成令牌时仍需至少成功完成一次 OAuth。它的作用是避开 VS Code 内部 Webview 的登录流程，而不是完全不用浏览器。 
github
订阅要求：需要 Pro、Max、Team 或 Enterprise 计划。 
claude
功能限制：它的范围仅限于推理，无法建立 Remote Control 会话。 
claude
有效期：令牌有效期为一年，到期后需要重新运行 setup-token。
安全：这个令牌等同于你的订阅凭证，不要提交到代码仓库，也不要发给别人。

Sources:

Claude Code 身份验证文档（中文）
GitHub issue #74204


可以在其它电脑上登陆，如果在虚拟机中通过不了的话，可能还是有bun的bug问题：

PS C:\Users\liusj> $exe = "D:\VSCode-win32-x64-1.107.1\data\extensions\anthropic.claude-code-2.1.301-win32-x64\resources\native-binary\claude.exe"
PS C:\Users\liusj> claude.exe --version
claude.exe : 无法将“claude.exe”项识别为 cmdlet、函数、脚本文件或可运行程序的名称。请检查名称的拼写，如果包括路径，请
确保路径正确，然后再试一次。
所在位置 行:1 字符: 1
+ claude.exe --version
+ ~~~~~~~~~~
    + CategoryInfo          : ObjectNotFound: (claude.exe:String) [], CommandNotFoundException
    + FullyQualifiedErrorId : CommandNotFoundException

PS C:\Users\liusj> & $exe --version
2.1.301 (Claude Code)
PS C:\Users\liusj> & $exe setup-token


Your OAuth token (valid for 1 year):

 。。。。。。。

 Store this token securely. You won't be able to see it again.

 Use this token by setting: export CLAUDE_CODE_OAUTH_TOKEN=<token>
export CLAUDE_CODE_OAUTH_TOKEN=。。。。

setx CLAUDE_CODE_OAUTH_TOKEN "。。。。。"

### 方案 C：命令行控制台鉴权（`claude auth login --console`）

在终端中执行：
```bat
"D:\VSCode-win32-x64-1.107.1\data\extensions\anthropic.claude-code-2.1.301-win32-x64\resources\native-binary\claude.exe" auth login --console
```
直接在控制台命令行中粘贴 Key，完成全局用户凭证绑定。

