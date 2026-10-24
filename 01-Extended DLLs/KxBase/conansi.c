///////////////////////////////////////////////////////////////////////////////
//
// Module Name:
//
//     conansi.c
//
// Abstract:
//
//     Contains support code for processing ANSI escape sequences on a
//     Windows console.
//
// Author:
//
//     vxiiduu (05-May-2026)
//
// Environment:
//
//     Win32
//
// Revision History:
//
//     vxiiduu              05-May-2026  Initial creation.
//
///////////////////////////////////////////////////////////////////////////////

#include "buildcfg.h"
#include "kxbasep.h"

#define ESC (0x1B)
#define BEL (0x07)
#define VXKEX_CONSOLE_ANSI_OUTPUT	1

#define MAX_CSI_PARAMETER_COUNT		16

//
// Global variables for ANSI escape sequence support.
//

STATIC ULONG g_ConsoleANSISupport = 0; // VXKEX_CONSOLE_*
STATIC COORD g_SavedCursorPosition; // 1-based, viewport-relative.
STATIC WORD g_DefaultTextAttributes;

STATIC struct {
	BOOLEAN		Active;
	COORD		SavedCursorPosition;
	PCHAR_INFO	Buffer;
	COORD		BufferSize;
	SMALL_RECT	WindowRect;
} g_AlternateScreen = {0};

STATIC CONST WCHAR DecLineDrawingTable[0x20] = {
	0x00A0, // 0x5F: blank / non-breaking space
	0x2666, // 0x60 '`': diamond
	0x2592, // 0x61 'a': checkerboard / shaded block
	0x2409, // 0x62 'b': HT symbol
	0x240C, // 0x63 'c': FF symbol
	0x240D, // 0x64 'd': CR symbol
	0x240A, // 0x65 'e': LF symbol
	0x00B0, // 0x66 'f': degree sign
	0x00B1, // 0x67 'g': plus-minus
	0x2424, // 0x68 'h': NL symbol
	0x240B, // 0x69 'i': VT symbol
	0x2518, // 0x6A 'j': lower right corner
	0x2510, // 0x6B 'k': upper right corner
	0x250C, // 0x6C 'l': upper left corner
	0x2514, // 0x6D 'm': lower left corner
	0x253C, // 0x6E 'n': crossing lines
	0x23BA, // 0x6F 'o': scan line 1
	0x23BB, // 0x70 'p': scan line 3
	0x2500, // 0x71 'q': horizontal line
	0x23BC, // 0x72 'r': scan line 7
	0x23BD, // 0x73 's': scan line 9
	0x251C, // 0x74 't': left tee
	0x2524, // 0x75 'u': right tee
	0x2534, // 0x76 'v': bottom tee
	0x252C, // 0x77 'w': top tee
	0x2502, // 0x78 'x': vertical line
	0x2264, // 0x79 'y': less-than-or-equal
	0x2265, // 0x7A 'z': greater-than-or-equal
	0x03C0, // 0x7B '{': pi
	0x2260, // 0x7C '|': not-equal
	0x00A3, // 0x7D '}': pound
	0x00B7  // 0x7E '~': centered dot / bullet
};

STATIC BOOLEAN g_DecLineDrawing = FALSE;

#define MAX_PENDING_ESCAPE_CCH 256

STATIC struct {
	WCHAR	Buffer[MAX_PENDING_ESCAPE_CCH];
	ULONG	Cch;
	HANDLE	ConsoleHandle;
} g_PendingEscape = {0};

STATIC VOID VTLeaveAlternateScreenBuffer(
	IN	HANDLE	ConsoleHandle);

STATIC VOID VTEnterAlternateScreenBuffer(
	IN	HANDLE	ConsoleHandle);

BOOL IsConsoleHandle(
	IN	HANDLE	Handle)
{
	DWORD Mode;
	if ((GetFileType(Handle) & FILE_TYPE_CHAR) == 0) return FALSE;
	return GetConsoleMode(Handle, &Mode);
}

BOOLEAN BaseIsConsoleAnsiSupportEnabled(
	IN	HANDLE	ConsoleHandle)
{
	return !!g_ConsoleANSISupport;
}

BOOLEAN BaseEnableConsoleAnsiSupport(
	IN	HANDLE	ConsoleHandle)
{
	BOOLEAN Success;

	ASSERT (IsConsoleHandle(ConsoleHandle));

	Success = VTGetCursorPosition(ConsoleHandle, &g_SavedCursorPosition);
	ASSERT (Success);

	if (!Success) {
		return Success;
	}

	//
	// We need to save the current console attributes instead of using a
	// hardcoded value such as white on black, because apps started from
	// e.g. Powershell will need to take into account the pre-existing white
	// on blue text attributes. Otherwise any attempt to reset text attributes
	// will cause ugliness.
	//
	// Also, the user can change the default console colors, and we want to
	// behave correctly in that situation.
	//

	Success = GetConsoleTextAttribute(ConsoleHandle, &g_DefaultTextAttributes);
	ASSERT (Success);

	// Remove reverse video attribute if it happens to be set.
	// It's very unlikely, but if it were, then SGR 0 would end up enabling
	// reverse video, which is unwanted.
	g_DefaultTextAttributes &= ~COMMON_LVB_REVERSE_VIDEO;

	if (!Success) {
		return Success;
	}

	g_ConsoleANSISupport |= VXKEX_CONSOLE_ANSI_OUTPUT;
	KexLogDebugEvent(L"Enabled escape sequence processing");
	return Success;
}

