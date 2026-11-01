# Bun 在 Windows 7 + VxKex NEXT 下 `ENOTSUP` 与网络失败根因逆向分析（2026-11-01）

## 1. 背景与问题描述

在 Windows 7 SP1 x64 + VxKex NEXT 环境下，所有基于 **Bun 运行时**编译的命令行与桌面后端工具——包括：
- **Claude Code 原生版**（`claude.exe` 2.1.314 / 2.1.316，内嵌 Bun 1.4.4）
- **OpenCode 原生版**（基于 Bun 构建的单文件 CLI）
- **独立 `bun.exe` 运行时**

在启动或发起网络通信时均会出现两类致命网络故障，这也是此前不得不将 OpenCode 和 Claude Code 降级/切换为 Node.js 版的根本原因：
1. **出站 HTTP/HTTPS/代理连接失败（`ENOTSUP` + `TCP RST`）**：
   - 调用 `fetch()`、`Bun.connect()` 或 `node:http` 客户端连接本地代理（如 `127.0.0.1:15800`、`127.0.0.1:10809`）或直连目标服务器时，TCP 三次握手在内核层实际已完成（服务端 `accept` 已触发），但 Bun 立即抛出：
     ```text
     TypeError: ENOTSUP: Unable to connect. Is the computer able to access the url?
     ```
   - 紧接着对端服务器收到 **TCP `RST`（`read ECONNRESET`）**，整个连接中无任何应用层字节发出（`connections > 0, requests = 0`）。
2. **入站本地 HTTP 回调服务失败（`ENOTSUP: operation not supported on socket, read`）**：
   - OAuth 浏览器登录回调、OpenCode 本地控制服务等使用 `Bun.serve()` 或 `node:net` `createServer()` 监听 `127.0.0.1` 端口时，客户端连接刚被 `accept`，服务端立即报错：
     ```text
     ENOTSUP: operation not supported on socket, read (syscall: "read", errno: -4049, code: "ENOTSUP")
     ```

---

## 2. 历史假设复核与证伪

此前在多份排查文档（[`docx/Bun运行时在Win7兼容层下的网络实现缺陷分析及建议-20261018.md`](Bun运行时在Win7兼容层下的网络实现缺陷分析及建议-20261018.md)、[`docx/OpenCode在Win7下无法运行原因深度排查与架构分析-20261018.md`](OpenCode在Win7下无法运行原因深度排查与架构分析-20261018.md)、[`docx/ClaudeCode-ENOTSUP排查-20261101.md`](ClaudeCode-ENOTSUP排查-20261101.md)、[`docx/ClaudeCode新模型与Bun兼容修复可行性复核-20261101.md`](ClaudeCode新模型与Bun兼容修复可行性复核-20261101.md)）中，曾提出过以下推测：
- 推测 A：Bun / `libuv` 强依赖 Windows 8+ 内核 API `NtCreateWaitCompletionPacket` / `NtAssociateWaitCompletionPacket`，在 Win7 下缺失导致事件循环失败；
- 推测 B：`libuv` 的 `uv_poll_init_socket` 调用 `WSAIoctl(SIO_BASE_HANDLE)` 或 `IOCTL_AFD_POLL` 在 Win7 下失败；
- 推测 C：`setsockopt` 设置 `SO_REUSE_UNICASTPORT` 等选项失败。

