# VxKex NEXT 修复 Bun 网络兼容性设计方案（2026-11-01）

## 1. 设计目标与适用范围

- **目标**：在 VxKex NEXT 兼容层（`KexDll.dll` + `KxNet.dll`）中原生解决 Bun 运行时（包括 `bun.exe`、Claude Code 原生版 `claude.exe`、OpenCode 原生版等）在 Windows 7 SP1 下的 `ENOTSUP` 报错与连接重置（`TCP RST` / `ECONNRESET`）问题，无需修改目标程序二进制或注入外部 JS 预加载脚本。
- **问题根因分析参见**：[`docx/Bun在Win7下ENOTSUP与网络失败根因逆向分析-20261101.md`](Bun在Win7下ENOTSUP与网络失败根因逆向分析-20261101.md)。
- **涉及模块与文件**：
  1. [`D:\project\VxKex-NEXT\KexDll\dllrewrt.c`](file:///D:/project/VxKex-NEXT/KexDll/dllrewrt.c)
  2. [`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\kxnetp.h`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/kxnetp.h)
  3. [`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\winsock.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/winsock.c)
  4. [`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\kxnet.def`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/kxnet.def)
  5. [`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\forwards.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/forwards.c)（注释掉已被 `kxnet.def` 实现的导出转发，防止链接器报 LNK4197 冲突）

---

## 2. 架构与关键设计决策

### 2.1 为什么通过 `KexIsRewriteForcedWindowsDll` 重写 `wsock32.dll` 导入表，而非将 `wsock32` 直接重定向到 `kxnet`？
- Windows 7 的 `wsock32.dll` 与 `ws2_32.dll` 导出序号在 `@10`、`@11`、`@12` 上定义相反：
  - `wsock32.dll`: `@10 = inet_addr`, `@11 = inet_ntoa`, `@12 = ioctlsocket`
  - `ws2_32.dll`: `@10 = ioctlsocket`, `@11 = inet_addr`, `@12 = inet_ntoa`
- 若在 `KexDll/redirects.h` 中将 `wsock32` 简单映射为 `kxnet`，按序号导入 `wsock32!@12`（`ioctlsocket`）的程序会错误调用到 `ws2_32!@12`（`inet_ntoa`），引发崩溃。
- 而在 Windows 7 的 `C:\Windows\System32\wsock32.dll`（及 `SysWOW64\wsock32.dll`）中：
  - `@16` (`recv`) 与 `@17` (`recvfrom`) 是实体包装函数，内部将 `flags` 组装为 `DWORD dwFlags = flags`，调用其静态导入的 `WS2_32.dll!WSARecv` 与 `WS2_32.dll!WSARecvFrom`；
  - `@21` (`setsockopt`) 与 `@7` (`getsockopt`) 也是实体包装函数，内部调用其静态导入的 `WS2_32.dll!@21` 与 `WS2_32.dll!@7`；
  - 其余函数均为指向 `ws2_32.*` 或 `MSWSOCK.*` 的导出转发器。
- 因此，只需在 [`KexIsRewriteForcedWindowsDll`](file:///D:/project/VxKex-NEXT/KexDll/dllrewrt.c#L578-L620) 中加入 `wsock32.dll`：
  1. 完全保留系统 `wsock32.dll` 原本的序号表（`@10/@11/@12` 顺序不乱）；
  2. 当 `wsock32.dll` 被映射到进程时，`KexDllNotificationCallback` 自动将其导入表中的 `WS2_32.dll` 重写为 `kxnet.dll`；
  3. 任何通过 `wsock32.dll` 调用的 `recv`、`recvfrom`、`setsockopt`、`getsockopt` 均自动进入 `KxNet.dll` 的扩展实现（`Ext_WSARecv`、`Ext_WSARecvFrom`、`Ext_setsockopt`、`Ext_getsockopt`）。

### 2.2 `recv` / `recvfrom` / `WSARecv` / `WSARecvFrom` 标志位净化设计
- Windows 8 引入了 `MSG_PUSH_IMMEDIATE (0x20)`。Bun 的 `uSockets` 在 Windows 下对所有 `recv()`（包括 `connect` 完成后的 0 字节探测 `recv(s, NULL, 0, 0x20)` 以及常规数据读取 `recv(s, buf, 524288, 0x20)`）均硬编码传入了 `0x20`。
- Windows 7 的 `mswsock.dll!WSPRecv` 不识别 `0x20`，直接报错 `WSAEOPNOTSUPP (10045)`。
- 在 `KxNet.dll` 中导出并拦截：
  - `recv = Ext_recv @16`
  - `recvfrom = Ext_recvfrom @17`
  - `WSARecv = Ext_WSARecv @87`
  - `WSARecvFrom = Ext_WSARecvFrom @89`
  （注：32 位与 64 位 `ws2_32.dll` 的上述 4 个序号完全一致）。
- 在调用原生 `recv` / `recvfrom` / `WSARecv` / `WSARecvFrom` 之前，统一清除传入标志中的 `MSG_PUSH_IMMEDIATE (0x20)` 位（`Flags &= ~MSG_PUSH_IMMEDIATE`）。

### 2.3 `WSAIoctl` 与 `setsockopt` 的 Win8+/Win10+ 选项兼容设计
- 在 `KxNet.dll` 中导出并拦截 `WSAIoctl = Ext_WSAIoctl @74`：
  - 先调用原生 `WSAIoctl`；
  - 若返回 `SOCKET_ERROR` 且 `WSAGetLastError()` 为 `WSAEOPNOTSUPP (10045)` 或 `WSAEINVAL (10022)`，当 `dwIoControlCode` 为 `SIO_TCP_INITIAL_RTO (0x98000011)` 或 `SIO_LOOPBACK_FAST_PATH (0x98000010)` 时，若 `lpcbBytesReturned != NULL` 则置 `*lpcbBytesReturned = 0`，调用 `WSASetLastError(0)` 并返回 `0`。
- 在 [`Ext_setsockopt`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/winsock.c#L82-L135) 中补充 `Level == IPPROTO_TCP` 的高版本选项兜底：
  - 当 `ReturnCode == SOCKET_ERROR` 且 `ErrorCode == WSAENOPROTOOPT || ErrorCode == WSAEINVAL` 时，若 `Level == IPPROTO_TCP` 且 `OptionId` 为 `TCP_FASTOPEN (15)`、`TCP_KEEPCNT (16)`、`TCP_KEEPINTVL (17)`、`TCP_FAIL_CONNECT_ON_ICMP_ERROR (18)`，伪装成功并返回 `0`。

---

## 3. 具体代码修改清单（供直接执行）

### 3.1 修改文件一：`D:\project\VxKex-NEXT\KexDll\dllrewrt.c`

定位 [`KexIsRewriteForcedWindowsDll`](file:///D:/project/VxKex-NEXT/KexDll/dllrewrt.c#L578-L620)（第 578–620 行），将原代码：
```c
KEXAPI BOOLEAN NTAPI KexIsRewriteForcedWindowsDll(
	IN	PCUNICODE_STRING	FullDllName,
	IN	PCUNICODE_STRING	BaseDllName)
{
	UNICODE_STRING WebIo;
	UNICODE_STRING WinInet;
	UNICODE_STRING SspiCli;
	UNICODE_STRING GdiPlus;

	//
	// If the DLL is WebIO (used by WinHTTP) or WinInet, then we will rewrite
	// its imports so that schannel.dll and secur32.dll get caught.
	// This is to enable support for KxSChanl and TLS 1.3.
	//

	RtlInitConstantUnicodeString(&WebIo, L"webio.dll");
	RtlInitConstantUnicodeString(&WinInet, L"wininet.dll");

	//
	// We will rewrite SSPICLI so that we can redirect the SecurityProviders
	// registry value to our own (see Ext_RegQueryValueExW in KxAdvapi).
	//

	RtlInitConstantUnicodeString(&SspiCli, L"sspicli.dll");

	//
	// This is used to fix a bug in GDI+ which caused GdipCreateFontFromLogfontA/W
	// failed with NotTrueTypeFont error and breaks any text rendering after.
	// See Ext_LoadLibraryExW in KxBase\module.c.
	//

	RtlInitConstantUnicodeString(&GdiPlus, L"GdiPlus.dll");

	if (RtlEqualUnicodeString(BaseDllName, &WebIo, TRUE) ||
		RtlEqualUnicodeString(BaseDllName, &WinInet, TRUE) ||
		RtlEqualUnicodeString(BaseDllName, &SspiCli, TRUE) ||
		RtlEqualUnicodeString(BaseDllName, &GdiPlus, TRUE)) {

		return TRUE;
	}

	return FALSE;
}
```
替换为：
```c
KEXAPI BOOLEAN NTAPI KexIsRewriteForcedWindowsDll(
	IN	PCUNICODE_STRING	FullDllName,
	IN	PCUNICODE_STRING	BaseDllName)
{
	UNICODE_STRING WebIo;
	UNICODE_STRING WinInet;
	UNICODE_STRING SspiCli;
	UNICODE_STRING GdiPlus;
	UNICODE_STRING Wsock32;

	//
	// If the DLL is WebIO (used by WinHTTP) or WinInet, then we will rewrite
	// its imports so that schannel.dll and secur32.dll get caught.
	// This is to enable support for KxSChanl and TLS 1.3.
	//

	RtlInitConstantUnicodeString(&WebIo, L"webio.dll");
	RtlInitConstantUnicodeString(&WinInet, L"wininet.dll");

	//
	// We will rewrite SSPICLI so that we can redirect the SecurityProviders
	// registry value to our own (see Ext_RegQueryValueExW in KxAdvapi).
	//

	RtlInitConstantUnicodeString(&SspiCli, L"sspicli.dll");

	//
	// This is used to fix a bug in GDI+ which caused GdipCreateFontFromLogfontA/W
	// failed with NotTrueTypeFont error and breaks any text rendering after.
	// See Ext_LoadLibraryExW in KxBase\module.c.
	//

	RtlInitConstantUnicodeString(&GdiPlus, L"GdiPlus.dll");

	//
	// Rewrite wsock32.dll imports so that its internal calls to WS2_32.dll
	// (WSARecv, WSARecvFrom, setsockopt, getsockopt) go through KxNet.dll.
	// Runtimes such as Bun/uSockets delay-load wsock32.dll by ordinal.
	//

	RtlInitConstantUnicodeString(&Wsock32, L"wsock32.dll");

	if (RtlEqualUnicodeString(BaseDllName, &WebIo, TRUE) ||
		RtlEqualUnicodeString(BaseDllName, &WinInet, TRUE) ||
		RtlEqualUnicodeString(BaseDllName, &SspiCli, TRUE) ||
		RtlEqualUnicodeString(BaseDllName, &GdiPlus, TRUE) ||
		RtlEqualUnicodeString(BaseDllName, &Wsock32, TRUE)) {

		return TRUE;
	}

	return FALSE;
}
```

---

### 3.2 修改文件二：`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\kxnetp.h`

定位 [`01-Extended DLLs/KxNet/kxnetp.h`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/kxnetp.h#L9-L15)（第 9–15 行），将原代码：
```c
// For setsockopt and getsockopt.
// These values are not supported on Win7.
#define SO_REUSE_UNICASTPORT	0x3007
#define SO_REUSE_MULTICASTPORT	0x3008
#define SO_ORIGINAL_DST			0x300F
```
替换为：
```c
// For setsockopt, getsockopt, recv, WSARecv, and WSAIoctl.
// These values are only supported on Win8/Win10+ and fail on Win7.
#define SO_REUSE_UNICASTPORT			0x3007
#define SO_REUSE_MULTICASTPORT			0x3008
#define SO_ORIGINAL_DST					0x300F

#ifndef MSG_PUSH_IMMEDIATE
#  define MSG_PUSH_IMMEDIATE			0x20
#endif

#ifndef SIO_LOOPBACK_FAST_PATH
#  define SIO_LOOPBACK_FAST_PATH		0x98000010
#endif

#ifndef SIO_TCP_INITIAL_RTO
#  define SIO_TCP_INITIAL_RTO			0x98000011
#endif

#ifndef TCP_FASTOPEN
#  define TCP_FASTOPEN					15
#endif

#ifndef TCP_KEEPCNT
#  define TCP_KEEPCNT					16
#endif

#ifndef TCP_KEEPINTVL
#  define TCP_KEEPINTVL					17
#endif

#ifndef TCP_FAIL_CONNECT_ON_ICMP_ERROR
#  define TCP_FAIL_CONNECT_ON_ICMP_ERROR 18
#endif
```

---

### 3.3 修改文件三：`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\winsock.c`

在 [`01-Extended DLLs/KxNet/winsock.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/winsock.c) 中：
1. 在 `Ext_setsockopt`（第 103–115 行）补充 `IPPROTO_TCP` 高版本选项兼容；
2. 在文件末尾新增 `Ext_recv`、`Ext_recvfrom`、`Ext_WSARecv`、`Ext_WSARecvFrom`、`Ext_WSAIoctl` 五个函数。

`Ext_setsockopt` 更新及新增函数完整代码如下：
```c
	if (ReturnCode == SOCKET_ERROR &&
		ErrorCode == WSAENOPROTOOPT &&
		Level == SOL_SOCKET) {

		switch (OptionId) {
		case SO_REUSE_UNICASTPORT:
		case SO_REUSE_MULTICASTPORT:
			KexLogDebugEvent(L"Faking success (OptionId 0x%x)", OptionId);
			WSASetLastError(0);
			ReturnCode = 0;
			break;
		}
	}

	if (ReturnCode == SOCKET_ERROR &&
		(ErrorCode == WSAENOPROTOOPT || ErrorCode == WSAEINVAL) &&
		Level == IPPROTO_TCP) {

		switch (OptionId) {
		case TCP_FASTOPEN:
		case TCP_KEEPCNT:
		case TCP_KEEPINTVL:
		case TCP_FAIL_CONNECT_ON_ICMP_ERROR:
			KexLogDebugEvent(L"Faking success for IPPROTO_TCP option %d", OptionId);
			WSASetLastError(0);
			ReturnCode = 0;
			break;
		}
	}
```
在文件末尾追加：
```c
//
// Bun (uSockets) passes MSG_PUSH_IMMEDIATE (0x20, added in Windows 8) to recv().
// On Windows 7, mswsock.dll rejects bit 0x20 with WSAEOPNOTSUPP (10045), causing
// Bun to abort every socket with ENOTSUP and TCP RST. Strip MSG_PUSH_IMMEDIATE
// before forwarding to the native Winsock implementation.
//
KXNETAPI INT WSAAPI Ext_recv(
	IN	SOCKET	SocketHandle,
	OUT	PCHAR	Buffer,
	IN	INT		BufferLength,
	IN	INT		Flags)
{
	return recv(
		SocketHandle,
		Buffer,
		BufferLength,
		Flags & ~MSG_PUSH_IMMEDIATE);
}

KXNETAPI INT WSAAPI Ext_recvfrom(
	IN		SOCKET				SocketHandle,
	OUT		PCHAR				Buffer,
	IN		INT					BufferLength,
	IN		INT					Flags,
	OUT		struct sockaddr		*FromAddress OPTIONAL,
	IN OUT	PINT				FromAddressLength OPTIONAL)
{
	return recvfrom(
		SocketHandle,
		Buffer,
		BufferLength,
		Flags & ~MSG_PUSH_IMMEDIATE,
		FromAddress,
		FromAddressLength);
}

KXNETAPI INT WSAAPI Ext_WSARecv(
	IN		SOCKET								SocketHandle,
	IN OUT	LPWSABUF							Buffers,
	IN		DWORD								BufferCount,
	OUT		LPDWORD								NumberOfBytesRecvd OPTIONAL,
	IN OUT	LPDWORD								Flags,
	IN OUT	LPWSAOVERLAPPED						Overlapped OPTIONAL,
	IN		LPWSAOVERLAPPED_COMPLETION_ROUTINE	CompletionRoutine OPTIONAL)
{
	DWORD LocalFlags;

	if (Flags != NULL) {
		*Flags &= ~((DWORD) MSG_PUSH_IMMEDIATE);
	} else {
		LocalFlags = 0;
		Flags = &LocalFlags;
	}

	return WSARecv(
		SocketHandle,
		Buffers,
		BufferCount,
		NumberOfBytesRecvd,
		Flags,
		Overlapped,
		CompletionRoutine);
}

KXNETAPI INT WSAAPI Ext_WSARecvFrom(
	IN		SOCKET								SocketHandle,
	IN OUT	LPWSABUF							Buffers,
	IN		DWORD								BufferCount,
	OUT		LPDWORD								NumberOfBytesRecvd OPTIONAL,
	IN OUT	LPDWORD								Flags,
	OUT		struct sockaddr						*FromAddress OPTIONAL,
	IN OUT	LPINT								FromAddressLength OPTIONAL,
	IN OUT	LPWSAOVERLAPPED						Overlapped OPTIONAL,
	IN		LPWSAOVERLAPPED_COMPLETION_ROUTINE	CompletionRoutine OPTIONAL)
{
	DWORD LocalFlags;

	if (Flags != NULL) {
		*Flags &= ~((DWORD) MSG_PUSH_IMMEDIATE);
	} else {
		LocalFlags = 0;
		Flags = &LocalFlags;
	}

	return WSARecvFrom(
		SocketHandle,
		Buffers,
		BufferCount,
		NumberOfBytesRecvd,
		Flags,
		FromAddress,
		FromAddressLength,
		Overlapped,
		CompletionRoutine);
}

//
// Emulate Windows 8+ SIO_TCP_INITIAL_RTO (0x98000011) and
// SIO_LOOPBACK_FAST_PATH (0x98000010) which fail with WSAEOPNOTSUPP on Win7.
//
KXNETAPI INT WSAAPI Ext_WSAIoctl(
	IN		SOCKET								SocketHandle,
	IN		DWORD								IoControlCode,
	IN		LPVOID								InBuffer OPTIONAL,
	IN		DWORD								InBufferCb,
	OUT		LPVOID								OutBuffer OPTIONAL,
	IN		DWORD								OutBufferCb,
	OUT		LPDWORD								BytesReturned,
	IN OUT	LPWSAOVERLAPPED						Overlapped OPTIONAL,
	IN		LPWSAOVERLAPPED_COMPLETION_ROUTINE	CompletionRoutine OPTIONAL)
{
	INT ReturnCode;
	INT ErrorCode;

	ReturnCode = WSAIoctl(
		SocketHandle,
		IoControlCode,
		InBuffer,
		InBufferCb,
		OutBuffer,
		OutBufferCb,
		BytesReturned,
		Overlapped,
		CompletionRoutine);

	if (ReturnCode == SOCKET_ERROR) {
		ErrorCode = WSAGetLastError();

		if ((ErrorCode == WSAEOPNOTSUPP || ErrorCode == WSAEINVAL) &&
			(IoControlCode == SIO_TCP_INITIAL_RTO ||
			 IoControlCode == SIO_LOOPBACK_FAST_PATH)) {

			if (BytesReturned != NULL) {
				*BytesReturned = 0;
			}

			WSASetLastError(0);
			return 0;
		}

		WSASetLastError(ErrorCode);
	}

	return ReturnCode;
}
```

---

### 3.4 修改文件四：`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\kxnet.def`

定位 [`01-Extended DLLs/KxNet/kxnet.def`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/kxnet.def#L25-L32)（第 25–32 行），补充 5 个导出项（务必携带与 `ws2_32.dll` 严格一致的序号 `@16`、`@17`、`@74`、`@87`、`@89`）：
```def
	GetHostNameW
	getaddrinfo							= Ext_getaddrinfo		@178
	GetAddrInfoExW						= Ext_GetAddrInfoExW	@29
	GetAddrInfoExCancel
	GetAddrInfoExOverlappedResult
	setsockopt							= Ext_setsockopt		@21
	getsockopt							= Ext_getsockopt		@7
	recv								= Ext_recv				@16
	recvfrom							= Ext_recvfrom			@17
	WSAIoctl							= Ext_WSAIoctl			@74
	WSARecv								= Ext_WSARecv			@87
	WSARecvFrom							= Ext_WSARecvFrom		@89
	WSASetLastError						= Stub_WSASetLastError	@112
```

---

### 3.5 修改文件五：`D:\project\VxKex-NEXT\01-Extended DLLs\KxNet\forwards.c`

在 [`01-Extended DLLs/KxNet/forwards.c`](file:///D:/project/VxKex-NEXT/01-Extended%20DLLs/KxNet/forwards.c) 中，注释掉已被 `kxnet.def` 接管实现的 5 个导出转发宏（与既有的 `setsockopt`、`getsockopt` 保持一致），防止 MSVC 链接器报告 `LNK4197: export specified multiple times` 警告并因 `WX` 导致构建失败：
```c
// Line 311:
//#pragma comment(linker, "/EXPORT:WSAIoctl=ws2_32.WSAIoctl,@74")

// Line 325, 327:
//#pragma comment(linker, "/EXPORT:WSARecv=ws2_32.WSARecv,@87")
//#pragma comment(linker, "/EXPORT:WSARecvFrom=ws2_32.WSARecvFrom,@89")

// Line 437, 438:
//#pragma comment(linker, "/EXPORT:recv=ws2_32.recv,@16")
//#pragma comment(linker, "/EXPORT:recvfrom=ws2_32.recvfrom,@17")
```

---

## 4. 编译、打包与测试验证步骤

### 4.1 编译与打包命令
直接调用仓库根目录的 [`D:\project\VxKex-NEXT\build-installer.cmd`](file:///D:/project/VxKex-NEXT/build-installer.cmd)（已内置 `/p:TrackFileAccess=false /m:1`，会自动编译 Win32/x64 的 `KexDll.dll`、`KxNet.dll` 并生成 `D:\project\VxKex-NEXT\KexSetup_Release.exe`）：
```bat
cmd.exe /c "D:\project\VxKex-NEXT\build-installer.cmd"
```

### 4.2 导出表静态校验
实测验证通过，x64 与 Win32 产物导出表均 100% 具备：
- `getsockopt` (`@7`)
- `recv` (`@16`)
- `recvfrom` (`@17`)
- `setsockopt` (`@21`)
- `WSAIoctl` (`@74`)
- `WSARecv` (`@87`)
- `WSARecvFrom` (`@89`)
- `WSASetLastError` (`@112`)
- `getaddrinfo` (`@178`)

### 4.3 构建产物与哈希
- 全量安装包：`D:\project\VxKex-NEXT\KexSetup_Release.exe`（5,035,685 字节，SHA256: `e173e24a9ecbef1f4bdd72d4ea1aaad37310bc173c9a6ef6ac1960d46b930dfa`）
- 时间戳备份：`D:\project\VxKex-NEXT\KexSetup_Release_1.2.3.2463_20261101_1605.exe`
- x64 KxNet.dll：`D:\project\VxKex-NEXT\x64\Release\KxNet.dll`（SHA256: `08c7b658a5d0b6693ec1b44bbb59b51ae315741c2e66427b211e9f9b6d6ba87b`）
- x64 KexDll.dll：`D:\project\VxKex-NEXT\x64\Release\KexDll.dll`（SHA256: `4c0241dc0f275f2460f8c2f38201efcd88617610116d0800961c57eb81c0f5d1`）
- Win32 KxNet.dll：`D:\project\VxKex-NEXT\Release\KxNet.dll`（SHA256: `0dacc7b9676afda9522b91631f33559c112ff5db5211a56de594d439bfef7752`）
- Win32 KexDll.dll：`D:\project\VxKex-NEXT\Release\KexDll.dll`（SHA256: `50d1a0073b7f1a9cf85e04bca72a0666c0c2bd667d82c8b78695be58c4d77695`）