BOOLEAN BaseDisableConsoleAnsiSupport(
	IN	HANDLE	ConsoleHandle)
{
	VTLeaveAlternateScreenBuffer(ConsoleHandle);
	g_PendingEscape.Cch = 0;
	g_DecLineDrawing = FALSE;

	g_ConsoleANSISupport &= ~VXKEX_CONSOLE_ANSI_OUTPUT;
	KexLogDebugEvent(L"Disabled escape sequence processing");
	return TRUE;
}

STATIC ULONG ParseNumber(
	IN	PCWCHAR	Characters,
	IN	ULONG	MaxCch,
	OUT	PULONG	NumberCch OPTIONAL)
{
	ULONG Accumulator;
	ULONG Index;

	Accumulator = 0;
	Index = 0;

	until (!iswdigit(Characters[Index]) || Index >= MaxCch) {
		Accumulator *= 10;
		Accumulator += Characters[Index] - '0';
		++Index;
	}

	if (NumberCch) {
		*NumberCch = Index;
	}

	return Accumulator;
}

STATIC ULONG GetCchToNextEscapeOrEndOfBufferAorW(
	IN	PCVOID	Buffer,
	IN	ULONG	BufferCch,
	IN	BOOLEAN	Unicode)
{
	ULONG Index;

	Index = 0;

	if (Unicode) {
		PCWCHAR BufferW;

		BufferW = (PCWCHAR) Buffer;

		until (BufferCch == 0) {
			if (BufferW[Index] == ESC) {
				return Index;
			}

			--BufferCch;
			++Index;
		}
	} else {
		PCSTR BufferA;

		BufferA = (PCSTR) Buffer;

		until (BufferCch == 0) {
			if (BufferA[Index] == ESC) {
				return Index;
			}

			--BufferCch;
			++Index;
		}
	}

	// If we get here it means that there's no escape sequence
	return Index;
}

STATIC BOOLEAN ExecuteCSISetGraphicsRendition(
	IN	HANDLE	ConsoleHandle,
	IN	SHORT	CsiParameters[MAX_CSI_PARAMETER_COUNT],
	IN	ULONG	ParameterCount)
{
	WORD TextAttribute;
	ULONG ParameterIndex;

	GetConsoleTextAttribute(ConsoleHandle, &TextAttribute);

	// set parameter count to 1 if it is 0.
	// if there are no parameters we treat that the same as one parameter with
	// the value 0. Note that the Parameters array is initialized to all zeros
	// if there are no parameters, which is why this works.
	ParameterCount = max(ParameterCount, 1);

	for (ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex) {
		SHORT Parameter;

		Parameter = CsiParameters[ParameterIndex];

		if (TextAttribute & COMMON_LVB_REVERSE_VIDEO) {
			// Swap the handling of foreground and background colors
			if (Parameter >= 30 && Parameter <= 39 ||
				Parameter >= 90 && Parameter <= 97) {

				Parameter += 10;
			} else if (Parameter >= 40 && Parameter <= 49 ||
						Parameter >= 100 && Parameter <= 107) {

				Parameter -= 10;
			}
		}

		if (Parameter >= 30 && Parameter <= 39) {
			// setting non-bright foreground color
			TextAttribute &= ~(FOREGROUND_INTENSITY |
								FOREGROUND_RED |
								FOREGROUND_GREEN |
								FOREGROUND_BLUE);
		} else if (Parameter >= 40 && Parameter <= 49) {
			// setting non-bright background color
			TextAttribute &= ~(BACKGROUND_INTENSITY |
								BACKGROUND_RED |
								BACKGROUND_GREEN |
								BACKGROUND_BLUE);
		} else if (Parameter >= 90 && Parameter <= 97) {
			// setting bright foreground color
			TextAttribute |= FOREGROUND_INTENSITY;
			TextAttribute &= ~(FOREGROUND_RED |
								FOREGROUND_GREEN |
								FOREGROUND_BLUE);
			Parameter -= 60;
		} else if (Parameter >= 100 && Parameter <= 107) {
			// setting bright background color
			TextAttribute |= BACKGROUND_INTENSITY;
			TextAttribute &= ~(BACKGROUND_RED |
								BACKGROUND_GREEN |
								BACKGROUND_BLUE);
			Parameter -= 60;
		}

		switch (Parameter) {
		case 0:		// Reset
			TextAttribute = g_DefaultTextAttributes;
			break;
		case 1:		// Bright
			TextAttribute |= FOREGROUND_INTENSITY;
			break;
		case 7:		// Reverse video (negative)
			// This flag has no effect on the console but we just use it to remember
			// the fact that reverse video is enabled
			TextAttribute |= COMMON_LVB_REVERSE_VIDEO;
			// Swap foreground and background colors
			*(PBYTE) &TextAttribute = _rotl8((BYTE) TextAttribute, 4);
			break;
		case 27:	// Turn off reverse video (positive)
			TextAttribute &= ~COMMON_LVB_REVERSE_VIDEO;
			*(PBYTE) &TextAttribute = _rotl8((BYTE) TextAttribute, 4);
			break;
		case 22:	// No bright
			TextAttribute &= ~FOREGROUND_INTENSITY;
			break;
		case 31:	// Red
			TextAttribute |= FOREGROUND_RED;
			break;
		case 32:	// Green
			TextAttribute |= FOREGROUND_GREEN;
			break;
		case 33:	// Yellow
			TextAttribute |= FOREGROUND_RED | FOREGROUND_GREEN;
			break;
		case 34:	// Blue
			TextAttribute |= FOREGROUND_BLUE;
			break;
		case 35:	// Magenta
			TextAttribute |= FOREGROUND_RED | FOREGROUND_BLUE;
			break;
		case 36:	// Cyan
			TextAttribute |= FOREGROUND_GREEN | FOREGROUND_BLUE;
			break;
		case 37:	// White
			TextAttribute |= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
			break;
		case 39:	// Foreground reset
			TextAttribute |= g_DefaultTextAttributes & (FOREGROUND_INTENSITY |
														FOREGROUND_RED |
														FOREGROUND_GREEN |
														FOREGROUND_BLUE);
			break;
		case 41:	// Background red
			TextAttribute |= BACKGROUND_RED;
			break;
		case 42:	// Background green
			TextAttribute |= BACKGROUND_GREEN;
			break;
		case 43:	// Background yellow
			TextAttribute |= BACKGROUND_RED | BACKGROUND_GREEN;
			break;
		case 44:	// Background blue
			TextAttribute |= BACKGROUND_BLUE;
			break;
		case 45:	// Background magenta
			TextAttribute |= BACKGROUND_RED | BACKGROUND_BLUE;
			break;
		case 46:	// Background cyan
			TextAttribute |= BACKGROUND_GREEN | BACKGROUND_BLUE;
			break;
		case 47:	// Background white
			TextAttribute |= BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
			break;
		case 49:	// Background reset
			TextAttribute |= g_DefaultTextAttributes & (BACKGROUND_INTENSITY |
														BACKGROUND_RED |
														BACKGROUND_GREEN |
														BACKGROUND_BLUE);
			break;
		case 38:	// Extended foreground color setting
		case 48:	// Extended background color setting
			// We don't support extended fg/bg color setting.
			// These come with additional parameters which will cause
			// random color changes if we were to interpret them as normal
			// SGR parameters, so we'll just ignore the rest of the SGR escape.
			KexLogDebugEvent(
				L"Extended %s color setting ignored",
				Parameter == 38 ? L"foreground" : L"background");
			KexDebugCheckpoint();
			return FALSE;
		}
	}

	return SetConsoleTextAttribute(ConsoleHandle, TextAttribute);
}

