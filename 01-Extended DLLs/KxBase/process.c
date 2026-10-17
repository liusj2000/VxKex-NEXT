#include "buildcfg.h"
#include "kxbasep.h"

#ifndef SW_HIDE
#define SW_HIDE 0
#endif

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#  define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef BOOL (WINAPI *PFN_CREATEPROCESSA)(
	PCSTR, PSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
	DWORD, PVOID, PCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);

typedef BOOL (WINAPI *PFN_CREATEPROCESSW)(
	PCWSTR, PWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
	DWORD, PVOID, PCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

typedef BOOL (WINAPI *PFN_CREATEPROCESSASUSERW)(
	HANDLE, PCWSTR, PWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
	DWORD, PVOID, PCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

typedef struct _KX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY {
	PPROC_THREAD_ATTRIBUTE_LIST	AttributeList;
	HPCON						PseudoConsoleHandle;
	BOOL NativeAttributes;
	BOOL HandleList;
} TYPEDEF_TYPE_NAME(KX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY);

STATIC KX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY KxPseudoConsoleAttributeEntries[64];
STATIC ULONG KxPseudoConsoleAttributeEvictCursor = 0;

STATIC BOOL KxBasepHasNativeProcThreadAttributes(
	IN	PPROC_THREAD_ATTRIBUTE_LIST	AttributeList)
{
	BOOL HasNative;

	if (!AttributeList) {
		return FALSE;
	}

	HasNative = FALSE;
	__try {
		// On Windows Vista/7, PROC_THREAD_ATTRIBUTE_LIST starts with:
		//   ULONG PresentFlags;   (offset 0)
		//   ULONG AttributeCount; (offset 4)
		//   ULONG LastAttribute;  (offset 8)
		HasNative = (((PULONG) AttributeList)[0] != 0 &&
					 ((PULONG) AttributeList)[2] != 0);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		HasNative = FALSE;
	}

	return HasNative;
}

// All table operations are serialized by KxPseudoConsoleLock.
STATIC BOOL KxBasepRememberPseudoConsoleAttribute(
	IN	PPROC_THREAD_ATTRIBUTE_LIST	AttributeList,
	IN	HPCON						PseudoConsoleHandle)
{
	ULONG Index;
	ULONG EmptyIndex;
	ULONG EvictIndex;
	ULONG ScanIndex;

	EmptyIndex = ARRAYSIZE(KxPseudoConsoleAttributeEntries);
	EvictIndex = ARRAYSIZE(KxPseudoConsoleAttributeEntries);

	for (Index = 0; Index < ARRAYSIZE(KxPseudoConsoleAttributeEntries); ++Index) {
		if (KxPseudoConsoleAttributeEntries[Index].AttributeList == AttributeList) {
			KxPseudoConsoleAttributeEntries[Index].PseudoConsoleHandle = PseudoConsoleHandle;
			return TRUE;
		}

		if (!KxPseudoConsoleAttributeEntries[Index].AttributeList &&
			EmptyIndex == ARRAYSIZE(KxPseudoConsoleAttributeEntries)) {
			EmptyIndex = Index;
		}
	}

	if (EmptyIndex == ARRAYSIZE(KxPseudoConsoleAttributeEntries)) {
		// Reclaim a stale slot that was only initialized and never bound an HPCON.
		for (Index = 0; Index < ARRAYSIZE(KxPseudoConsoleAttributeEntries); ++Index) {
			ScanIndex = (KxPseudoConsoleAttributeEvictCursor + Index) %
						ARRAYSIZE(KxPseudoConsoleAttributeEntries);
			if (!KxPseudoConsoleAttributeEntries[ScanIndex].PseudoConsoleHandle) {
				EvictIndex = ScanIndex;
				KxPseudoConsoleAttributeEvictCursor =
					(ScanIndex + 1) % ARRAYSIZE(KxPseudoConsoleAttributeEntries);
				break;
			}
		}
		EmptyIndex = EvictIndex;
	}

	if (EmptyIndex != ARRAYSIZE(KxPseudoConsoleAttributeEntries)) {
		KxPseudoConsoleAttributeEntries[EmptyIndex].AttributeList = AttributeList;
		KxPseudoConsoleAttributeEntries[EmptyIndex].PseudoConsoleHandle = PseudoConsoleHandle;
		KxPseudoConsoleAttributeEntries[EmptyIndex].NativeAttributes =
			KxBasepHasNativeProcThreadAttributes(AttributeList);
		KxPseudoConsoleAttributeEntries[EmptyIndex].HandleList = FALSE;
		return TRUE;
	}
	SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	return FALSE;
}

STATIC PKX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY KxBasepFindPseudoConsoleAttribute(
	IN	PPROC_THREAD_ATTRIBUTE_LIST	AttributeList)
{
	ULONG Index;

	if (!AttributeList) {
		return NULL;
	}

	for (Index = 0; Index < ARRAYSIZE(KxPseudoConsoleAttributeEntries); ++Index) {
		if (KxPseudoConsoleAttributeEntries[Index].AttributeList == AttributeList) {
			return &KxPseudoConsoleAttributeEntries[Index];
		}
	}

	return NULL;
}

VOID KxBasepInvalidatePseudoConsoleAttributes(IN HPCON PseudoConsoleHandle)
{
	ULONG Index;
	for (Index = 0; Index < ARRAYSIZE(KxPseudoConsoleAttributeEntries); ++Index) {
		if (KxPseudoConsoleAttributeEntries[Index].PseudoConsoleHandle == PseudoConsoleHandle) {
			KxPseudoConsoleAttributeEntries[Index].PseudoConsoleHandle = (HPCON) INVALID_HANDLE_VALUE;
		}
	}
}

VOID KxBasepAttachPseudoConsoleProcess(
	IN	PPROC_THREAD_ATTRIBUTE_LIST	AttributeList,
	IN	HANDLE						ProcessHandle)
{
	PKX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY Entry;
	PKEX_PSEUDOCONSOLE PseudoConsole;
	HANDLE DuplicatedProcess;
	ULONG Index;
	ULONG Slot;
	DWORD LastError;

	if (!AttributeList || !ProcessHandle) {
		return;
	}

	LastError = GetLastError();
	DuplicatedProcess = NULL;

	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
	Entry = KxBasepFindPseudoConsoleAttribute(AttributeList);
	if (Entry && KxBasepIsPseudoConsoleHandle(Entry->PseudoConsoleHandle, &PseudoConsole)) {
		if (!DuplicateHandle(
				NtCurrentProcess(),
				ProcessHandle,
				NtCurrentProcess(),
				&DuplicatedProcess,
				PROCESS_TERMINATE | SYNCHRONIZE,
				FALSE,
				0)) {
			DuplicateHandle(
				NtCurrentProcess(),
				ProcessHandle,
				NtCurrentProcess(),
				&DuplicatedProcess,
				0,
				FALSE,
				DUPLICATE_SAME_ACCESS);
		}

		if (DuplicatedProcess) {
			Slot = ARRAYSIZE(PseudoConsole->hProcesses);
			for (Index = 0; Index < ARRAYSIZE(PseudoConsole->hProcesses); ++Index) {
				if (PseudoConsole->hProcesses[Index] &&
					WaitForSingleObject(PseudoConsole->hProcesses[Index], 0) == WAIT_OBJECT_0) {
					CloseHandle(PseudoConsole->hProcesses[Index]);
					PseudoConsole->hProcesses[Index] = NULL;
				}
				if (!PseudoConsole->hProcesses[Index] &&
					Slot == ARRAYSIZE(PseudoConsole->hProcesses)) {
					Slot = Index;
				}
			}

			if (Slot == ARRAYSIZE(PseudoConsole->hProcesses)) {
				CloseHandle(PseudoConsole->hProcesses[0]);
				Slot = 0;
			}
			PseudoConsole->hProcesses[Slot] = DuplicatedProcess;
		}
	}
	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
	SetLastError(LastError);
}

KXBASEAPI BOOL WINAPI Ext_InitializeProcThreadAttributeList(
	OUT PPROC_THREAD_ATTRIBUTE_LIST AttributeList,
	IN DWORD Count, IN DWORD Flags, IN OUT PSIZE_T Size)
{
	BOOL Success;
	PKX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY Entry;
	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
	Success = InitializeProcThreadAttributeList(AttributeList, Count, Flags, Size);
	if (Success) {
		if (KxBasepRememberPseudoConsoleAttribute(AttributeList, NULL)) {
			Entry = KxBasepFindPseudoConsoleAttribute(AttributeList);
			Entry->NativeAttributes = FALSE;
			Entry->HandleList = FALSE;
		}
	}
	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
	return Success;
}

KXBASEAPI VOID WINAPI Ext_DeleteProcThreadAttributeList(
	IN OUT PPROC_THREAD_ATTRIBUTE_LIST AttributeList)
{
	PKX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY Entry;
	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
	Entry = KxBasepFindPseudoConsoleAttribute(AttributeList);
	if (Entry) RtlZeroMemory(Entry, sizeof(*Entry));
	DeleteProcThreadAttributeList(AttributeList);
	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
}

// Returns 0 for no binding, 1 for attached, -1 for an invalid binding.
STATIC LONG KxBasepPreparePseudoConsole(
	IN PPROC_THREAD_ATTRIBUTE_LIST AttributeList,
	IN OUT LPSTARTUPINFOW StartupInfo,
	IN OUT PDWORD CreationFlags)
{
	PKX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY Entry;
	PKEX_PSEUDOCONSOLE PseudoConsole;
	HANDLE Input;
	HANDLE Output;
	HANDLE ErrorOutput;
	DWORD Error;
	LONG Result;

	Result = 0;
	Input = NULL;
	Output = NULL;
	ErrorOutput = NULL;
	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
	Entry = KxBasepFindPseudoConsoleAttribute(AttributeList);
	if (Entry && Entry->PseudoConsoleHandle) {
		Result = -1;
		Error = ERROR_INVALID_HANDLE;
		if (KxBasepIsPseudoConsoleHandle(Entry->PseudoConsoleHandle, &PseudoConsole)) {
			// Private pipe duplicates are absent from the caller's explicit
			// allow-list. Do not silently launch with inaccessible std handles.
			if (Entry->HandleList) {
				Error = ERROR_NOT_SUPPORTED;
			} else {
				if (DuplicateHandle(NtCurrentProcess(), PseudoConsole->hInput,
					NtCurrentProcess(), &Input, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
					if (DuplicateHandle(NtCurrentProcess(), PseudoConsole->hOutput,
						NtCurrentProcess(), &Output, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
						if (DuplicateHandle(NtCurrentProcess(), PseudoConsole->hOutput,
							NtCurrentProcess(), &ErrorOutput, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
							Result = 1;
						}
					}
				}
				Error = GetLastError();
			}
		}
		if (Result > 0) {
			StartupInfo->dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
			StartupInfo->wShowWindow = SW_HIDE;
			StartupInfo->hStdInput = Input;
			StartupInfo->hStdOutput = Output;
			StartupInfo->hStdError = ErrorOutput;
			*CreationFlags = (*CreationFlags & ~(CREATE_NEW_CONSOLE | DETACHED_PROCESS)) | CREATE_NO_WINDOW;
			if (!Entry->NativeAttributes) {
				StartupInfo->cb = sizeof(STARTUPINFOW);
				*CreationFlags &= ~EXTENDED_STARTUPINFO_PRESENT;
			}
		} else {
			if (Input) CloseHandle(Input);
			if (Output) CloseHandle(Output);
			if (ErrorOutput) CloseHandle(ErrorOutput);
			SetLastError(Error);
		}
	}
	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
	return Result;
}

STATIC VOID KxBasepReleaseStartupHandles(IN LPSTARTUPINFOW StartupInfo)
{
	DWORD Error;
	Error = GetLastError();
	if (StartupInfo->hStdInput) CloseHandle(StartupInfo->hStdInput);
	if (StartupInfo->hStdOutput) CloseHandle(StartupInfo->hStdOutput);
	if (StartupInfo->hStdError && StartupInfo->hStdError != StartupInfo->hStdOutput) {
		CloseHandle(StartupInfo->hStdError);
	}
	SetLastError(Error);
}

STATIC FARPROC KxBasepGetKernel32Procedure(
	IN	PCSTR	ProcedureName)
{
	HMODULE Kernel32;

	Kernel32 = GetModuleHandleW(L"kernel32.dll");
	ASSERT (Kernel32 != NULL);

	return GetProcAddress(Kernel32, ProcedureName);
}

KXBASEAPI BOOL WINAPI GetProcessInformation(
	IN	HANDLE						ProcessHandle,
	IN	PROCESS_INFORMATION_CLASS	ProcessInformationClass,
	OUT	PVOID						ProcessInformation,
	IN	ULONG						ProcessInformationSize)
{
	// TODO
	BaseSetLastNTError(STATUS_NOT_IMPLEMENTED);
	return FALSE;
}

KXBASEAPI BOOL WINAPI SetProcessInformation(
	IN	HANDLE						ProcessHandle,
	IN	PROCESS_INFORMATION_CLASS	ProcessInformationClass,
	IN	PVOID						ProcessInformation,
	IN	ULONG						ProcessInformationSize)
{
	// TODO
	BaseSetLastNTError(STATUS_NOT_IMPLEMENTED);
	return FALSE;
}

KXBASEAPI BOOL WINAPI SetProcessDefaultCpuSets(
	IN	HANDLE	ProcessHandle,
	IN	PULONG	CpuSetIds,
	IN	ULONG	NumberOfCpuSetIds)
{
	if (CpuSetIds == NULL) {
		if (NumberOfCpuSetIds != 0) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}
	}

	return TRUE;
}

KXBASEAPI BOOL WINAPI SetProcessDefaultCpuSetMasks(
	IN	HANDLE			ProcessHandle,
	IN	PGROUP_AFFINITY	CpuSetMasks,
	IN	ULONG			NumberOfCpuSetMasks)
{
	if (CpuSetMasks == NULL) {
		if (NumberOfCpuSetMasks != 0) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}
	}

	return TRUE;
}

KXBASEAPI BOOL WINAPI GetProcessDefaultCpuSets(
	IN	HANDLE	ProcessHandle,
	OUT	PULONG	CpuSetIds,
	IN	ULONG	CpuSetIdArraySize,
	OUT	PULONG	ReturnCount)
{
	*ReturnCount = 0;

	if (CpuSetIds == NULL) {
		if (CpuSetIdArraySize != 0) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}
	}

	return TRUE;
}

KXBASEAPI BOOL WINAPI GetProcessDefaultCpuSetMasks(
	IN	HANDLE			ProcessHandle,
	OUT	PGROUP_AFFINITY	CpuSetMasks,
	IN	ULONG			CpuSetMaskArraySize,
	OUT	PULONG			ReturnCount)
{
	*ReturnCount = 0;

	if (CpuSetMasks == NULL) {
		if (CpuSetMaskArraySize != 0) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}
	}

	return TRUE;
}

KXBASEAPI BOOL WINAPI SetProcessMitigationPolicy(
	IN	PROCESS_MITIGATION_POLICY	MitigationPolicy,
	IN	PVOID						Buffer,
	IN	SIZE_T						BufferCb)
{
	//
	// Note that Windows 7 has SetProcessDEPPolicy but it doesn't do anything
	// for x64.
	//

	if (KexRtlCurrentProcessBitness() == 32 && MitigationPolicy == ProcessDEPPolicy) {
		PPROCESS_MITIGATION_DEP_POLICY DepPolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_DEP_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		DepPolicy = (PPROCESS_MITIGATION_DEP_POLICY) Buffer;

		if (DepPolicy->Flags.ReservedFlags) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}

		return SetProcessDEPPolicy(DepPolicy->Flags.AsUlong);
	} else {
		KexLogWarningEvent(
			L"SetProcessMitigationPolicy called with unsupported MitigationPolicy value %d",
			MitigationPolicy);

		// Fall through and pretend we succeeded.
	}

	return TRUE;
}

