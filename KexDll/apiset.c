#include "buildcfg.h"
#include "kexdllp.h"

NTSTATUS NTAPI ApiSetQueryApiSetPresence(
	IN	PUNICODE_STRING	Namespace,
	OUT	PBOOLEAN		Present)
{
	KexLogDebugEvent(L"ApiSetQueryApiSetPresence called with \"%wZ\"", Namespace);
	*Present = TRUE;
	return STATUS_SUCCESS;
}

KEXAPI BOOL WINAPI IsApiSetImplemented(
	IN	PCSTR	Contract)
{
	if (!Contract || !*Contract) {
		return FALSE;
	}

	KexLogDebugEvent(L"IsApiSetImplemented called with \"%hs\"", Contract);
	return TRUE;
}