STATIC VOID VTEnterAlternateScreenBuffer(
	IN	HANDLE	ConsoleHandle)
{
	CONSOLE_SCREEN_BUFFER_INFO Sbi;
	SHORT Width, Height;
	ULONG CellCount;

	if (g_AlternateScreen.Active) {
		return;
	}

	if (!GetConsoleScreenBufferInfo(ConsoleHandle, &Sbi)) {
		return;
	}

	Width = Sbi.srWindow.Right - Sbi.srWindow.Left + 1;
	Height = Sbi.srWindow.Bottom - Sbi.srWindow.Top + 1;
	if (Width <= 0 || Height <= 0) {
		return;
	}

	g_AlternateScreen.SavedCursorPosition = Sbi.dwCursorPosition;
	g_AlternateScreen.WindowRect = Sbi.srWindow;
	g_AlternateScreen.BufferSize.X = Width;
	g_AlternateScreen.BufferSize.Y = Height;

	CellCount = (ULONG) Width * Height;

	if (g_AlternateScreen.Buffer) {
		RtlFreeHeap(RtlProcessHeap(), 0, g_AlternateScreen.Buffer);
		g_AlternateScreen.Buffer = NULL;
	}

	g_AlternateScreen.Buffer = (PCHAR_INFO) RtlAllocateHeap(
		RtlProcessHeap(),
		HEAP_ZERO_MEMORY,
		CellCount * sizeof(CHAR_INFO));

	if (g_AlternateScreen.Buffer) {
		COORD BufferCoord = {0, 0};
		SMALL_RECT ReadRegion = Sbi.srWindow;
		if (!ReadConsoleOutputW(
			ConsoleHandle,
			g_AlternateScreen.Buffer,
			g_AlternateScreen.BufferSize,
			BufferCoord,
			&ReadRegion)) {
			HANDLE ConOut = CreateFileW(
				L"CONOUT$",
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				0,
				NULL);
			if (ConOut != INVALID_HANDLE_VALUE) {
				ReadConsoleOutputW(
					ConOut,
					g_AlternateScreen.Buffer,
					g_AlternateScreen.BufferSize,
					BufferCoord,
					&ReadRegion);
				CloseHandle(ConOut);
			}
		}
	}

	g_AlternateScreen.Active = TRUE;

	// Clear the viewport and place cursor at (Left, Top)
	{
		COORD Origin;
		ULONG TotalCells;
		ULONG Written;

		Origin.X = Sbi.srWindow.Left;
		Origin.Y = Sbi.srWindow.Top;
		TotalCells = (ULONG) Sbi.dwSize.X * Height;

		FillConsoleOutputCharacterW(ConsoleHandle, L' ', TotalCells, Origin, &Written);
		FillConsoleOutputAttribute(ConsoleHandle, g_DefaultTextAttributes ? g_DefaultTextAttributes : Sbi.wAttributes, TotalCells, Origin, &Written);
		SetConsoleCursorPosition(ConsoleHandle, Origin);
	}
}