KXBASEAPI BOOL WINAPI GetProcessMitigationPolicy(
	IN	HANDLE						ProcessHandle,
	IN	PROCESS_MITIGATION_POLICY	MitigationPolicy,
	OUT	PVOID						Buffer,
	IN	SIZE_T						BufferCb)
{
	if (MitigationPolicy == ProcessMitigationOptionsMask) {
		PULONGLONG Mask;

		//
		// Buffer is a pointer to either one or two ULONGLONGs.
		// The first one contains PROCESS_CREATION_MITIGATION_POLICY_*.
		// The second one if present contains PROCESS_CREATION_MITIGATION_POLICY2_*
		//

		if (BufferCb != 8 && BufferCb != 16) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}

		Mask = (PULONGLONG) Buffer;

		Mask[0] = PROCESS_CREATION_MITIGATION_POLICY_VALID_MASK;

		if (BufferCb > 8) {
			Mask[1] = 0;
		}

		return TRUE;
	} else if (MitigationPolicy == ProcessSystemCallDisablePolicy) {
		PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY SystemCallPolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}

		SystemCallPolicy = (PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY) Buffer;

		//
		// Always set this to 0 because Win7 does not support disabling win32k or
		// any other syscalls.
		//

		SystemCallPolicy->Flags = 0;

		return TRUE;
	} else if (MitigationPolicy == ProcessDEPPolicy) {
		BOOLEAN Success;
		PPROCESS_MITIGATION_DEP_POLICY DepPolicy;
		BOOL Permanent;

		if (BufferCb != sizeof(PROCESS_MITIGATION_DEP_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		DepPolicy = (PPROCESS_MITIGATION_DEP_POLICY) Buffer;

		Success = GetProcessDEPPolicy(
			NtCurrentProcess(),
			&DepPolicy->Flags.AsUlong,
			&Permanent);

		DepPolicy->Permanent = Permanent;
		return Success;
	} else if (MitigationPolicy == ProcessSystemCallDisablePolicy) {
		PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY SystemCallDisablePolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		SystemCallDisablePolicy = (PPROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY) Buffer;

		SystemCallDisablePolicy->DisallowWin32kSystemCalls = 0;
		SystemCallDisablePolicy->AuditDisallowWin32kSystemCalls = 0;
		SystemCallDisablePolicy->DisallowFsctlSystemCalls = 0;
		SystemCallDisablePolicy->AuditDisallowFsctlSystemCalls = 0;

		return TRUE;
	} else if (MitigationPolicy == ProcessSignaturePolicy) {
		PPROCESS_MITIGATION_BINARY_SIGNATURE_POLICY SignaturePolicy;

		if (BufferCb != sizeof(PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY)) {
			BaseSetLastNTError(STATUS_INVALID_BUFFER_SIZE);
			return FALSE;
		}

		SignaturePolicy = (PPROCESS_MITIGATION_BINARY_SIGNATURE_POLICY) Buffer;

		SignaturePolicy->AuditMicrosoftSignedOnly = 0;
		SignaturePolicy->AuditStoreSignedOnly = 0;
		SignaturePolicy->MicrosoftSignedOnly = 0;
		SignaturePolicy->MitigationOptIn = 0;
		SignaturePolicy->StoreSignedOnly = 0;

		return TRUE;
	} else {
		KexLogWarningEvent(
			L"GetProcessMitigationPolicy called with unsupported MitigationPolicy value %d",
			MitigationPolicy);
	}

	BaseSetLastNTError(STATUS_NOT_SUPPORTED);
	return FALSE;
}

KXBASEAPI BOOL WINAPI IsProcessCritical(
	IN	HANDLE	ProcessHandle,
	OUT	PBOOL	Critical)
{
	ULONG BreakOnTermination = 0;
	NTSTATUS Status = NtQueryInformationProcess(ProcessHandle, ProcessBreakOnTermination,  &BreakOnTermination, sizeof(BreakOnTermination), NULL);
	if (NT_SUCCESS(Status)) {
		*Critical = BreakOnTermination != 0;
		return TRUE;
	}
	return FALSE;
}

KXBASEAPI BOOL WINAPI Ext_IsProcessInJob(
	IN	HANDLE	ProcessHandle,
	IN	HANDLE	JobHandle,
	OUT	PBOOL	IsInJob)
{
	//
	// APPSPECIFICHACK: Make Chromium non-official builds not meddle with the IAT.
	// See sandbox\policy\win\sandbox_win.cc SandboxWin::InitBrokerServices.
	// This function should only be called in one place.
	//

	if ((KexData->Flags & KEXDATA_FLAG_CHROMIUM) &&
		AshModuleBaseNameIs(ReturnAddress(), L"chrome.dll")) {

		ASSERT (ProcessHandle == NtCurrentProcess());
		ASSERT (JobHandle == NULL);
		ASSERT (IsInJob != NULL);

		KexLogDebugEvent(L"Returning fake IsProcessInJob return value for Chrome compatibility");

		if (ProcessHandle == NtCurrentProcess() &&
			JobHandle == NULL &&
			IsInJob != NULL) {

			*IsInJob = TRUE;
			return TRUE;
		}
	}

	return IsProcessInJob(ProcessHandle, JobHandle, IsInJob);
}

KXBASEAPI BOOL WINAPI Ext_UpdateProcThreadAttribute(
	IN OUT	PPROC_THREAD_ATTRIBUTE_LIST	AttributeList,
	IN		ULONG						Flags,
	IN		ULONG_PTR					Attribute,
	IN		PVOID						Value,
	IN		SIZE_T						Size,
	OUT		PVOID						PreviousValue OPTIONAL,
	OUT		PSIZE_T						ReturnSize OPTIONAL)
{
	BOOLEAN Success;
	BOOLEAN AlreadyTriedAgain;
	ULONG MitigationPolicy;
	PKX_PSEUDOCONSOLE_ATTRIBUTE_ENTRY Entry;

	AlreadyTriedAgain = FALSE;

	if (Attribute == PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE) {
		HPCON PseudoConsoleHandle;
		BOOL Valid;

		if (Flags != 0 || PreviousValue || ReturnSize || !AttributeList || !Value ||
			Size != sizeof(HPCON)) {
			BaseSetLastNTError(STATUS_INVALID_PARAMETER);
			return FALSE;
		}

		RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
		PseudoConsoleHandle = (HPCON) Value;
		Valid = KxBasepIsPseudoConsoleHandle(PseudoConsoleHandle, NULL);
		if (!Valid) {
			__try {
				PseudoConsoleHandle = *(HPCON *) Value;
				Valid = KxBasepIsPseudoConsoleHandle(PseudoConsoleHandle, NULL);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				Valid = FALSE;
			}
		}
		if (!Valid) {
			RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
			BaseSetLastNTError(STATUS_INVALID_HANDLE);
			return FALSE;
		}

		Valid = KxBasepRememberPseudoConsoleAttribute(AttributeList, PseudoConsoleHandle);
		RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
		return Valid;
	}

	RtlAcquireSRWLockExclusive(&KxPseudoConsoleLock);
TryAgain:
	Success = UpdateProcThreadAttribute(
		AttributeList,
		Flags,
		Attribute,
		Value,
		Size,
		PreviousValue,
		ReturnSize);
	if (Success) {
		Entry = KxBasepFindPseudoConsoleAttribute(AttributeList);
		if (Entry) {
			Entry->NativeAttributes = TRUE;
			if (Attribute == PROC_THREAD_ATTRIBUTE_HANDLE_LIST) Entry->HandleList = TRUE;
		}
	}

	if (!Success) {
		ULONG LastError;

		// Save last-error code so we can restore it. The code below might modify it
		// by accident.
		LastError = GetLastError();

		if (AlreadyTriedAgain) {
			KexLogWarningEvent(
				L"UpdateProcThreadAttribute failed despite modifying parameters.\r\n\r\n"
				L"Win32 error: (%d) %s",
				LastError, Win32ErrorAsString(LastError));

			ASSERT (FALSE);
		} else {
			if (LastError == ERROR_NOT_SUPPORTED) {
				switch (Attribute) {
				case PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY:
					//
					// Edit the mitigation policy and go try again.
					//

					if (!AlreadyTriedAgain) {
						KexLogDebugEvent(L"Unsupported mitigation policies specified. Stripping.");

						if (Size > sizeof(ULONG)) {
							ASSERT (Size == sizeof(ULONGLONG) || Size == 2 * sizeof(ULONGLONG));
							Size = sizeof(ULONG);
						}

						MitigationPolicy = (*(PULONG) Value) & PROCESS_CREATION_MITIGATION_POLICY_VALID_MASK;
						Value = &MitigationPolicy;
				
						AlreadyTriedAgain = TRUE;
						goto TryAgain;
					}
				
					break;
				default:
					// Just pretend we succeeded and hope nothing bad happens.
					// Most of the extra mitigation policies don't really do anything anyway,
					// so it should be fine to just pretend it succeeded.
					Success = TRUE;
					LastError = ERROR_SUCCESS;
					break;
				}
			}
		}

		SetLastError(LastError);
	}

	RtlReleaseSRWLockExclusive(&KxPseudoConsoleLock);
	return Success;
}

KXBASEAPI BOOL WINAPI Ext_CreateProcessW(
	IN PCWSTR ApplicationName OPTIONAL,
	IN OUT PWSTR CommandLine OPTIONAL,
	IN LPSECURITY_ATTRIBUTES ProcessAttributes OPTIONAL,
	IN LPSECURITY_ATTRIBUTES ThreadAttributes OPTIONAL,
	IN BOOL InheritHandles,
	IN DWORD CreationFlags,
	IN PVOID Environment OPTIONAL,
	IN PCWSTR CurrentDirectory OPTIONAL,
	IN LPSTARTUPINFOW StartupInfo,
	OUT LPPROCESS_INFORMATION ProcessInformation)
{
	PFN_CREATEPROCESSW CreateProcessProc;
	STARTUPINFOEXW LocalStartupInfoEx;
	LONG Attached;
	BOOL Success;

	CreateProcessProc = (PFN_CREATEPROCESSW) KxBasepGetKernel32Procedure("CreateProcessW");
	if (!CreateProcessProc) {
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	Attached = 0;
	if ((CreationFlags & EXTENDED_STARTUPINFO_PRESENT) && StartupInfo &&
		StartupInfo->cb >= sizeof(LocalStartupInfoEx)) {
		RtlCopyMemory(&LocalStartupInfoEx, StartupInfo, sizeof(LocalStartupInfoEx));
		Attached = KxBasepPreparePseudoConsole(LocalStartupInfoEx.lpAttributeList,
			(LPSTARTUPINFOW) &LocalStartupInfoEx.StartupInfo, &CreationFlags);
		if (Attached < 0) return FALSE;
		if (Attached) {
			StartupInfo = &LocalStartupInfoEx.StartupInfo;
			InheritHandles = TRUE;
		}
	}
	Success = CreateProcessProc(ApplicationName, CommandLine,
		ProcessAttributes, ThreadAttributes, InheritHandles, CreationFlags,
		Environment, CurrentDirectory, StartupInfo, ProcessInformation);
	if (Attached) {
		if (Success && ProcessInformation) {
			KxBasepAttachPseudoConsoleProcess(
				LocalStartupInfoEx.lpAttributeList,
				ProcessInformation->hProcess);
		}
		KxBasepReleaseStartupHandles((LPSTARTUPINFOW) StartupInfo);
	}
	return Success;
}
KXBASEAPI BOOL WINAPI Ext_CreateProcessA(
	IN PCSTR ApplicationName OPTIONAL,
	IN OUT PSTR CommandLine OPTIONAL,
	IN LPSECURITY_ATTRIBUTES ProcessAttributes OPTIONAL,
	IN LPSECURITY_ATTRIBUTES ThreadAttributes OPTIONAL,
	IN BOOL InheritHandles,
	IN DWORD CreationFlags,
	IN PVOID Environment OPTIONAL,
	IN PCSTR CurrentDirectory OPTIONAL,
	IN LPSTARTUPINFOA StartupInfo,
	OUT LPPROCESS_INFORMATION ProcessInformation)
{
	PFN_CREATEPROCESSA CreateProcessProc;
	STARTUPINFOEXA LocalStartupInfoEx;
	LONG Attached;
	BOOL Success;

	CreateProcessProc = (PFN_CREATEPROCESSA) KxBasepGetKernel32Procedure("CreateProcessA");
	if (!CreateProcessProc) {
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	Attached = 0;
	if ((CreationFlags & EXTENDED_STARTUPINFO_PRESENT) && StartupInfo &&
		StartupInfo->cb >= sizeof(LocalStartupInfoEx)) {
		RtlCopyMemory(&LocalStartupInfoEx, StartupInfo, sizeof(LocalStartupInfoEx));
		Attached = KxBasepPreparePseudoConsole(LocalStartupInfoEx.lpAttributeList,
			(LPSTARTUPINFOW) &LocalStartupInfoEx.StartupInfo, &CreationFlags);
		if (Attached < 0) return FALSE;
		if (Attached) {
			StartupInfo = &LocalStartupInfoEx.StartupInfo;
			InheritHandles = TRUE;
		}
	}
	Success = CreateProcessProc(ApplicationName, CommandLine,
		ProcessAttributes, ThreadAttributes, InheritHandles, CreationFlags,
		Environment, CurrentDirectory, StartupInfo, ProcessInformation);
	if (Attached) {
		if (Success && ProcessInformation) {
			KxBasepAttachPseudoConsoleProcess(
				LocalStartupInfoEx.lpAttributeList,
				ProcessInformation->hProcess);
		}
		KxBasepReleaseStartupHandles((LPSTARTUPINFOW) StartupInfo);
	}
	return Success;
}
KXBASEAPI BOOL WINAPI Ext_CreateProcessAsUserW(
	IN HANDLE Token,
	IN PCWSTR ApplicationName OPTIONAL,
	IN OUT PWSTR CommandLine OPTIONAL,
	IN LPSECURITY_ATTRIBUTES ProcessAttributes OPTIONAL,
	IN LPSECURITY_ATTRIBUTES ThreadAttributes OPTIONAL,
	IN BOOL InheritHandles,
	IN DWORD CreationFlags,
	IN PVOID Environment OPTIONAL,
	IN PCWSTR CurrentDirectory OPTIONAL,
	IN LPSTARTUPINFOW StartupInfo,
	OUT LPPROCESS_INFORMATION ProcessInformation)
{
	PFN_CREATEPROCESSASUSERW CreateProcessProc;
	STARTUPINFOEXW LocalStartupInfoEx;
	LONG Attached;
	BOOL Success;

	CreateProcessProc = (PFN_CREATEPROCESSASUSERW) KxBasepGetKernel32Procedure("CreateProcessAsUserW");
	if (!CreateProcessProc) {
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	Attached = 0;
	if ((CreationFlags & EXTENDED_STARTUPINFO_PRESENT) && StartupInfo &&
		StartupInfo->cb >= sizeof(LocalStartupInfoEx)) {
		RtlCopyMemory(&LocalStartupInfoEx, StartupInfo, sizeof(LocalStartupInfoEx));
		Attached = KxBasepPreparePseudoConsole(LocalStartupInfoEx.lpAttributeList,
			(LPSTARTUPINFOW) &LocalStartupInfoEx.StartupInfo, &CreationFlags);
		if (Attached < 0) return FALSE;
		if (Attached) {
			StartupInfo = &LocalStartupInfoEx.StartupInfo;
			InheritHandles = TRUE;
		}
	}
	Success = CreateProcessProc(Token, ApplicationName, CommandLine,
		ProcessAttributes, ThreadAttributes, InheritHandles, CreationFlags,
		Environment, CurrentDirectory, StartupInfo, ProcessInformation);
	if (Attached) {
		if (Success && ProcessInformation) {
			KxBasepAttachPseudoConsoleProcess(
				LocalStartupInfoEx.lpAttributeList,
				ProcessInformation->hProcess);
		}
		KxBasepReleaseStartupHandles((LPSTARTUPINFOW) StartupInfo);
	}
	return Success;
}
