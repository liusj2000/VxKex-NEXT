# Codex 插件登录 OAuth Token 换取失败与 TLS 根因分析及解决措施（2026-10-18）

## 一、现象与问题描述

在 Win7 SP1 x64 环境下，通过 VxKex NEXT（补齐 `IsApiSetImplemented` 与 `DeriveCapabilitySidsFromName`）成功解决 `codex.exe` 启动缺失导出（`0xc0000139`）后，VS Code（`D:\VSCode-win32-x64-1.107.1` 便携版）中的 Codex 插件（`openai.chatgpt-26.1007.21434-win32-x64`）已能正常渲染前端界面。

用户在面板中点击 **「Sign in with ChatGPT」**：
1. 外部浏览器成功打开 OpenAI 网页登录授权；
2. 授权完成后，浏览器携带授权码重定向回本地回调地址：
   `http://127.0.0.1:1455/auth/callback?code=oai...`；
3. 但浏览器与 VS Code 界面随即报错中断：
   ```text
   Sign-in could not be completed
   Token exchange failed: error sending request for url (https://auth.openai.com/oauth/token)
   Error code: token_exchange_failed
   ```

---

## 二、日志排查与证据链

### 1. VS Code 插件日志记录
查看日志：`D:\VSCode-win32-x64-1.107.1\data\user-data\logs\20261018T120510\window1\exthost\openai.chatgpt\Codex.log`

关键日志（第 33 行、40 行）：
```text
2026-10-18 12:07:51.853 [error] [CodexMcpConnection] cli: message="2026-10-18T04:07:51.851663Z ERROR codex_login::server: oauth token exchange transport failure is_timeout=false is_connect=true is_request=true error=error sending request for url (https://auth.openai.com/oauth/token)\nToken exchange error: error sending request for url (https://auth.openai.com/oauth/token)\n2026-10-18T04:07:51.851663Z ERROR codex_login::server: login callback token exchange failed"
```

**关键指标**：
- `is_timeout=false`：非连接超时；
- `is_connect=true`：错误发生在**建立 TCP/TLS 连接或握手阶段**；
- `error sending request for url (https://auth.openai.com/oauth/token)`：表明在尝试通过 HTTPS 向 `auth.openai.com` 发起 POST 请求以将 code 兑换为 Access Token 时，连接被底层网络/TLS 协议栈中断。

### 2. 插件健康诊断（`codex doctor`）全面体检
执行 `codex.exe doctor` 进行连通性检查：
```text
Connectivity
  ✓ network      network-related environment looks readable
      proxy env vars present   HTTP_PROXY, HTTPS_PROXY, NO_PROXY, http_proxy, https_proxy, no_proxy
      respect system proxy     disabled
      managed proxy            not configured
  ⚠ websocket    Responses WebSocket failed; HTTPS fallback may still work
      endpoint                 wss://api.openai.com/v1/<redacted>
      handshake transport error http 401 Unauthorized (已到达OpenAI，TLS正常)
  ✗ reachability one or more required provider endpoints are unreachable over HTTP
      reachability mode        ChatGPT auth
      ChatGPT inference URL    https://chatgpt.com/backend-api/<redacted> TLS handshake or certificate validation failed (required)
  ⚠ updates      update configuration is locally consistent
      latest version probe     error sending request for url (https://api.github.com/repos/<redacted>)
```

**核心发现**：
- 不仅 OAuth 换取 Token 失败，`codex.exe` 访问 `chatgpt.com` 时也明确报告：**`TLS handshake or certificate validation failed (required)`**！
- 访问 GitHub API 也同样报 `error sending request for url`。
- 说明该问题是通用的 **TLS 握手与证书校验失败**。

---

## 三、根本原因深度分析

### 1. 架构机理：Rust `rustls-platform-verifier` 在 Windows 7 上的局限
逆向与依赖分析确认 `codex.exe` 采用：
- `reqwest-0.12.28`
- `rustls-0.23.45`
- `rustls-platform-verifier-0.7.0`（位于 `windows.rs`）

在 Windows 平台上，`rustls-platform-verifier` 会调用系统 Win32 CryptoAPI（`CertGetCertificateChain` 与 `CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, ...)`），将证书链校验完全交由宿主操作系统的证书库与验证逻辑处理。

### 2. 根证书缺失：Windows 7 SP1 原生根证书库停滞于 2009~2011 年
现代互联网主流云服务（OpenAI、Cloudflare、GitHub）已全面切换到现代 ECC 根证书及 Google Trust Services：
- `chatgpt.com` 的证书链：
  - 叶子证书：`chatgpt.com`（由 `WE1` 签发）；
  - 中间证书：`WE1`（Google Trust Services，由 `GlobalSign ECC Root CA - R4` 签发）；
  - 根证书：`GlobalSign ECC Root CA - R4`（2012 年发布）。
- 检查 Win7 系统 `ROOT` 存储区：仅有 25 张老旧根证书，**根本不存在 `GlobalSign ECC Root CA - R4`**，也不存在 Google Trust Services 现代根证书。
- 缺少根证书导致 Windows 7 CryptoAPI 构建证书链时返回 `CERT_TRUST_IS_PARTIAL_CHAIN` (`0x10000`) 或 `CERT_TRUST_IS_UNTRUSTED_ROOT` (`0x20`)。