STATIC VOID VTLeaveAlternateScreenBuffer(
	IN	HANDLE	ConsoleHandle)
{
	if (!g_AlternateScreen.Active) {
		return;
	}

	if (g_AlternateScreen.Buffer) {
		COORD BufferCoord = {0, 0};
		SMALL_RECT WriteRegion = g_AlternateScreen.WindowRect;
		if (!WriteConsoleOutputW(
			ConsoleHandle,
			g_AlternateScreen.Buffer,
			g_AlternateScreen.BufferSize,
			BufferCoord,
			&WriteRegion)) {
			HANDLE ConOut = CreateFileW(
				L"CONOUT$",
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				0,
				NULL);
			if (ConOut != INVALID_HANDLE_VALUE) {
				WriteConsoleOutputW(
					ConOut,
					g_AlternateScreen.Buffer,
					g_AlternateScreen.BufferSize,
					BufferCoord,
					&WriteRegion);
				CloseHandle(ConOut);
			}
		}

		RtlFreeHeap(RtlProcessHeap(), 0, g_AlternateScreen.Buffer);
		g_AlternateScreen.Buffer = NULL;
	}

	SetConsoleCursorPosition(ConsoleHandle, g_AlternateScreen.SavedCursorPosition);
	g_AlternateScreen.Active = FALSE;
}

STATIC BOOLEAN ExecuteCSIErase(
	IN	HANDLE	ConsoleHandle,
	IN	SHORT	Parameter,
	IN	BOOLEAN	LineOnly)
{
	BOOLEAN Success;
	CONSOLE_SCREEN_BUFFER_INFO Sbi;
	COORD EraseStart;
	COORD EraseEnd;
	ULONG EraseCount;

	Success = GetConsoleScreenBufferInfo(ConsoleHandle, &Sbi);
	if (!Success) {
		return FALSE;
	}

	switch (Parameter) {
	case 0:
		EraseStart = Sbi.dwCursorPosition;
		EraseEnd.X = Sbi.dwSize.X - 1;

		if (LineOnly) {
			// erase from cursor position to end of current line
			EraseEnd.Y = Sbi.dwCursorPosition.Y;
		} else {
			// erase from cursor position to end of display
			EraseEnd.Y = Sbi.srWindow.Bottom;
		}

		break;
	case 1:
		EraseEnd = Sbi.dwCursorPosition;
		EraseStart.X = 0;

		if (LineOnly) {
			// erase from beginning of line to cursor
			EraseStart.Y = Sbi.dwCursorPosition.Y;
		} else {
			// erase from beginning of display to cursor
			EraseStart.Y = Sbi.srWindow.Top;
		}

		break;
	case 2:
		EraseStart.X = 0;
		EraseEnd.X = Sbi.dwSize.X - 1;

		if (LineOnly) {
			// erase whole line
			EraseStart.Y = Sbi.dwCursorPosition.Y;
			EraseEnd.Y = Sbi.dwCursorPosition.Y;
		} else {
			// erase whole screen
			EraseStart.Y = Sbi.srWindow.Top;
			EraseEnd.Y = Sbi.srWindow.Bottom;
		}

		break;
	case 3:
		if (LineOnly) {
			EraseStart.X = 0;
			EraseEnd.X = Sbi.dwSize.X - 1;
			EraseStart.Y = Sbi.dwCursorPosition.Y;
			EraseEnd.Y = Sbi.dwCursorPosition.Y;
		} else {
			// erase whole screen buffer including scrollback
			EraseStart.X = 0;
			EraseStart.Y = 0;
			EraseEnd.X = Sbi.dwSize.X - 1;
			EraseEnd.Y = Sbi.dwSize.Y - 1;
		}

		break;
	default:
		KexLogDebugEvent(L"Invalid parameter %hu to CSI 'J' or 'K' escape", Parameter);
		return FALSE;
	}

	EraseCount = (EraseEnd.Y - EraseStart.Y) * Sbi.dwSize.X;
	EraseCount += EraseEnd.X - EraseStart.X + 1;

	Success = FillConsoleOutputCharacter(
		ConsoleHandle,
		' ',
		EraseCount,
		EraseStart,
		&EraseCount);
	
	if (!Success) {
		return Success;
	}

	return FillConsoleOutputAttribute(
		ConsoleHandle,
		Sbi.wAttributes,
		EraseCount,
		EraseStart,
		&EraseCount);
}

