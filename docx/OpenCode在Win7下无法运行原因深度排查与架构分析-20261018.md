# OpenCode CLI 在 Windows 7 下无法运行原因深度排查与架构分析（2026-10-18）

> [!IMPORTANT]
> **⚠️ 历史阶段性结论更正与修复声明（2026-11-01）**：
> 本文下文所做的“Bun 在 Win7 下有不可修复的驱动级缺陷”、“必须放弃 Bun”为早期排查推论，**现已在 VxKex NEXT 中彻底排查清楚并完美修复**！
> - **真实根因**：Bun 报 `TypeError: Unable to connect` 并发送 `TCP RST`，是因为 `uSockets` 在调用 `recv()` 时硬编码传入了 Windows 8+ 新增标志 `MSG_PUSH_IMMEDIATE (0x20)`，触发 Win7 `mswsock.dll` 报错 `WSAEOPNOTSUPP (10045)`；同时其延迟加载的 `wsock32.dll` 绕过了兼容层；
> - **解决状态**：已在 `KxNet.dll` 过滤清除 `0x20` 标志，并在 `KexDll.dll` 中强制重写 `wsock32.dll`。包含修复的最新安装包 `KexSetup_Release.exe` 部署后，原生 Bun 编译的 `opencode.exe` 已可 100% 正常进行本地 HTTP 微服务通信与远端联网。详见 [`docx/Bun在Win7下ENOTSUP与网络失败根因逆向分析-20261101.md`](Bun在Win7下ENOTSUP与网络失败根因逆向分析-20261101.md) 与 [`docx/VxKex-NEXT修复Bun网络兼容性设计方案-20261101.md`](VxKex-NEXT修复Bun网络兼容性设计方案-20261101.md)。

## 一、现象与问题描述

用户在 Windows 7 SP1 x64（VxKex NEXT 1.2.3.2463）环境下，运行位于 `D:\opencode-cli-2.0.6-windows-x64\opencode.exe` 的 OpenCode CLI 工具时，程序无法正常进入交互界面，表现为：
1. 直接执行 `opencode` 时卡顿数秒后退出，或抛出服务进程异常退出；
2. 再次执行时提示端口被占用：
   ```text
   Managed service port 49374 on 127.0.0.1 is already in use by another process.
   Configure another port with `opencode service set port <port>` and start the service again.
   Server process exited with code 3
   ```
3. 即使加上官方独立运行参数 `opencode --standalone --print-logs`，程序仍然瞬间崩溃报错：
   ```text
   ERROR (#1): UnknownError: An error occurred in Effect.tryPromise
   [cause]: ClientError: Transport
   [cause]: TypeError: Unable to connect. Is the computer able to access the url?
       at async <anonymous> (B:/~BUN/root/chunk-fnnphm06.js:2:781)
       at Tui.run (B:/~BUN/root/chunk-5e6hn758.js:98:247)
   ```

---

## 二、OpenCode 内部架构与执行机理

通过对 `opencode.exe` 二进制文件（PE 节结构含 `.bun`、`.bunblt`，体积 205MB）与全局路径、日志的深入排查，确认：

1. **运行时底层**：`opencode.exe` 是基于 **Bun** 运行时编译打包的单文件应用（Single Executable Application），内部使用 Effect-TS 框架与 `@opentf` / Ink 构建终端界面。
2. **前后端解耦的本地微服务架构**：
   - 用户在终端启动的 `opencode` 实际上是一个 **CLI 前端（TUI 客户端）**；
   - OpenCode 不在 CLI 主线程中直接处理 AI 逻辑与本地状态，而是依赖一个后台托管服务：`opencode.exe serve --service`，默认在本地监听端口 `127.0.0.1:49374`；
   - CLI 客户端启动后，首先通过 HTTP REST 与 WebSocket 连接 `http://127.0.0.1:49374` 进行健康检查与握手；握手成功后才渲染 TUI 界面。

---

## 三、根本原因深度分析（三大阻断链条）

### 阻断一：`Bun.serve()` 服务端在 Win7 下所有入站连接 100% 发生 TCP Reset（核心死穴）

这是导致 OpenCode 在 Windows 7 下完全无法工作的最根本技术瓶颈。

#### 1. 实验复现与抓包证据
我们直接在前台启动 OpenCode 的 Web 服务端：
```bat
"D:\opencode-cli-2.0.6-windows-x64\opencode.exe" serve --port 49374 --print-logs
```
服务端成功输出启动信息并处于监听状态：
```text
server listening on http://127.0.0.1:49374
server password 9tG_s5UrKSeFARj368u5zYGkLbRvJ3rndGQ5P52kJec
```
随后，我们在另一个终端中使用 Curl 向该端口发起测试请求：
```bat
curl -v http://127.0.0.1:49374/
```
结果极其确凿：
```text
*   Trying 127.0.0.1:49374...
* Connected to 127.0.0.1 (127.0.0.1) port 49374
* using HTTP/1.x
> GET / HTTP/1.1
> Host: 127.0.0.1:49374
> User-Agent: curl/8.10.1
* Recv failure: Connection was reset
* closing connection #0
curl: (56) Recv failure: Connection was reset
```
无论是通过 IPv4（`127.0.0.1`）还是 IPv6（`::1`），无论是否携带认证密码 Header，**一旦客户端发送 HTTP 请求数据，服务端底层的套接字驱动立即主动发送 TCP RST 强行重置连接**。

