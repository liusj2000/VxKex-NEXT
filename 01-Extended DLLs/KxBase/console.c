///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     console.c
//
// Abstract:
//
//     Console functions.
//
//     A bit of background information: Console "handles" are not real kernel
//     handles on Windows 7 (they are on Windows 10 though). They are a
//     separate type of handle which only works with console functions.
//
//     Console handles always have the least significant bit set, while of course,
//     kernel handles are always divisible by 4. Therefore it is possible to
//     quickly determine what is a console handle and what is not.
//
// Author:
//
//     vxiiduu (29-Apr-2026)
//
// Environment:
//
//     Win32
//
// Revision History:
//
//     vxiiduu              29-Apr-2026  Initial creation.
//                                       Add Ext_GetConsoleTitleA/W and
//                                       Ext_GetConsoleOriginalTitleA/W
//     vxiiduu              04-May-2026  Add initial ANSI escape sequence support.
//
///////////////////////////////////////////////////////////////////////////////

#include "buildcfg.h"
#include "kxbasep.h"

#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x4
#define KX_VT_OUTPUT_MASK (0x0004 | 0x0008 | 0x0200)
#define KX_VT_INPUT_MASK 0x0200
RTL_SRWLOCK KxPseudoConsoleLock = RTL_SRWLOCK_INIT;
STATIC PKEX_PSEUDOCONSOLE KxPseudoConsoles;

typedef struct _KX_CONSOLE_MODE_ENTRY {
	HANDLE Handle;
	ULONG VirtualMode;
} KX_CONSOLE_MODE_ENTRY;

STATIC RTL_SRWLOCK KxConsoleModeLock = RTL_SRWLOCK_INIT;
STATIC KX_CONSOLE_MODE_ENTRY KxConsoleModes[128];

STATIC BOOLEAN KxBasepHasEmulatedAnsiOutputLocked(
	IN	HANDLE	ConsoleHandle OPTIONAL)
{
	ULONG Index;

	for (Index = 0; Index < ARRAYSIZE(KxConsoleModes); ++Index) {
		if (!KxConsoleModes[Index].Handle) {
			continue;
		}
		if (ConsoleHandle && KxConsoleModes[Index].Handle != ConsoleHandle) {
			continue;
		}
		if (KxConsoleModes[Index].VirtualMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
			return TRUE;
		}
	}

	return FALSE;
}

STATIC BOOLEAN KxBasepShouldProcessConsoleAnsi(
	IN	HANDLE	ConsoleHandle)
{
	BOOLEAN Enabled;

	if (!BaseIsConsoleAnsiSupportEnabled(ConsoleHandle)) {
		return FALSE;
	}

	RtlAcquireSRWLockShared(&KxConsoleModeLock);
	Enabled = KxBasepHasEmulatedAnsiOutputLocked(ConsoleHandle);
	RtlReleaseSRWLockShared(&KxConsoleModeLock);

	return Enabled;
}

KXBASEAPI BOOL WINAPI Ext_CloseHandle(IN HANDLE Handle)
{
	BOOL Success;
	ULONG Index;
	BOOLEAN RemovedAnsiOutput;

	Success = CloseHandle(Handle);
	if (Success && Handle && Handle != INVALID_HANDLE_VALUE) {
		RemovedAnsiOutput = FALSE;
		RtlAcquireSRWLockExclusive(&KxConsoleModeLock);
		for (Index = 0; Index < ARRAYSIZE(KxConsoleModes); ++Index) {
			if (KxConsoleModes[Index].Handle == Handle) {
				if (KxConsoleModes[Index].VirtualMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
					RemovedAnsiOutput = TRUE;
				}
				RtlZeroMemory(&KxConsoleModes[Index], sizeof(KxConsoleModes[Index]));
				break;
			}
		}
		if (RemovedAnsiOutput && !KxBasepHasEmulatedAnsiOutputLocked(NULL)) {
			BaseDisableConsoleAnsiSupport(Handle);
		}
		RtlReleaseSRWLockExclusive(&KxConsoleModeLock);
	}
	return Success;
}
#ifndef PSEUDOCONSOLE_INHERIT_CURSOR
#  define PSEUDOCONSOLE_INHERIT_CURSOR 0x1
#endif