STATIC BOOLEAN ExecuteCSI(
	IN	HANDLE	ConsoleHandle,
	IN	WCHAR	Command,
	IN	BOOLEAN	IsPrivateMode,
	IN	SHORT	CsiParameters[MAX_CSI_PARAMETER_COUNT],
	IN	ULONG	ParameterCount)
{
	COORD Coord;

	switch (Command) {
	case 'A': // Cursor Up
		Coord.X = 0;
		Coord.Y = -max(1, CsiParameters[0]);
		return VTMoveCursorRelative(ConsoleHandle, Coord);
	case 'B': // Cursor Down
		Coord.X = 0;
		Coord.Y = max(1, CsiParameters[0]);
		return VTMoveCursorRelative(ConsoleHandle, Coord);
	case 'C': // Cursor Forward (Right)
		Coord.X = max(1, CsiParameters[0]);
		Coord.Y = 0;
		return VTMoveCursorRelative(ConsoleHandle, Coord);
	case 'D': // Cursor Backward (Left)
		Coord.X = -max(1, CsiParameters[0]);
		Coord.Y = 0;
		return VTMoveCursorRelative(ConsoleHandle, Coord);
	case 'E': // Cursor Next Line
		return VTMoveCursorVerticalRelativeWithHorizontalReset(
			ConsoleHandle,
			max(1, CsiParameters[0]));
	case 'F': // Cursor Previous Line
		return VTMoveCursorVerticalRelativeWithHorizontalReset(
			ConsoleHandle,
			-max(1, CsiParameters[0]));
	case 'G': // Cursor Horizontal Absolute
		return VTSetCursorHorizontalPosition(ConsoleHandle, max(1, CsiParameters[0]));
	case 'd': // Vertical Line Position Absolute
		return VTSetCursorVerticalPosition(ConsoleHandle, max(1, CsiParameters[0]));
	case 'H': // Cursor Position
	case 'f': // Horizontal Vertical Position
		Coord.Y = max(1, CsiParameters[0]);
		Coord.X = max(1, CsiParameters[1]);
		return VTSetCursorPosition(ConsoleHandle, Coord);
	case 'J': // Erase in Display
		return ExecuteCSIErase(ConsoleHandle, CsiParameters[0], FALSE);
	case 'K': // Erase in Line
		return ExecuteCSIErase(ConsoleHandle, CsiParameters[0], TRUE);
	case 'c': // Device Attributes
		if (CsiParameters[0] == 0) {
			BOOLEAN Success;

			Success = InjectStringToConsoleInput(
				GetStdHandle(STD_INPUT_HANDLE),
				L"\x1b[?1;0c");

			ASSERT (Success);
			return Success;
		} else {
			KexLogDebugEvent(L"Invalid parameter %hu to CSI 'c' escape", CsiParameters[0]);
			KexDebugCheckpoint();
			return FALSE;
		}

		break;
	case 'h': // Set Mode (SM / DECSET)
	case 'l': // Reset Mode (RM / DECRST)
		if (IsPrivateMode) {
			ULONG ParamIndex;
			for (ParamIndex = 0; ParamIndex < max(1, ParameterCount); ++ParamIndex) {
				SHORT Param = CsiParameters[ParamIndex];
				switch (Param) {
				case 25: { // DECTCEM: Text Cursor Enable Mode
					CONSOLE_CURSOR_INFO CursorInfo;
					if (GetConsoleCursorInfo(ConsoleHandle, &CursorInfo)) {
						CursorInfo.bVisible = (Command == 'h');
						SetConsoleCursorInfo(ConsoleHandle, &CursorInfo);
					}
					break;
				}
				case 47:
				case 1047:
				case 1049: { // Alternate Screen Buffer
					if (Command == 'h') {
						VTEnterAlternateScreenBuffer(ConsoleHandle);
					} else {
						VTLeaveAlternateScreenBuffer(ConsoleHandle);
					}
					break;
				}
				case 7: { // DECAWM: use the native wrap flag, not a no-op.
					ULONG Mode;
					if (!GetConsoleMode(ConsoleHandle, &Mode)) {
						return FALSE;
					}
					if (Command == 'h') {
						Mode |= ENABLE_WRAP_AT_EOL_OUTPUT;
					} else {
						Mode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
					}
					if (!SetConsoleMode(ConsoleHandle, Mode)) {
						return FALSE;
					}
					break;
				}
				case 1:    // DECCKM
				case 12:   // Cursor blink
				case 1000: // Mouse reporting
				case 1002: // Mouse motion
				case 1003:
				case 1004:
				case 1005:
				case 1006: // SGR mouse
				case 2004: // Bracketed paste mode
					// Silently accept
					break;
				default:
					KexLogDebugEvent(L"Unhandled DEC private mode ?%hd%c", Param, Command);
					break;
				}
			}
			return TRUE;
		} else {
			// Standard ANSI modes (e.g. 4 = Insert Mode)
			return TRUE;
		}
	case 'm': // Set Graphics Rendition (SGR)
		return ExecuteCSISetGraphicsRendition(ConsoleHandle, CsiParameters, ParameterCount);
	case 's': // Save Cursor (ANSI.SYS)
		return VTGetCursorPosition(ConsoleHandle, &g_SavedCursorPosition);
	case 'u': // Restore Cursor (ANSI.SYS)
		return VTSetCursorPosition(ConsoleHandle, g_SavedCursorPosition);
	default:
		KexLogDebugEvent(L"Unrecognized CSI ANSI escape '%c'", Command);
		KexDebugCheckpoint();
		return FALSE;
	}
}