#### 2. 深层机理（Bun 与 Windows 7 内核网络模型不兼容）
- Bun 的 HTTP 服务端（`Bun.serve()`）构建于高性能 C 语言网络库 `uSockets` 之上；
- 在 Windows 平台，`uSockets` 严重依赖 Windows 10（1709+）引入的现代完成端口（IOCP）与非阻塞异步文件驱动（`AFD.SYS`）行为；
- 在 Windows 7 环境下（即使有 VxKex 导出模拟），Windows 7 的 Winsock 内核驱动对重叠 I/O 状态完成（Overlapped Completion）的回调状态与 Win10 存在差异，导致 `uSockets` 在完成 TCP 握手并尝试从套接字读取 HTTP 数据时，因非预期返回值直接进入异常处理逻辑，强行向对端发出 RST 中断连接。

---

### 阻断二：HTTP 握手失败引发的级联崩溃与僵尸端口冲突

由于阻断一的存在，OpenCode 的客户端每次尝试连接后台服务时都会被直接掐断连接：

1. **第一次运行**：
   - CLI 客户端启动，检测到服务不存在；
   - CLI 子进程在后台启动 `opencode serve --service`（PID 比如为 2428），成功占用 `49374` 端口；
   - CLI 客户端尝试向 `127.0.0.1:49374` 发送 HTTP 握手探测；
   - 遭遇 `Recv failure: Connection was reset`；
   - CLI 客户端在超时后认为后台服务未能启动，直接抛出 `Server process exited with code 3` 退出；
   - **但是，后端的第一个 `opencode.exe` 进程并没有退出，仍然作为僵尸进程挂在后台并霸占着 49374 端口**。
2. **第二次运行**：
   - 用户再次键入 `opencode`；
   - CLI 客户端再次探测 49374 端口，依然被 TCP Reset；
   - CLI 再次尝试调用后台启动命令；
   - 新启动的 `serve` 进程在尝试绑定 49374 时触发 `EADDRINUSE`（地址已被占用），向日志写入：
     `Managed service port 49374 on 127.0.0.1 is already in use by another process.`
   - 两个异常相互嵌套，形成了用户看到的“端口被占用且服务不可用”死循环。

---

### 阻断三：`--standalone` 私有实例依然无法绕开 HTTP 传输层

部分用户试图通过 `--standalone` 标志绕过后台常驻服务：
```bat
opencode --standalone
```
然而排查显示，OpenCode 的 `--standalone` 模式设计如下：
1. CLI 主进程启动私有子进程：
   `opencode.exe serve --stdio --port 0`；
2. 子进程在标准输出上打印随机分配的私有 HTTP 地址，例如：
   `{"url":"http://127.0.0.1:51808"}`；
3. 前台 TUI 客户端**依然通过 HTTP 协议去连接这个随机端口（`51808`）**；
4. 随即立刻再次触发阻断一的 `Bun.serve()` 连接重置；
5. TUI 抛出 `ClientError: Transport (TypeError: Unable to connect. Is the computer able to access the url?)`，当场崩溃。

---

### 阻断四：云端更新与模型出站请求受限于 Bun 代理隧道缺陷

在日志 `C:\Users\liusj\.local\share\opencode\log\opencode.log` 中反复出现：
```text
message="update check failed" error="Error: Failed to check for updates (cause: TypeError: Unable to connect. Is the computer able to access the url?)"
```
这与我们先前排查 Claude Code（同为 Bun 运行时）的根本原因完全一致：
- Bun 在 Windows 7 下的非阻塞 Socket 发起出站代理隧道（`HTTP_PROXY`）连接时，在 TLS 握手阶段同样会触发主动 RST，导致任何出站 HTTPS 请求报 `ECONNREFUSED` 或 `Unable to connect`。

---

## 四、AI 工具在 Windows 7 平台的技术可行性总结

| 工具名称 | 运行时 | 网络与架构特征 | Win7 兼容状态 | 建议与结论 |
| :--- | :--- | :--- | :--- | :--- |
| **OpenCode** | **Bun** | **前后端分离强依赖本地 `Bun.serve()` HTTP 通信** | **完全不可用（0%）** | `uSockets` 在 Win7 上必定 Reset 本地 HTTP 连接，架构上彻底无解 |
| **Claude Code** | **Bun** | 客户端架构，走 Webview 进程与 IPC 管道 | **部分可用（API 模式 100%）** | 网页 OAuth 流程失败，但配置 `ANTHROPIC_API_KEY` 后可绕开本地网络缺陷正常工作 |
| **Codex** | **Rust** | 标准同步/异步 mio 线程模型，系统 API | **完全可用（100%）** | 注入 `SSL_CERT_FILE` 并关闭 CRL 吊销检查后，终端与 Webview 体验完全正常 |
| **Antigravity** | **Node.js** | 内嵌 Chromium / Node.js 完整环境 | **完全可用（100%）** | 拥有独立完整的现代 CA 库与代理栈，无任何兼容障碍 |

---

## 五、结论与替代建议

1. **技术定论**：
   `D:\opencode-cli-2.0.6-windows-x64\opencode.exe` 之所以无法在 Windows 7 上运行，**并非缺少 DLL 导出，也不是 ConPTY 伪控制台问题，而是由于其底层依赖的 Bun 运行时及其 `uSockets` 网络引擎在 Windows 7 平台的非阻塞网络 I/O 驱动缺陷**。
   由于 OpenCode 整个系统的基础通信（无论是后台模式还是 standalone 模式）都强制架设在 `Bun.serve()` 本地 HTTP 通信之上，只要入站连接发生 TCP Reset，整个 CLI 便寸步难行。

2. **建议方案**：
   - 在当前 Windows 7 环境下，**强烈建议放弃使用基于 Bun 的 OpenCode**；
   - 推荐使用已在该环境下完全验证调优通过的 **Codex**（Rust）或 **VS Code 内置插件 / Antigravity**；
   - 若确需使用 OpenCode，可将其部署在局域网的 Linux/Mac/Win10 服务器上，在 Win7 本机通过浏览器或远程终端访问其 Web 界面。