BOOL KxBasepIsPseudoConsoleHandle(
	IN	HPCON				PseudoConsoleHandle,
	OUT	PKEX_PSEUDOCONSOLE	*PseudoConsole OPTIONAL)
{
	PKEX_PSEUDOCONSOLE LocalPseudoConsole;

	if (!PseudoConsoleHandle) {
		return FALSE;
	}

	// Only dereference objects registered by CreatePseudoConsole. The caller
	// holds the lifetime lock, so Close cannot free an object during this walk.
	for (LocalPseudoConsole = KxPseudoConsoles; LocalPseudoConsole;
		 LocalPseudoConsole = LocalPseudoConsole->Next) {
		if ((HPCON) LocalPseudoConsole == PseudoConsoleHandle) break;
	}
	if (!LocalPseudoConsole || LocalPseudoConsole->Magic != KEX_PSEUDOCONSOLE_MAGIC) {
		return FALSE;
	}

	if (ARGUMENT_PRESENT(PseudoConsole)) {
		*PseudoConsole = LocalPseudoConsole;
	}

	return TRUE;
}

KXBASEAPI BOOL WINAPI Ext_WriteConsoleA(
	IN	HANDLE	ConsoleHandle,
	IN	PCVOID	Buffer,
	IN	ULONG	CchToWrite,
	OUT	PULONG	CchWritten OPTIONAL,
	IN	PVOID	Reserved OPTIONAL)
{
	if (KxBasepShouldProcessConsoleAnsi(ConsoleHandle)) {
		return WriteConsoleWithEscapeSequencesAorW(
			ConsoleHandle,
			Buffer,
			CchToWrite,
			CchWritten,
			Reserved,
			FALSE);
	}

	return WriteConsoleA(
		ConsoleHandle,
		Buffer,
		CchToWrite,
		CchWritten,
		Reserved);
}

KXBASEAPI BOOL WINAPI Ext_WriteConsoleW(
	IN	HANDLE	ConsoleHandle,
	IN	PCVOID	Buffer,
	IN	ULONG	CchToWrite,
	OUT	PULONG	CchWritten OPTIONAL,
	IN	PVOID	Reserved OPTIONAL)
{
	if (KxBasepShouldProcessConsoleAnsi(ConsoleHandle)) {
		return WriteConsoleWithEscapeSequencesAorW(
			ConsoleHandle,
			Buffer,
			CchToWrite,
			CchWritten,
			Reserved,
			TRUE);
	}

	return WriteConsoleW(
		ConsoleHandle,
		Buffer,
		CchToWrite,
		CchWritten,
		Reserved);
}

KXBASEAPI BOOL WINAPI Ext_GetConsoleMode(
	IN	HANDLE	ConsoleHandle,
	OUT	PULONG	Mode)
{
	BOOL Success;
	ULONG Index;

	Success = GetConsoleMode(ConsoleHandle, Mode);

	if (Success) {
		RtlAcquireSRWLockShared(&KxConsoleModeLock);
		for (Index = 0; Index < ARRAYSIZE(KxConsoleModes); ++Index) {
			if (KxConsoleModes[Index].Handle == ConsoleHandle) {
				*Mode |= KxConsoleModes[Index].VirtualMode;
				break;
			}
		}
		RtlReleaseSRWLockShared(&KxConsoleModeLock);
	}

	return Success;
}