STATIC BOOLEAN ProcessEscapeSequenceAorW(
	IN	HANDLE	ConsoleHandle,
	IN	PCVOID	Buffer,
	IN	ULONG	BufferCch,
	OUT	PULONG	CchProcessedOut OPTIONAL,
	IN	BOOLEAN	Unicode)
{
	WCHAR EscapeSequence[260];
	ULONG EscapeSequenceCch;
	ULONG CchProcessed;
	ULONG Index;

	ASSERT (BufferCch != 0);

	CchProcessed = 0;
	Index = 0;

	//
	// Fetch the escape sequence into the buffer, and convert to Unicode if it
	// is in ANSI format.
	// The buffer size is the maximum reasonable length of an escape sequence.
	//

	EscapeSequenceCch = min(BufferCch, ARRAYSIZE(EscapeSequence));

	if (EscapeSequenceCch < 2) {
		// Minimum length of an escape sequence is 2 characters.
		// If it's a single ESC at the end of the buffer, save to pending buffer.
		if (BufferCch == 1 && ((Unicode && *(PCWSTR)Buffer == ESC) || (!Unicode && *(PCSTR)Buffer == ESC))) {
			g_PendingEscape.Buffer[0] = ESC;
			g_PendingEscape.Cch = 1;
			g_PendingEscape.ConsoleHandle = ConsoleHandle;
		}
		CchProcessed = EscapeSequenceCch;
		goto Exit;
	}
	
	if (Unicode) {
		RtlCopyMemory(EscapeSequence, Buffer, EscapeSequenceCch * sizeof(WCHAR));
	} else {
		UINT CodePage;

		CodePage = GetConsoleOutputCP();
		if (!CodePage) {
			CodePage = CP_ACP;
		}

		EscapeSequenceCch = MultiByteToWideChar(
			CodePage,
			0,
			(PCSTR) Buffer,
			min(BufferCch, ARRAYSIZE(EscapeSequence) - 1),
			EscapeSequence,
			ARRAYSIZE(EscapeSequence));

		ASSERT (EscapeSequenceCch != 0);

		if (EscapeSequenceCch == 0) {
			// We're screwed
			return FALSE;
		}
	}

	//
	// Parse escape sequence and act on it if necessary.
	// https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
	//

	ASSERT (EscapeSequence[0] == ESC);
	ASSERT (EscapeSequenceCch >= 2);

	CchProcessed = 2;

	switch (EscapeSequence[1]) {
	case '(':
	case ')': {
		if (EscapeSequenceCch < 3) {
			if (BufferCch == EscapeSequenceCch && EscapeSequenceCch < ARRAYSIZE(g_PendingEscape.Buffer)) {
				RtlCopyMemory(g_PendingEscape.Buffer, EscapeSequence, EscapeSequenceCch * sizeof(WCHAR));
				g_PendingEscape.Cch = EscapeSequenceCch;
				g_PendingEscape.ConsoleHandle = ConsoleHandle;
			}
			CchProcessed = EscapeSequenceCch;
			goto Exit;
		}

		CchProcessed = 3;
		if (EscapeSequence[2] == '0') {
			g_DecLineDrawing = TRUE;
		} else if (EscapeSequence[2] == 'B' || EscapeSequence[2] == 'A') {
			g_DecLineDrawing = FALSE;
		}
		break;
	}
	case 'M': {
		COORD RelativeMovement;
		// Reverse Index.
		// Move the cursor up by one line and maintain horizontal position.
		RelativeMovement.X = 0;
		RelativeMovement.Y = -1;
		VTMoveCursorRelative(ConsoleHandle, RelativeMovement);
		break;
			  }
	case '7':
		// Save Cursor Position in Memory
		VTGetCursorPosition(ConsoleHandle, &g_SavedCursorPosition);
		break;
	case '8':
		// Restore Cursor Position from Memory
		VTSetCursorPosition(ConsoleHandle, g_SavedCursorPosition);
		break;
	case '[': {
		PWCHAR Csi;
		ULONG CsiLength;
		SHORT CsiParameters[MAX_CSI_PARAMETER_COUNT] = {0};
		WCHAR Command;
		ULONG ParameterCount;
		BOOLEAN IsPrivateMode;

		// CSI

		if (EscapeSequenceCch < 3) {
			if (BufferCch == EscapeSequenceCch && EscapeSequenceCch < ARRAYSIZE(g_PendingEscape.Buffer)) {
				RtlCopyMemory(g_PendingEscape.Buffer, EscapeSequence, EscapeSequenceCch * sizeof(WCHAR));
				g_PendingEscape.Cch = EscapeSequenceCch;
				g_PendingEscape.ConsoleHandle = ConsoleHandle;
			}
			CchProcessed = EscapeSequenceCch;
			goto Exit;
		}

		//
		// Determine length of CSI sequence
		//

		Csi = &EscapeSequence[2];
		CsiLength = 0;

		until (Csi[CsiLength] >= 0x40 && Csi[CsiLength] <= 0x7E) {
			++CsiLength;

			if (2 + CsiLength >= EscapeSequenceCch) {
				if (BufferCch == EscapeSequenceCch && EscapeSequenceCch < ARRAYSIZE(g_PendingEscape.Buffer)) {
					RtlCopyMemory(g_PendingEscape.Buffer, EscapeSequence, EscapeSequenceCch * sizeof(WCHAR));
					g_PendingEscape.Cch = EscapeSequenceCch;
					g_PendingEscape.ConsoleHandle = ConsoleHandle;
				} else {
					KexLogDebugEvent(L"Unterminated CSI ANSI escape");
				}
				CchProcessed = EscapeSequenceCch;
				goto Exit;
			}
		}

		// add 1 for the terminating character, which is the command
		++CsiLength;
		CchProcessed = 2 + CsiLength;

		// final character determines command
		Command = Csi[CsiLength - 1];

		// Check for private mode prefix ('?' or '>' or '!')
		IsPrivateMode = FALSE;
		if (CsiLength > 1 && (*Csi == '?' || *Csi == '>' || *Csi == '!')) {
			IsPrivateMode = (*Csi == '?');
			++Csi;
			--CsiLength;
		}

		//
		// Parse CSI sequence parameters
		//

		Index = 0;
		ParameterCount = 0;

		while (CsiLength != 0) {
			ULONG Parameter;
			ULONG NumberCch;

			Parameter = ParseNumber(Csi, CsiLength, &NumberCch);
			Csi += NumberCch;
			CsiLength -= NumberCch;

			if (NumberCch == 0) {
				if (CsiLength > 0 && *Csi == ';') {
					// treat as default 0
					ASSUME (Parameter == 0);
				} else {
					// no more parameters
					break;
				}
			}

			if (CsiLength > 0 && *Csi == ';') {
				// skip past semicolon separator
				++Csi;
				--CsiLength;
			}

			if (ParameterCount >= ARRAYSIZE(CsiParameters)) {
				KexLogDebugEvent(L"Too many numeric parameters in CSI ANSI escape");
				goto Exit;
			}

			if (Parameter > SHRT_MAX) {
				KexLogDebugEvent(L"Malformed number in CSI ANSI escape");
				goto Exit;
			}

			CsiParameters[ParameterCount] = (SHORT) Parameter;
			++ParameterCount;
		}

		ExecuteCSI(ConsoleHandle, Command, IsPrivateMode, CsiParameters, ParameterCount);
		break;
			  }
	case ']': // OSC (Operating System Command)
	case '_': // APC (Application Program Command, e.g. Kitty graphics query)
	case 'P': // DCS (Device Control String)
	case '^': // PM  (Privacy Message)
	{
		PWCHAR Osc;
		ULONG OscLength;
		ULONG NumberOfTerminatingCharacters;

		// OSC

		//
		// Determine length of OSC sequence.
		// OSC sequences are terminated by BEL or by ST (ESC \)
		//

		Osc = &EscapeSequence[2];
		OscLength = 0;

		until (Osc[OscLength] == BEL || Osc[OscLength] == ESC) {
			++OscLength;

			if (2 + OscLength >= EscapeSequenceCch) {
				if (BufferCch == EscapeSequenceCch && EscapeSequenceCch < ARRAYSIZE(g_PendingEscape.Buffer)) {
					RtlCopyMemory(g_PendingEscape.Buffer, EscapeSequence, EscapeSequenceCch * sizeof(WCHAR));
					g_PendingEscape.Cch = EscapeSequenceCch;
					g_PendingEscape.ConsoleHandle = ConsoleHandle;
				} else {
					KexLogDebugEvent(L"Unterminated OSC ANSI escape sequence");
				}
				CchProcessed = EscapeSequenceCch;
				goto Exit;
			}
		}

		if (Osc[OscLength] == BEL) {
			// add the BEL character
			NumberOfTerminatingCharacters = 1;
		} else {
			ASSUME (Osc[OscLength] == ESC);
			NumberOfTerminatingCharacters = 2;
		}

		OscLength += NumberOfTerminatingCharacters;
		CchProcessed += OscLength;

		if (EscapeSequence[1] == ']' && (Osc[0] == '0' || Osc[0] == '2') && Osc[1] == ';') {
			BOOL Success;
			WCHAR ConsoleTitle[256];
			ULONG ConsoleTitleCch;

			// Set Window Title

			ConsoleTitleCch = OscLength - 2 - NumberOfTerminatingCharacters;
			ConsoleTitleCch = min(ConsoleTitleCch, (ULONG) ARRAYSIZE(ConsoleTitle) - 1);

			//
			// Copy and null-terminate the title string.
			// MS docs say that 255 is the maximum length.
			//

			RtlCopyMemory(
				ConsoleTitle,
				&Osc[2],
				ConsoleTitleCch * sizeof(WCHAR));

			ConsoleTitle[ConsoleTitleCch] = '\0';

			//
			// Set the console title.
			//

			Success = SetConsoleTitle(ConsoleTitle);
			ASSERT (Success);
		}

		break;
			  }
	default:
		// Unknown two character escape
		KexLogDebugEvent(L"Unrecognized two-character ANSI escape '%c'", EscapeSequence[1]);
		break;
	}

Exit:
	if (CchProcessedOut) {
		*CchProcessedOut = CchProcessed;
	}

	return TRUE;
}

