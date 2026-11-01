#include "buildcfg.h"
#include "kxbasep.h"

//
// If strong SharedUserData spoofing is enabled, this function
// supersedes KernelBase!GetSystemTimeAsFileTime because the original
// function reads system time from SharedUserData.
//
KXBASEAPI VOID WINAPI KxBasepGetSystemTimeAsFileTimeHook(
	OUT	PFILETIME	SystemTimeAsFileTime)
{
	ASSERT (KexData->IfeoParameters.StrongVersionSpoof & KEX_STRONGSPOOF_SHAREDUSERDATA);
	NtQuerySystemTime((PLONGLONG) SystemTimeAsFileTime);
}

//
// Same as above but this function supersedes GetSystemTime when doing
// SharedUserData-based version spoofing.
//
KXBASEAPI VOID WINAPI KxBasepGetSystemTimeHook(
	OUT	PSYSTEMTIME	SystemTime)
{
	LONGLONG Time;
	TIME_FIELDS TimeFields;

	ASSERT (KexData->IfeoParameters.StrongVersionSpoof & KEX_STRONGSPOOF_SHAREDUSERDATA);

	NtQuerySystemTime(&Time);
	RtlTimeToTimeFields(&Time, &TimeFields);

	//
	// Annoyingly, the TIME_FIELDS structure is not directly compatible with
	// the SYSTEMTIME structure...
	//

	SystemTime->wYear			= TimeFields.Year;
	SystemTime->wMonth			= TimeFields.Month;
	SystemTime->wDay			= TimeFields.Day;
	SystemTime->wDayOfWeek		= TimeFields.Weekday;
	SystemTime->wHour			= TimeFields.Hour;
	SystemTime->wMinute			= TimeFields.Minute;
	SystemTime->wSecond			= TimeFields.Second;
	SystemTime->wMilliseconds	= TimeFields.Milliseconds;
}

KXBASEAPI VOID WINAPI GetSystemTimePreciseAsFileTime(
	OUT	PFILETIME	SystemTimeAsFileTime)
{
	*(PLONGLONG) SystemTimeAsFileTime = KexRtlGetSystemTimePrecise();
}

STATIC ULONGLONG KxBasepReadInterruptTime(
	VOID)
{
	LONG High1;
	LONG High2;
	ULONG Low;

	do {
		High1 = SharedUserData->InterruptTime.High1Time;
		Low = SharedUserData->InterruptTime.LowPart;
		High2 = SharedUserData->InterruptTime.High2Time;
	} while (High1 != High2);

	return ((ULONGLONG) (ULONG) High1 << 32) | Low;
}

STATIC LONGLONG g_QpcFrequency = 0;
STATIC LONGLONG g_BaseQpc = 0;
STATIC ULONGLONG g_BaseInterruptTime = 0;
STATIC VOLATILE LONG g_PreciseTimeInitState = 0;

KXBASEAPI VOID WINAPI QueryInterruptTimePrecise(
	OUT	PULONGLONG	InterruptTimePrecise)
{
	LARGE_INTEGER CurrentQpc;

	if (!InterruptTimePrecise) {
		return;
	}

	if (g_PreciseTimeInitState != 2) {
		if (InterlockedCompareExchange(&g_PreciseTimeInitState, 1, 0) == 0) {
			LARGE_INTEGER Freq;
			LARGE_INTEGER BaseCounter;

			if (QueryPerformanceFrequency(&Freq) && Freq.QuadPart > 0 &&
				QueryPerformanceCounter(&BaseCounter)) {
				g_BaseInterruptTime = KxBasepReadInterruptTime();
				g_BaseQpc = BaseCounter.QuadPart;
				g_QpcFrequency = Freq.QuadPart;
			}

			InterlockedExchange(&g_PreciseTimeInitState, 2);
		} else {
			while (g_PreciseTimeInitState != 2) {
				YieldProcessor();
			}
		}
	}

	if (g_QpcFrequency > 0 && QueryPerformanceCounter(&CurrentQpc)) {
		LONGLONG DeltaTicks = CurrentQpc.QuadPart - g_BaseQpc;
		if (DeltaTicks >= 0) {
			ULONGLONG Quotient = (ULONGLONG) DeltaTicks / (ULONGLONG) g_QpcFrequency;
			ULONGLONG Remainder = (ULONGLONG) DeltaTicks % (ULONGLONG) g_QpcFrequency;
			ULONGLONG Delta100ns = Quotient * 10000000ULL + (Remainder * 10000000ULL) / (ULONGLONG) g_QpcFrequency;
			*InterruptTimePrecise = g_BaseInterruptTime + Delta100ns;
			return;
		}
	}

	*InterruptTimePrecise = KxBasepReadInterruptTime();
}

KXBASEAPI VOID WINAPI QueryInterruptTime(
	OUT	PULONGLONG	InterruptTime)
{
	if (!InterruptTime) {
		return;
	}

	if (KexData && (KexData->IfeoParameters.StrongVersionSpoof & KEX_STRONGSPOOF_SHAREDUSERDATA)) {
		QueryInterruptTimePrecise(InterruptTime);
		return;
	}

	*InterruptTime = KxBasepReadInterruptTime();
}

KXBASEAPI VOID WINAPI QueryUnbiasedInterruptTimePrecise(
	OUT	PULONGLONG	UnbiasedInterruptTimePrecise)
{
	QueryUnbiasedInterruptTime(UnbiasedInterruptTimePrecise);
}

KXBASEAPI HRESULT WINAPI ConvertAuxiliaryCounterToPerformanceCounter(
	IN	ULONGLONG	AuxiliaryCounterValue,
	OUT	PULONGLONG	PerformanceCounterValue,
	OUT	PULONGLONG	ConversionError OPTIONAL)
{
	if (!PerformanceCounterValue) {
		return E_POINTER;
	}

	return E_NOTIMPL;
}

KXBASEAPI HRESULT WINAPI ConvertPerformanceCounterToAuxiliaryCounter(
	IN	ULONGLONG	PerformanceCounterValue,
	OUT	PULONGLONG	AuxiliaryCounterValue,
	OUT	PULONGLONG	ConversionError OPTIONAL)
{
	if (!AuxiliaryCounterValue) {
		return E_POINTER;
	}

	return E_NOTIMPL;
}

KXBASEAPI HRESULT WINAPI QueryAuxiliaryCounterFrequency(
	OUT	PULONGLONG	AuxiliaryCounterFrequency)
{
	if (!AuxiliaryCounterFrequency) {
		return E_POINTER;
	}

	return E_NOTIMPL;
}