本次通过对 [`claude.exe`](file:///D:/VSCode-win32-x64-1.107.1/data/extensions/anthropic.claude-code-2.1.314-win32-x64/resources/native-binary/claude.exe)（Bun 1.4.4，revision `adbced2272bdbf18b4a45542955b17400a5b072c`）的 `.text` 段全量反汇编与运行时动态 IAT 追踪，**正式证伪推测 A 与推测 B**：

### 2.1 证伪推测 A：`NtCreateWaitCompletionPacket` 在 `libuv` 中已有原生 Win7 降级分支
扫描 `claude.exe` 的整个 `.text` 段可知，全局函数指针 `pNtCreateWaitCompletionPacket`（`RVA 0x7129c78`）与 `pNtAssociateWaitCompletionPacket`（`RVA 0x7129c80`）仅在 `uv_run`（`RVA 0x481402`）中用于高精度定时器：
```asm
; RVA 0x481402 (libuv uv_run)
49 83 bf c8 00 00 00 00   cmp qword ptr [r15+0xc8], 0   ; loop->timer_par (WaitCompletionPacket 句柄)
0f 84 c9 00 00 00         je  0x4814d3                  ; Win7 下为 NULL，直接跳转到 0x4814d3！
...
; RVA 0x4814d3
48 8b 4f 38               mov rcx, qword ptr [rdi+0x38] ; loop->iocp
...
ff 15 0c 56 b9 06         call qword ptr [kernel32!GetQueuedCompletionStatusEx]
```
当 Win7 `ntdll.dll` 不存在 `NtCreateWaitCompletionPacket` 时，`loop->timer_par` 为 `NULL`，`uv_run` 原生直接将超时毫秒数传给 `GetQueuedCompletionStatusEx`，**完全不会报错，更不会产生 `ENOTSUP`**。

### 2.2 证伪推测 B：`SIO_BASE_HANDLE` 与 `IOCTL_AFD_POLL` 在 Win7 下均原生成功
反汇编 `uv_poll_init_socket`（`RVA 0x48d650`）与动态追踪证明：
- `ioctlsocket(s, FIONBIO, &1)` 返回 `0`；
- `WSAIoctl(s, SIO_BASE_HANDLE = 0x48000022)` 返回 `0`；
- `getsockopt(s, SOL_SOCKET, SO_PROTOCOL_INFOW = 0x2005)` 返回 `0`，且 GUID 匹配 `uv_msafd_provider_ids`，成功创建 `peer_socket` 并投递 `IOCTL_AFD_POLL (0x00012007)`。

---

## 3. 真正根因逆向定位（三层叠加致死链）

通过 `BUN_OPTIONS="--preload ..."` 结合 `bun:ffi` 在 `claude.exe` 进程内挂载 Winsock IAT 追踪器（记录每次调用的返回地址 RVA、参数、返回值与 `WSAGetLastError()`），捕获到未修复前 `fetch("http://127.0.0.1:51527/hello")` 的精确执行轨迹：

```text
[TRACE +0x30465e9] WSASocketW(af=2, type=1, proto=0, info=0, g=0, flags=0x81) -> 0x114 (err=0)
[TRACE +0x3046664] ioctlsocket(s=0x114, cmd=0x8004667e [FIONBIO], arg=1) -> 0 (err=0)
[TRACE +0x3046765] WSAIoctl(s=0x114, code=0x98000011 [SIO_TCP_INITIAL_RTO], inlen=4, outlen=0) -> -1 (err=10045)
[TRACE +0x304679c] connect(s=0x114, namelen=16) -> -1 (err=10035 WSAEWOULDBLOCK)
[TRACE +0x3b5eb70] setsockopt(s=0x114, level=0x6 [IPPROTO_TCP], optname=0x1 [TCP_NODELAY], val=1) -> 0 (err=0)
[TRACE +0x48d6c2]  ioctlsocket(s=0x114, cmd=0x8004667e [FIONBIO], arg=1) -> 0 (err=0)
[TRACE +0x48d703]  WSAIoctl(s=0x114, code=0x48000022 [SIO_BASE_HANDLE], inlen=0, outlen=8) -> 0 (err=0)
[TRACE +0x48d772]  getsockopt(s=0x114, level=0xffff, optname=0x2005 [SO_PROTOCOL_INFOW], outlen=628) -> 0 (err=0)
[TRACE +0x48d838]  WSASocketW(af=2, type=1, proto=6, info=0x4EB4DE10, g=0, flags=0x81) -> 0x124 (err=0)
[TRACE +0x3b5f73b] recv(s=0x114, len=0, flags=0x20) -> -1 (err=10045 WSAEOPNOTSUPP)   <-- 【致命故障点】
[TRACE +0x3b5f991] getsockopt(s=0x114, level=0xffff, optname=0x1007 [SO_ERROR], outval=0) -> 0 (err=0)
[TRACE +0x20eeb9d] setsockopt(s=0x114, level=0xffff, optname=0x80 [SO_DONTLINGER], val=1) -> 0 (err=0)
[TRACE +0x3428419] closesocket(s=0x114) -> 0 (err=0)                                    <-- 【触发 TCP RST】
```

### 3.1 根因一（直接致死点）：Bun 的 `uSockets` 在所有 `recv()` 调用中硬编码传入了 Win8+ 标志位 `MSG_PUSH_IMMEDIATE (0x20)`

#### （1）出站连接完成探测（`RVA 0x3b5f725`）
反汇编 `claude.exe` 的 `RVA 0x3b5f725`–`0x3b5f9ff`（`uSockets` 连接可写就绪回调）：
```asm
; RVA 0x3b5f725: 连接就绪后，通过 0 字节 recv 探测连接状态
03b5f725: 48 8b 4e 08               mov  rcx, qword ptr [rsi+8]     ; s = socket
03b5f729: 31 d2                     xor  edx, edx                   ; buf = NULL
03b5f72b: 45 31 c0                  xor  r8d, r8d                   ; len = 0
03b5f72e: 41 b9 20 00 00 00         mov  r9d, 0x20                  ; flags = 0x20 (MSG_PUSH_IMMEDIATE)
03b5f734: ff 15 9d 50 57 03         call qword ptr [WSOCK32!ord_16] ; recv(s, NULL, 0, 0x20)
03b5f73a: 83 f8 ff                  cmp  eax, -1
03b5f73d: 0f 84 ff 01 00 00         je   0x3b5f942                  ; 若返回 -1，跳至错误处理分支！
```
当 `recv(s, NULL, 0, 0x20)` 返回 `-1` 时，错误处理分支（`RVA 0x3b5f942`）逻辑如下：
```asm
03b5f942: ff 15 f7 4d 57 03         call qword ptr [WSOCK32!ord_111]; eax = WSAGetLastError()
03b5f948: 3d 14 27 00 00            cmp  eax, 10004                 ; WSAEINTR (0x2714) -> 视为正常
03b5f94d: 0f 84 f0 fd ff ff         je   0x3b5f743
03b5f953: 3d 33 27 00 00            cmp  eax, 10035                 ; WSAEWOULDBLOCK (0x2733) -> 视为连接正常！
03b5f958: 0f 84 e5 fd ff ff         je   0x3b5f743
03b5f95e: 89 c3                     mov  ebx, eax                   ; ebx = 10045 (WSAEOPNOTSUPP)
...
03b5f98a: ff 15 0f 4e 57 03         call qword ptr [WSOCK32!ord_7]  ; getsockopt(s, SOL_SOCKET, SO_ERROR, &so_error, &4)
03b5f995: 8b 45 fc                  mov  eax, dword ptr [rbp-4]     ; eax = so_error (此时握手已成功，故 so_error == 0)
...
03b5f9f9: 85 c0                     test eax, eax                   ; 检查 so_error > 0
03b5f9fb: 89 da                     mov  edx, ebx                   ; edx = ebx (10045 WSAEOPNOTSUPP)
03b5f9fd: 0f 4f d0                  cmovg edx, eax                  ; 仅当 so_error > 0 时取 so_error，否则保留 10045！
```
- **Windows 8/10/11 行为**：Windows 8 在 `ws2def.h` 中新增了 `MSG_PUSH_IMMEDIATE = 0x20`，`mswsock.dll` / `afd.sys` 允许该标志；非阻塞套接字上 `recv(s, NULL, 0, 0x20)` 返回 `-1` 且错误码为 `WSAEWOULDBLOCK (10035)`，命中 `0x3b5f953` 分支，连接顺利建立。
- **Windows 7 行为**：Windows 7 的 `mswsock.dll!WSPRecv` 仅支持 `MSG_OOB | MSG_PEEK | MSG_DONTROUTE | MSG_WAITALL | MSG_PARTIAL`（`0x01 | 0x02 | 0x04 | 0x08 | 0x8000`），遇到未知位 `0x20` 直接拒绝并返回 **`WSAEOPNOTSUPP (10045)`**！
- **连锁反应**：Bun 收到 `10045` 后，再查 `SO_ERROR` 得到 `0`（因为 TCP 握手实际上已经成功），`cmovg` 保留了 `10045`，将其映射为 `ENOTSUP` 抛给上层 JS，并在 `RVA 0x20eeb9d` 设置 `SO_LINGER` 后调用 `closesocket`（`RVA 0x3428419`），导致内核向代理或服务端发送 **TCP `RST`**！

#### （2）入站与常规数据读取（`RVA 0x2fe2052`）
在 `uSockets` 的常规读取函数 `bsd_recv`（`RVA 0x2fe2040`）中，Bun 同样硬编码传入了 `flags = 0x20`：
```text
[TRACE +0x2fe2052] recv(s=0x138, len=524288, flags=0x20) -> -1 (err=10045 WSAEOPNOTSUPP)
```
这导致 `Bun.serve()` 或 `node:net` 服务端在 `accept()` 客户端连接后，首次触发可读事件调用 `recv()` 时立即因 `flags=0x20` 收到 `WSAEOPNOTSUPP (10045)`，被转换为 `errno: -4049 (UV_ENOTSUP)` 抛出 `ENOTSUP: operation not supported on socket, read`。

---

### 3.2 根因二（兼容层穿透点）：Bun 通过延迟加载按序号导入 `WSOCK32.dll`，绕过了 `KxNet.dll`

解析 `claude.exe` 的延迟加载目录（`IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT`）发现，Bun 将 Winsock 调用拆成了两个 DLL：
1. **`WS2_32.dll`（17 个按名称导入函数）**：
   `FreeAddrInfoW`, `GetAddrInfoW`, `GetHostNameW`, `GetNameInfoW`, `WSADuplicateSocketW`, `WSAGetOverlappedResult`, `WSAIoctl`, `WSAPoll`, `WSARecv`, `WSARecvFrom`, `WSASend`, `WSASendTo`, `WSASocketW`, `freeaddrinfo`, `getaddrinfo`, `inet_ntop`, `inet_pton`。
2. **`WSOCK32.dll`（28 个按序号导入函数）**：
   - `Ord 1`: `accept`
   - `Ord 2`: `bind`
   - `Ord 3`: `closesocket`
   - `Ord 4`: `connect`
   - `Ord 5`: `getpeername`
   - `Ord 6`: `getsockname`
   - `Ord 7`: `getsockopt`
   - `Ord 12`: `ioctlsocket`
   - `Ord 13`: `listen`
   - `Ord 16`: `recv`
   - `Ord 17`: `recvfrom`
   - `Ord 19`: `send`
   - `Ord 21`: `setsockopt`
   - `Ord 23`: `socket`
   - `Ord 111`: `WSAGetLastError`
   - `Ord 112`: `WSASetLastError`
   - `Ord 115`: `WSAStartup`
   - `Ord 116`: `WSACleanup` 等。

剖析 Windows 7 系统自带的 `C:\Windows\System32\wsock32.dll`（以及 32 位 `SysWOW64\wsock32.dll`）导出表与导入表可知：
- `wsock32.dll` 与 `ws2_32.dll` 的序号 **不完全兼容**（`wsock32.dll` 中 `Ord 10=inet_addr, 11=inet_ntoa, 12=ioctlsocket`，而 `ws2_32.dll` 中 `Ord 10=ioctlsocket, 11=inet_addr, 12=inet_ntoa`），因此不能把 `wsock32.dll` 直接同名重定向为 `kxnet.dll`。
- `wsock32.dll` 除 4 个函数（`Ord 7 getsockopt`、`Ord 16 recv`、`Ord 17 recvfrom`、`Ord 21 setsockopt`）包含机器码外，其余导出全部是指向 `ws2_32.*` 或 `MSWSOCK.*` 的导出转发器（Forwarder）。
- 而 `wsock32.dll` 的那 4 个机器码函数，内部则是通过 `wsock32.dll` 自身的静态导入表调用 `WS2_32.dll` 的 4 个导入项：
  1. `WS2_32.dll!WSARecv`（被 `wsock32.dll!recv` 调用）
  2. `WS2_32.dll!WSARecvFrom`（被 `wsock32.dll!recvfrom` 调用）
  3. `WS2_32.dll!@21`（`setsockopt`，被 `wsock32.dll!setsockopt` 调用）
  4. `WS2_32.dll!@7`（`getsockopt`，被 `wsock32.dll!getsockopt` 调用）
- **为什么穿透了 VxKex NEXT**：在 [`KexDll/dllrewrt.c`](file:///D:/project/VxKex-NEXT/KexDll/dllrewrt.c#L578-L620) 中，`C:\Windows\System32\wsock32.dll` 位于系统目录，被 [`KexIsWindowsDll`](file:///D:/project/VxKex-NEXT/KexDll/dllrewrt.c#L401-L453) 判定为 Windows 系统 DLL，且未加入 [`KexIsRewriteForcedWindowsDll`](file:///D:/project/VxKex-NEXT/KexDll/dllrewrt.c#L578-L620) 白名单。因此当 `wsock32.dll` 被加载时，VxKex NEXT **没有重写 `wsock32.dll` 的静态导入表**，导致 `wsock32.dll` 直接绑定到了原生 `C:\Windows\System32\WS2_32.dll`，彻底绕过了 `KxNet.dll`！

---

### 3.3 根因三：`WSAIoctl` 缺少对 Win8+ `SIO_TCP_INITIAL_RTO` 等控制码的兼容处理

在 `claude.exe` 中，有两处代码调用了 Windows 8 新增的 `SIO_TCP_INITIAL_RTO (0x98000011)`：
1. `uSockets` 的 `bsd_connect_socket`（`RVA 0x3046755`）：连接 loopback 时设置 `SIO_TCP_INITIAL_RTO`；
2. `libuv` 的 `uv_tcp_connect`（`RVA 0x494560`）：设置 `SIO_TCP_INITIAL_RTO`。

在 Windows 7 下，原生 `WSAIoctl` 对 `0x98000011`（以及 `SIO_LOOPBACK_FAST_PATH = 0x98000010`）返回 `SOCKET_ERROR (-1)` 且错误码为 `WSAEOPNOTSUPP (10045)`。虽然 `uSockets` 在 `0x3046765` 未检查该返回值，但 `WSAGetLastError()` 被污染为 `10045`，且部分上层调用方若检查 `WSAIoctl` 返回值同样会触发 `WSAEOPNOTSUPP`。

---

## 4. 概念验证（PoC）实测结果

为验证上述根因分析的完备性，我们在不改动 `claude.exe` 二进制文件的前提下，通过 `bun:ffi` 在进程内对 `recv`（清除 `0x20` 标志位：`flags &= ~0x20`）和 `WSAIoctl`（对 `0x98000011` 返回 `0`）进行了挂钩验证：

### 4.1 实测一：Bun `fetch()` 请求本地 Node HTTP 服务
```text
[node-server] listening on 127.0.0.1:51667
[diag] Fetching http://127.0.0.1:51667/hello
[node-server] TCP connection accepted from 127.0.0.1 51668
[node-server] got request: GET /hello
[diag] Fetch success: 200 hello from node
```
- 修复前：`recv(s, len=0, flags=0x20)` 返回 `-1 (err=10045)`，抛出 `ENOTSUP` 并发送 `TCP RST`；
- 修复后：`recv(s, len=0, flags=0x20->0x0)` 返回 `-1 (err=10035 WSAEWOULDBLOCK)`，命中正常连接分支，成功发送 148 字节 HTTP 请求并接收 182 字节响应（`200 hello from node`）。

### 4.2 实测二：Bun 内部 `node:net` 服务端、`Bun.serve()` 服务端与 `fetch()` 自环测试
```text
[node:net server] listening on 51814
[node:net server] received: GET /from-fetch HTTP/1.1
[fetch -> node:net server] status: 200 body: BunServerOK!
[Bun.serve] listening on 51816
[fetch -> Bun.serve] status: 200 body: Hello from Bun.serve on Win7!
```
- 证明无论是 `node:net` 服务端还是 `Bun.serve` 本地 HTTP 服务端（如 OAuth 登录回调），在过滤 `flags &= ~0x20` 后均 100% 正常工作。

### 4.3 实测三：`claude.exe -p` 经本地 HTTP 代理请求远端 HTTPS 接口
- 成功向本地代理发送 96 字节 `CONNECT` 请求、接收 39 字节 `HTTP/1.1 200 Connection established`、完成 TLS 1.3 握手（发送 1482 字节 ClientHello、接收 3853+ 字节证书链）、发送加密业务请求并完整接收服务端 JSON 响应，全程无任何网络错误。

---

## 5. 相关文档链接

- 具体修复与代码实现方案见：[`docx/VxKex-NEXT修复Bun网络兼容性设计方案-20261101.md`](VxKex-NEXT修复Bun网络兼容性设计方案-20261101.md)