KXBASEAPI BOOL WINAPI Ext_SetConsoleMode(
	IN	HANDLE	ConsoleHandle,
	IN	ULONG	Mode)
{
	BOOL Success;
	BOOLEAN Output;
	ULONG Mask;
	ULONG Index;
	ULONG Slot;
	ULONG LastError;
	BOOL Emulated;

	LastError = GetLastError();
	Output = IsConsoleOutputHandle(ConsoleHandle);
	Mask = Output ? KX_VT_OUTPUT_MASK : KX_VT_INPUT_MASK;
	Emulated = FALSE;
	RtlAcquireSRWLockExclusive(&KxConsoleModeLock);
	Slot = ARRAYSIZE(KxConsoleModes);
	for (Index = 0; Index < ARRAYSIZE(KxConsoleModes); ++Index) {
		if (KxConsoleModes[Index].Handle == ConsoleHandle) {
			Slot = Index;
			break;
		}
		if (!KxConsoleModes[Index].Handle && Slot == ARRAYSIZE(KxConsoleModes)) Slot = Index;
	}
	if ((Mode & Mask) && Slot == ARRAYSIZE(KxConsoleModes)) {
		RtlReleaseSRWLockExclusive(&KxConsoleModeLock);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return FALSE;
	}
	Success = SetConsoleMode(ConsoleHandle, Mode);
	if (!Success && GetLastError() == ERROR_INVALID_PARAMETER && (Mode & Mask)) {
		Success = SetConsoleMode(ConsoleHandle, Mode & ~Mask);
		Emulated = Success;
	}

	if (Success) {
		if (Slot != ARRAYSIZE(KxConsoleModes)) {
			KxConsoleModes[Slot].Handle = (Mode & Mask) ? ConsoleHandle : NULL;
			KxConsoleModes[Slot].VirtualMode = Emulated ? (Mode & Mask) : 0;
		}
		if (Output) {
			if (Emulated && (Mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
				BaseEnableConsoleAnsiSupport(ConsoleHandle);
			} else if (!KxBasepHasEmulatedAnsiOutputLocked(NULL)) {
				BaseDisableConsoleAnsiSupport(ConsoleHandle);
			}
		}
	} else {
		LastError = GetLastError();
	}
	RtlReleaseSRWLockExclusive(&KxConsoleModeLock);
	SetLastError(LastError);

	return Success;
}

KXBASEAPI BOOL WINAPI Ext_FreeConsole(
	VOID)
{
	RtlAcquireSRWLockExclusive(&KxConsoleModeLock);
	RtlZeroMemory(KxConsoleModes, sizeof(KxConsoleModes));
	RtlReleaseSRWLockExclusive(&KxConsoleModeLock);
	BaseDisableConsoleAnsiSupport(GetStdHandle(STD_OUTPUT_HANDLE));
	return FreeConsole();
}

STATIC BOOL IsConsoleAttached(
	VOID)
{
	return (GetConsoleWindow() != NULL);
}

//
// The GetConsoleTitle family of functions returns 0 on error, and according
// to MS documentation, it is supposed to also set the last-error code to
// indicate the nature of the error. However, this is not always true.
//
// When GetConsoleTitle is called in the situation where a program is not
// attached to a console, it returns 0 and leaves the buffer unmodified.
// The last-error code in this case varies significantly across different
// versions of Windows:
//
//   NT4, NT5.2 and Vista: ERROR_INVALID_ACCESS
//   Windows 7: <Not modified>
//   Windows 8.1: ERROR_NOT_SUPPORTED
//   Windows 10: ERROR_SEM_NOT_FOUND
//
// It is clear that Microsoft didn't bother to definitively set an error
// code for this particular edge case. In the case of Windows 7, we are
// particularly unlucky because GetConsoleTitle does not change the previous
// value of the last-error code, which means that the value applications see
// is effectively undefined, and is often ERROR_SUCCESS.
//
// This is an ambiguous result, because when GetConsoleTitle is called when
// the application is attached to a console with a blank title, it also returns
// 0 and sets ERROR_SUCCESS. The difference is that it also places a null
// terminator in the caller-supplied buffer.
//
// - Electron uses the libuv library (statically linked).
// - Libuv contains a function which is called uv_get_process_title.
// - Before 05/02/2026, uv_get_process_title called GetConsoleTitleW and
//   simply checked for a zero return value to indicate an error.
// - Between 15/06/2025 and 05/02/2026, uv_get_process_title would not only
//   check for a zero return value, but it would also check that GetLastError()
//   does *not* return zero, as follows:
//
//   static int uv__get_process_title(void) {
//     WCHAR title_w[MAX_TITLE_LENGTH];
//     DWORD wlen;
//     DWORD err;
//
//     SetLastError(ERROR_SUCCESS);
//     wlen = GetConsoleTitleW(title_w, sizeof(title_w) / sizeof(WCHAR));
//     if (wlen == 0) {
//       err = GetLastError();
//       if (err != 0)
//         return uv_translate_sys_error(err);
//     }
//
//     return uv__convert_utf16_to_utf8(title_w, wlen, &process_title);
//   }
//
// - For these specific builds of libuv, calling uv_get_process_title when
//   the process is not attached to a console will result in libuv thinking
//   that GetConsoleTitle has succeeded, when in fact, it has not.
// - libuv will then proceed to treat the undefined contents of the title_w
//   stack buffer as a string, and potentially crash.
// - On 05/02/2026, a commit was made to libuv which made uv_get_process_title
//   stop using GetConsoleTitleW altogether. Instead, it uses GetModuleFileName
//   to figure out the name of the process EXE. Therefore the bug no longer
//   occurs.
//
// An application which is known to exhibit this issue is Blockbench 5.1.4.
// The symptom is that the app crashes shortly after launching, but when you
// start the app from a command prompt (the command prompt must be run with
// VxKex to bypass CPIW subsystem check), the app runs fine.
//
// In order to fix the issue for this application, we will simply ensure that
// when GetConsoleTitle returns 0 and has an error code of ERROR_SUCCESS, then
// we will always null-terminate the caller-supplied buffer if there is space
// to do so.
//

STATIC VOID FixGetConsoleTitleAorW(
	OUT	PVOID	ConsoleTitle,
	IN	ULONG	BufferCch,
	IN	ULONG	CharacterCb,
	IN	ULONG	ReturnLength)
{
	if (ReturnLength == 0 && RtlGetLastWin32Error() == ERROR_SUCCESS && BufferCch >= 1) {
		if (!IsConsoleAttached()) {
			// Console is not attached, so this is an error.
			// We'll go with the Win10 error value, but as I said it changes
			// wildly across different Windows versions, so the exact value doesn't
			// really matter.
			RtlSetLastWin32Error(ERROR_SEM_NOT_FOUND);
		} else {
			// Console is attached but the function failed for some reason.
			// Write one character's worth of zeros into the console title buffer.
			RtlZeroMemory(ConsoleTitle, CharacterCb);
		}
	}
}

KXBASEAPI ULONG WINAPI Ext_GetConsoleTitleA(
	OUT	PSTR	ConsoleTitle,
	IN	ULONG	BufferCch)
{
	ULONG ReturnLength;

	ReturnLength = GetConsoleTitleA(ConsoleTitle, BufferCch);
	FixGetConsoleTitleAorW(ConsoleTitle, BufferCch, sizeof(*ConsoleTitle), ReturnLength);

	return ReturnLength;
}

KXBASEAPI ULONG WINAPI Ext_GetConsoleTitleW(
	OUT	PWSTR	ConsoleTitle,
	IN	ULONG	BufferCch)
{
	ULONG ReturnLength;

	ReturnLength = GetConsoleTitleW(ConsoleTitle, BufferCch);
	FixGetConsoleTitleAorW(ConsoleTitle, BufferCch, sizeof(*ConsoleTitle), ReturnLength);

	return ReturnLength;
}

KXBASEAPI ULONG WINAPI Ext_GetConsoleOriginalTitleA(
	OUT	PSTR	ConsoleTitle,
	IN	ULONG	BufferCch)
{
	ULONG ReturnLength;

	ReturnLength = GetConsoleOriginalTitleA(ConsoleTitle, BufferCch);
	FixGetConsoleTitleAorW(ConsoleTitle, BufferCch, sizeof(*ConsoleTitle), ReturnLength);

	return ReturnLength;
}

KXBASEAPI ULONG WINAPI Ext_GetConsoleOriginalTitleW(
	OUT	PWSTR	ConsoleTitle,
	IN	ULONG	BufferCch)
{
	ULONG ReturnLength;

	ReturnLength = GetConsoleOriginalTitleW(ConsoleTitle, BufferCch);
	FixGetConsoleTitleAorW(ConsoleTitle, BufferCch, sizeof(*ConsoleTitle), ReturnLength);

	return ReturnLength;
}

KXBASEAPI HRESULT WINAPI CreatePseudoConsole(
	IN	COORD	Size,
	IN	HANDLE	InputHandle,
	IN	HANDLE	OutputHandle,
	IN	DWORD	Flags,
	OUT	HPCON	*PseudoConsoleHandle)
{
	PKEX_PSEUDOCONSOLE PseudoConsole;
	HANDLE CurrentProcess;
	HANDLE DuplicatedInputHandle;
	HANDLE DuplicatedOutputHandle;
	DWORD LastError;

	DuplicatedInputHandle = NULL;
	DuplicatedOutputHandle = NULL;

	if (!PseudoConsoleHandle) {
		return E_INVALIDARG;
	}

	*PseudoConsoleHandle = NULL;
	if (InputHandle == INVALID_HANDLE_VALUE || OutputHandle == INVALID_HANDLE_VALUE ||
		InputHandle == NtCurrentThread() || OutputHandle == NtCurrentThread()) {
		return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
	}

	if (Size.X <= 0 || Size.Y <= 0 || !InputHandle || !OutputHandle ||
		(Flags & ~PSEUDOCONSOLE_INHERIT_CURSOR)) {
		return E_INVALIDARG;
	}

	PseudoConsole = (PKEX_PSEUDOCONSOLE) RtlAllocateHeap(
		RtlProcessHeap(),
		HEAP_ZERO_MEMORY,
		sizeof(*PseudoConsole));

	if (!PseudoConsole) {
		return E_OUTOFMEMORY;
	}

	CurrentProcess = NtCurrentProcess();

	if (!DuplicateHandle(
			CurrentProcess,
			InputHandle,
			CurrentProcess,
			&DuplicatedInputHandle,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS)) {
		LastError = GetLastError();
		RtlFreeHeap(RtlProcessHeap(), 0, PseudoConsole);
		return HRESULT_FROM_WIN32(LastError);
	}

	if (!DuplicateHandle(
			CurrentProcess,
			OutputHandle,
			CurrentProcess,
			&DuplicatedOutputHandle,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS)) {
		LastError = GetLastError();
		CloseHandle(DuplicatedInputHandle);
		RtlFreeHeap(RtlProcessHeap(), 0, PseudoConsole);
		return HRESULT_FROM_WIN32(LastError);
	}

	PseudoConsole->Magic = KEX_PSEUDOCONSOLE_MAGIC;
	PseudoConsole->Size = Size;
	PseudoConsole->hInput = DuplicatedInputHandle;
	PseudoConsole->hOutput = DuplicatedOutputHandle;
	PseudoConsole->Flags = Flags;
	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
	PseudoConsole->Next = KxPseudoConsoles;
	KxPseudoConsoles = PseudoConsole;
	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);

	*PseudoConsoleHandle = (HPCON) PseudoConsole;

	KexLogWarningEvent(
		L"CreatePseudoConsole created a pipe compatibility backend. "
		L"Console buffer hosting and resize notifications are not implemented.");

	return S_OK;
}