### 3. CRL 吊销服务器不可达（`0x80092013 CRYPT_E_REVOCATION_OFFLINE`）
- Windows 7 注册表默认设置 `CertificateRevocation = 1`（启用服务器证书吊销检查）；
- 证书扩展中包含的 CRL 吊销地址为 Google 的 `http://c.pki.goog/...`；
- 在国内网络环境下，直接访问 `pki.goog` 被阻断；Windows 7 CryptoAPI 请求 CRL 超时失败，返回错误码 `0x80092013`（`CRYPT_E_REVOCATION_OFFLINE`）；
- `rustls-platform-verifier` 将脱机吊销视为致命错误，立即中断 TLS 握手。

### 4. 为什么浏览器能打开，而 codex 会失败？
- 现代浏览器（如新版 Chrome/Edge/Firefox）内置了自己的 Mozilla / Chrome Root Store 根证书库，不完全依赖 Win7 系统的过时证书库，且走已配置的系统代理（`192.168.x.x:10809`）；
- 授权重定向回本地后，接收请求并发起 Token 交换的是由 VS Code 扩展启动的 `codex.exe` 子进程；
- `codex.exe` 采用 Rust 原生网络栈并默认调用 Win7 系统 CryptoAPI，因而立刻撞上上述根证书缺失与吊销失败双重阻断。

---

## 四、解决措施与实施记录

### 1. 证书信任机制修复（注入现代 Mozilla CA 证书包）
经逆向 `codex.exe` 源码（`http-client\src\custom_ca.rs`），发现其支持直接读取环境变量 `SSL_CERT_FILE`。
当设置 `SSL_CERT_FILE` 时，`codex.exe` 会载入该文件中的全部权威根证书（包含 GlobalSign ECC R4、Google Trust Services、ISRG Root X1/X2、Sectigo 等全部现代根证书），绕过 Win7 系统 CryptoAPI 的缺失问题。

本机 Git 目录下自带一份完整且合规的 Mozilla CA 证书包：
`C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt`

**实测对比**：
- 未设置 `SSL_CERT_FILE`：`reachability fail - TLS handshake or certificate validation failed`
- 设置 `SSL_CERT_FILE` 后：
  ```text
  ✓ reachability active provider endpoints are reachable over HTTP
      ChatGPT inference URL reachable (HTTP 405)
  ```
  网络诊断全部转绿！

### 2. 用户全局环境变量永久配置
通过 PowerShell 将以下环境变量持久化写入 Windows 注册表（`HKCU\Environment`）：
- `SSL_CERT_FILE = C:\Program Files\Git\mingw64\etc\ssl\certs\ca-bundle.crt`
- `HTTP_PROXY = http://192.168.x.x:10809`
- `HTTPS_PROXY = http://192.168.x.x:10809`
- `NO_PROXY = localhost,127.0.0.1`

确保所有新启动的用户进程（VS Code、`codex.exe`、git、python 等）自动继承合法的代理与根证书包。

### 3. 关闭系统吊销检查误杀
在注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings` 中：
- 将 `CertificateRevocation` 设为 `0`；
- 避免因 Google CRL/OCSP 服务器国内无法直连而产生 `0x80092013` 误报。

### 4. 同步 VS Code 配置
修改 `D:\VSCode-win32-x64-1.107.1\data\user-data\User\settings.json`：
- 在 `terminal.integrated.env.windows` 中增加 `SSL_CERT_FILE`、`HTTP_PROXY`、`HTTPS_PROXY`；
- 在 `claudeCode.environmentVariables` 中增加 `SSL_CERT_FILE`。

### 5. 清理陈旧进程
终止之前启动、仍持有旧环境变量的后台 `codex.exe` 进程（PID 384）。

---

## 五、验证与使用步骤

1. **重载或重启 VS Code**：
   在 VS Code 中按快捷键 `Ctrl + Shift + P`，输入并运行：
   `Developer: Reload Window`
   （或直接退出 VS Code 重新打开），使扩展宿主进程读取最新的 `SSL_CERT_FILE` 与代理环境变量。
2. **在 Codex 界面完成登录**：
   - 在右侧 Codex 面板重新点击 **「Sign in with ChatGPT」**；
   - 浏览器授权通过后跳转回本地，此时 `codex.exe` 换取 Token 的 HTTPS 请求将顺利通过 TLS 校验，登录成功完成；
   - 也支持直接点击面板中的 **「Use API Key」** 使用个人 API 密钥。

---

## 六、横向对比延伸

关于 Antigravity 插件为何可以在同一环境下顺利认证，而 Codex 插件会触发此 TLS 问题的详细架构与协议栈机理对比，详见专门分析文档：
[Antigravity 与 Codex 插件认证架构及 TLS 证书链处理机制对比分析（2026-10-18）](Antigravity与Codex插件认证架构及TLS对比分析-20261018.md)。