STATIC BOOL WriteConsoleUnicodeText(
	IN HANDLE ConsoleHandle,
	IN PCWSTR Buffer,
	IN ULONG CchToWrite,
	OUT PULONG CchWrittenOut OPTIONAL)
{
	return WriteConsoleW(ConsoleHandle, Buffer, CchToWrite, CchWrittenOut, NULL);
}

STATIC BOOL WriteConsoleTextAorW(
	IN	HANDLE	ConsoleHandle,
	IN	PCVOID	Buffer,
	IN	ULONG	CchToWrite,
	OUT	PULONG	CchWrittenOut OPTIONAL,
	IN	BOOLEAN	Unicode)
{
	if (!g_DecLineDrawing) {
		if (Unicode) {
			return WriteConsoleUnicodeText(ConsoleHandle, (PCWSTR) Buffer, CchToWrite, CchWrittenOut);
		}
		return WriteConsoleAorW(
			ConsoleHandle,
			Buffer,
			CchToWrite,
			CchWrittenOut,
			NULL,
			Unicode);
	}

	// Translate DEC Line Drawing characters and output via WriteConsoleW
	{
		WCHAR LocalBuf[256];
		ULONG TotalWritten = 0;
		ULONG Remaining = CchToWrite;

		while (Remaining > 0) {
			ULONG Chunk = min(Remaining, (ULONG) ARRAYSIZE(LocalBuf));
			ULONG Index;
			ULONG WrittenThisChunk;
			BOOL Ok;

			if (Unicode) {
				PCWSTR Src = (PCWSTR) Buffer + TotalWritten;
				for (Index = 0; Index < Chunk; ++Index) {
					WCHAR Ch = Src[Index];
					if (Ch >= 0x5F && Ch <= 0x7E) {
						LocalBuf[Index] = DecLineDrawingTable[Ch - 0x5F];
					} else {
						LocalBuf[Index] = Ch;
					}
				}
			} else {
				PCSTR Src = (PCSTR) Buffer + TotalWritten;
				for (Index = 0; Index < Chunk; ++Index) {
					UCHAR Ch = (UCHAR) Src[Index];
					if (Ch >= 0x5F && Ch <= 0x7E) {
						LocalBuf[Index] = DecLineDrawingTable[Ch - 0x5F];
					} else {
						LocalBuf[Index] = (WCHAR) Ch;
					}
				}
			}

			Ok = WriteConsoleUnicodeText(ConsoleHandle, LocalBuf, Chunk, &WrittenThisChunk);
			if (!Ok) {
				if (CchWrittenOut) {
					*CchWrittenOut = TotalWritten;
				}
				return FALSE;
			}

			TotalWritten += WrittenThisChunk;
			Remaining -= WrittenThisChunk;
			if (WrittenThisChunk == 0) {
				break;
			}
		}

		if (CchWrittenOut) {
			*CchWrittenOut = TotalWritten;
		}

		return TRUE;
	}
}