KXBASEAPI HRESULT WINAPI ResizePseudoConsole(
	IN	HPCON	PseudoConsoleHandle,
	IN	COORD	Size)
{
	PKEX_PSEUDOCONSOLE PseudoConsole;

	if (Size.X <= 0 || Size.Y <= 0) {
		return E_INVALIDARG;
	}

	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
	if (!KxBasepIsPseudoConsoleHandle(PseudoConsoleHandle, &PseudoConsole)) {
		RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
		return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
	}

	PseudoConsole->Size = Size;
	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);

	return S_OK;
}

KXBASEAPI VOID WINAPI ClosePseudoConsole(
	IN	HPCON	PseudoConsoleHandle)
{
	PKEX_PSEUDOCONSOLE PseudoConsole;
	PKEX_PSEUDOCONSOLE *Link;
	HANDLE Processes[KEX_PSEUDOCONSOLE_MAX_PROCESSES];
	ULONG Index;

	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
	if (!KxBasepIsPseudoConsoleHandle(PseudoConsoleHandle, &PseudoConsole)) {
		RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
		return;
	}

	for (Link = &KxPseudoConsoles; *Link != PseudoConsole; Link = &(*Link)->Next) NOTHING;
	*Link = PseudoConsole->Next;
	KxBasepInvalidatePseudoConsoleAttributes(PseudoConsoleHandle);
	PseudoConsole->Magic = 0;
	RtlCopyMemory(Processes, PseudoConsole->hProcesses, sizeof(Processes));
	RtlZeroMemory(PseudoConsole->hProcesses, sizeof(PseudoConsole->hProcesses));
	CloseHandle(PseudoConsole->hInput);
	CloseHandle(PseudoConsole->hOutput);
	RtlFreeHeap(RtlProcessHeap(), 0, PseudoConsole);
	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);

	for (Index = 0; Index < ARRAYSIZE(Processes); ++Index) {
		if (Processes[Index]) {
			if (WaitForSingleObject(Processes[Index], 0) == WAIT_TIMEOUT) {
				TerminateProcess(Processes[Index], 0);
				WaitForSingleObject(Processes[Index], 2000);
			}
			CloseHandle(Processes[Index]);
		}
	}
}