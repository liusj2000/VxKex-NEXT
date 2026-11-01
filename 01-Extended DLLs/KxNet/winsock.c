#include "buildcfg.h"
#include "kxnetp.h"

//
// libuv (which is used by some random node.js package that a lot of electron
// apps, such as VSCode, Signal, etc. use,) depends on GetHostNameW and will
// shit itself if not found.
//
KXNETAPI INT WSAAPI GetHostNameW(
	OUT	PWSTR	Name,
	IN	INT		NameCch)
{
	INT ReturnValue;
	INT ConvertedCch;
	PCHAR NameAnsi;

	if (!NameCch) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}

	NameAnsi = StackAlloc(CHAR, NameCch);
	ReturnValue = gethostname(NameAnsi, NameCch);

	if (ReturnValue == SOCKET_ERROR) {
		return ReturnValue;
	}

	ConvertedCch = MultiByteToWideChar(
		CP_ACP,
		0,
		NameAnsi,
		-1,
		Name,
		NameCch);

	if (ConvertedCch == 0) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}

	return ReturnValue;
}

//
// Used for app-specific hack.
//
KXNETAPI INT WSAAPI Ext_getaddrinfo(
	IN	PCSTR					NodeName,
	IN	PCSTR					ServerName,
	IN	const struct addrinfo	*Hints,
	OUT	struct addrinfo			**Results)
{
	unless (KexData->IfeoParameters.DisableAppSpecific) {
		//
		// APPSPECIFICHACK: The game "Life is Strange: True Colors" attempts to connect
		// to a server and send some telemetry data. On Windows 7, the gathering of this
		// telemetry data fails. The error handling code in the game is faulty and will
		// cause a crash.
		//
		// As a workaround, we will simply fail "getaddrinfo" calls coming from the game, to
		// simulate the lack of an internet connection.
		//
		// For reference, the actual module that calls getaddrinfo with the game's
		// telemetry server is "osdk.dll", and the host name requested is
		// "see-siren.os.eidos.com".
		//

		if (AshExeBaseNameIs(L"Siren-Win64-Shipping.exe")) {
			return WSANO_DATA;
		}
	}

	return getaddrinfo(NodeName, ServerName, Hints, Results);
}

//
// The game osu!lazer fails to connect to the internet due to an unsupported
// socket option (SO_REUSE_UNICASTPORT) which is only available on newer
// versions of Windows. It can safely be ignored.
//
KXNETAPI INT WSAAPI Ext_setsockopt(
	IN	SOCKET	SocketHandle,
	IN	INT		Level,
	IN	INT		OptionId,
	IN	PCVOID	OptionBuffer,
	IN	INT		OptionCb)
{
	INT ReturnCode;
	INT ErrorCode;

	ReturnCode = setsockopt(
		SocketHandle,
		Level,
		OptionId,
		(PCCHAR) OptionBuffer,
		OptionCb);

	if (ReturnCode == SOCKET_ERROR) {
		ErrorCode = WSAGetLastError();
	}

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

	if (ReturnCode == SOCKET_ERROR) {
		KexLogWarningEvent(
			L"Windows Sockets function failed with an error code of %d\r\n\r\n"
			L"SocketHandle: 0x%p\r\n"
			L"Level:        %d\r\n"
			L"OptionId:     %d\r\n"
			L"OptionBuffer: 0x%p\r\n"
			L"OptionCb:     %d",
			ErrorCode,
			SocketHandle,
			Level,
			OptionId,
			OptionBuffer,
			OptionCb);

		KexDebugCheckpoint();
	}

	return ReturnCode;
}

KXNETAPI INT WSAAPI Ext_getsockopt(
	IN		SOCKET	SocketHandle,
	IN		INT		Level,
	IN		INT		OptionId,
	OUT		PVOID	OptionBuffer,
	IN OUT	PINT	OptionCb)
{
	INT ReturnCode;
	INT ErrorCode;

	ReturnCode = getsockopt(
		SocketHandle,
		Level,
		OptionId,
		(PCHAR) OptionBuffer,
		OptionCb);

	if (ReturnCode == SOCKET_ERROR) {
		ErrorCode = WSAGetLastError();
	}

	if (ReturnCode == SOCKET_ERROR &&
		ErrorCode == WSAENOPROTOOPT &&
		Level == SOL_SOCKET) {

		switch (OptionId) {
		case SO_REUSE_UNICASTPORT:
		case SO_REUSE_MULTICASTPORT:
			ASSERT (OptionCb != NULL);
			ASSERT (*OptionCb >= sizeof(ULONG));

			if (*OptionCb < sizeof(ULONG)) {
				return ReturnCode;
			}

			KexLogDebugEvent(L"Faking success (OptionId 0x%x)", OptionId);
			*(PULONG) OptionBuffer = 0;
			*OptionCb = sizeof(ULONG);
			ReturnCode = 0;
			break;
		}
	}

	if (ReturnCode == SOCKET_ERROR) {
		KexLogWarningEvent(
			L"Windows Sockets function failed with an error code of %d\r\n\r\n"
			L"SocketHandle: 0x%p\r\n"
			L"Level:        %d\r\n"
			L"OptionId:     %d\r\n"
			L"OptionBuffer: 0x%p\r\n"
			L"OptionCb:     %d",
			ErrorCode,
			SocketHandle,
			Level,
			OptionId,
			OptionBuffer,
			OptionCb);

		KexDebugCheckpoint();
	}

	return ReturnCode;
}

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