BOOL WriteConsoleWithEscapeSequencesAorW(
	IN	HANDLE	ConsoleHandle,
	IN	PCVOID	Buffer,
	IN	ULONG	CchToWrite,
	OUT	PULONG	CchWrittenOut OPTIONAL,
	IN	PVOID	Reserved OPTIONAL,
	IN	BOOLEAN	Unicode)
{
	BOOL Success;
	ULONG CchWritten;
	ULONG CbOfChar;

	Success = TRUE;
	CchWritten = 0;
	CbOfChar = Unicode ? sizeof(WCHAR) : sizeof(CHAR);

	//
	// Handle pending escape sequence split across calls
	//
	if (g_PendingEscape.Cch > 0 && g_PendingEscape.ConsoleHandle == ConsoleHandle && CchToWrite > 0) {
		WCHAR Combined[MAX_PENDING_ESCAPE_CCH + 64];
		ULONG PendingCch = g_PendingEscape.Cch;
		ULONG AppendCch;
		ULONG TotalCombinedCch;
		ULONG CchProcessedCombined;

		RtlCopyMemory(Combined, g_PendingEscape.Buffer, PendingCch * sizeof(WCHAR));

		AppendCch = min(CchToWrite, (ULONG)(ARRAYSIZE(Combined) - PendingCch - 1));
		if (Unicode) {
			RtlCopyMemory(&Combined[PendingCch], Buffer, AppendCch * sizeof(WCHAR));
		} else {
			UINT CodePage = GetConsoleOutputCP();
			if (!CodePage) {
				CodePage = CP_ACP;
			}
			AppendCch = MultiByteToWideChar(
				CodePage,
				0,
				(PCSTR) Buffer,
				AppendCch,
				&Combined[PendingCch],
				ARRAYSIZE(Combined) - PendingCch - 1);
		}

		TotalCombinedCch = PendingCch + AppendCch;
		CchProcessedCombined = 0;

		// Clear pending before calling so ProcessEscapeSequence can refill it if still truncated
		g_PendingEscape.Cch = 0;

		ProcessEscapeSequenceAorW(
			ConsoleHandle,
			Combined,
			TotalCombinedCch,
			&CchProcessedCombined,
			TRUE);

		if (CchProcessedCombined > PendingCch) {
			ULONG ConsumedFromBuffer = CchProcessedCombined - PendingCch;
			ConsumedFromBuffer = min(ConsumedFromBuffer, CchToWrite);

			CchWritten += ConsumedFromBuffer;
			CchToWrite -= ConsumedFromBuffer;
			Buffer = RVA_TO_VA(Buffer, ConsumedFromBuffer * CbOfChar);
		} else if (g_PendingEscape.Cch == 0 && TotalCombinedCch >= MAX_PENDING_ESCAPE_CCH) {
			// Sequence could not be completed and exceeded max pending size, discard
			KexLogDebugEvent(L"Discarding oversized pending escape sequence");
		}
	}

	until (CchToWrite == 0) {
		ULONG CchToNextEscape;

		//
		// Write to the console until we hit an escape sequence or hit the
		// end of the input buffer.
		//

		CchToNextEscape = GetCchToNextEscapeOrEndOfBufferAorW(
			Buffer,
			CchToWrite,
			Unicode);

		if (CchToNextEscape > 0) {
			ULONG CchWrittenThisCall;

			Success = WriteConsoleTextAorW(
				ConsoleHandle,
				Buffer,
				CchToNextEscape,
				&CchWrittenThisCall,
				Unicode);

			if (!Success) {
				goto Exit;
			}

			CchWritten += CchWrittenThisCall;
			CchToWrite -= CchWrittenThisCall;
			Buffer = RVA_TO_VA(Buffer, CchWrittenThisCall * CbOfChar);
		}

		//
		// If there's still more characters, that means we've hit an escape
		// sequence.
		//

		if (CchToWrite > 0) {
			ULONG CchProcessedThisCall;

			Success = ProcessEscapeSequenceAorW(
				ConsoleHandle,
				Buffer,
				CchToWrite,
				&CchProcessedThisCall,
				Unicode);

			if (!Success) {
				goto Exit;
			}

			CchWritten += CchProcessedThisCall;
			CchToWrite -= CchProcessedThisCall;
			Buffer = RVA_TO_VA(Buffer, CchProcessedThisCall * CbOfChar);
		}
	}

Exit:
	if (CchWrittenOut) {
		*CchWrittenOut = CchWritten;
	}

	return Success;
}