#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL (WINAPI *EXT_SETCONSOLEMODE)(HANDLE, DWORD);
typedef BOOL (WINAPI *EXT_GETCONSOLEMODE)(HANDLE, LPDWORD);
typedef BOOL (WINAPI *EXT_WRITECONSOLEA)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL (WINAPI *EXT_WRITECONSOLEW)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);

static int failures = 0;
#define CHECK(x) do { \
    if (!(x)) { \
        printf("[FAIL] line %d (error %lu): %s\n", __LINE__, GetLastError(), #x); \
        ++failures; \
    } else { \
        printf("[PASS] line %d: %s\n", __LINE__, #x); \
    } \
} while (0)

#define WRITE_STR(h, s) write_a(h, s, (DWORD)strlen(s), &written, NULL)

int main(void)
{
    HMODULE dll;
    WCHAR path[MAX_PATH];
    EXT_SETCONSOLEMODE setmode;
    EXT_GETCONSOLEMODE getmode;
    EXT_WRITECONSOLEA write_a;
    EXT_WRITECONSOLEW write_w;
    HANDLE conout;
    DWORD mode = 0;
    DWORD written = 0;
    CONSOLE_CURSOR_INFO curinfo;
    CONSOLE_SCREEN_BUFFER_INFO sbi;
    COORD testCoord;
    WCHAR readBuf[32];
    DWORD readChars = 0;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetDllDirectoryW(L"D:\\project\\VxKex-NEXT\\x64\\Release");
    LoadLibraryW(L"D:\\project\\VxKex-NEXT\\x64\\Release\\KxMi.dll");
    dll = LoadLibraryW(L"D:\\project\\VxKex-NEXT\\x64\\Release\\KxBase.dll");
    if (!dll) {
        printf("[BLOCKED] LoadLibrary KxBase.dll error %lu\n", GetLastError());
        return 2;
    }

    GetModuleFileNameW(dll, path, MAX_PATH);
    wprintf(L"Loaded: %s\n", path);

    setmode = (EXT_SETCONSOLEMODE) GetProcAddress(dll, "SetConsoleMode");
    getmode = (EXT_GETCONSOLEMODE) GetProcAddress(dll, "GetConsoleMode");
    write_a = (EXT_WRITECONSOLEA) GetProcAddress(dll, "WriteConsoleA");
    write_w = (EXT_WRITECONSOLEW) GetProcAddress(dll, "WriteConsoleW");

    CHECK(setmode != NULL);
    CHECK(getmode != NULL);
    CHECK(write_a != NULL);
    CHECK(write_w != NULL);

    conout = CreateFileW(
        L"CONOUT$",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (conout == INVALID_HANDLE_VALUE) {
        conout = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    CHECK(conout != INVALID_HANDLE_VALUE);

    // Enable VT processing
    CHECK(getmode(conout, &mode));
    printf("Native console mode: 0x%08X\n", mode);
    
    // Check what native SetConsoleMode does with 0x4!
    {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        EXT_SETCONSOLEMODE native_set = (EXT_SETCONSOLEMODE)GetProcAddress(k32, "SetConsoleMode");
        DWORD testM = mode | 0x0004;
        BOOL native_ok;
        DWORD native_err;
        SetLastError(0);
        native_ok = native_set(conout, testM);
        native_err = GetLastError();
        printf("Native SetConsoleMode(0x%08X): ret=%d, GetLastError()=%lu\n", testM, native_ok, native_err);
    }
    
    mode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
    CHECK(setmode(conout, mode));

    {
        DWORD newMode = 0;
        CHECK(getmode(conout, &newMode));
        printf("Mode after setmode: 0x%08X (has 0x4? %s)\n", newMode, (newMode & 0x4) ? "YES" : "NO");
    }

    printf("\n--- Test 1: DEC Private Mode Cursor Visibility (?25l / ?25h) ---\n");
    GetConsoleCursorInfo(conout, &curinfo);
    printf("Cursor initial: visible=%d, size=%lu\n", curinfo.bVisible, curinfo.dwSize);

    // Hide cursor
    WRITE_STR(conout, "\x1b[?25l");
    GetConsoleCursorInfo(conout, &curinfo);
    printf("Cursor after ?25l: visible=%d, size=%lu\n", curinfo.bVisible, curinfo.dwSize);
    CHECK(curinfo.bVisible == FALSE);

    // Show cursor
    WRITE_STR(conout, "\x1b[?25h");
    GetConsoleCursorInfo(conout, &curinfo);
    printf("Cursor after ?25h: visible=%d, size=%lu\n", curinfo.bVisible, curinfo.dwSize);
    CHECK(curinfo.bVisible == TRUE);

    printf("\n--- Test 2: DEC Line Drawing Character Set (ESC ( 0 / ESC ( B) ---\n");
    GetConsoleScreenBufferInfo(conout, &sbi);

    // Position cursor at a known spot: column 1, row 20
    WRITE_STR(conout, "\x1b[20;1H");

    // Enter DEC line drawing mode, write "aq", exit DEC line drawing
    WRITE_STR(conout, "\x1b(0aq\x1b(B");

    testCoord.X = 0;
    testCoord.Y = sbi.srWindow.Top + 19; // row 20 is index 19
    RtlZeroMemory(readBuf, sizeof(readBuf));
    ReadConsoleOutputCharacterW(conout, readBuf, 2, testCoord, &readChars);

    printf("Read back Unicode chars: 0x%04X, 0x%04X\n", readBuf[0], readBuf[1]);
    CHECK(readBuf[0] == 0x2592); // 'a' mapped to checkerboard
    CHECK(readBuf[1] == 0x2500); // 'q' mapped to horizontal line

    // Verify ESC ( B restores standard ASCII 'a'
    WRITE_STR(conout, "\x1b[20;1H");
    WRITE_STR(conout, "a");
    ReadConsoleOutputCharacterW(conout, readBuf, 1, testCoord, &readChars);
    printf("Read back ASCII char: '%c' (0x%04X)\n", (char)readBuf[0], readBuf[0]);
    CHECK(readBuf[0] == L'a');

    printf("\n--- Test 3: Stream Split / Chunk Boundary Incomplete Sequence ---\n");
    // Position to row 21, col 1
    WRITE_STR(conout, "\x1b[21;1H");
    // Erase line
    WRITE_STR(conout, "\x1b[2K");

    // Send split CUP sequence: Chunk 1 = "\x1b[21;", Chunk 2 = "15H" (move to row 21, col 15)
    write_a(conout, "\x1b[21;", 5, &written, NULL);
    write_a(conout, "15H", 3, &written, NULL);
    // Now write "TEST" at column 15
    WRITE_STR(conout, "TEST");

    // Read back column 1: It must NOT contain '2' or '1' or ';' or '1' or '5' or 'H'!
    testCoord.X = 0;
    testCoord.Y = sbi.srWindow.Top + 20;
    RtlZeroMemory(readBuf, sizeof(readBuf));
    ReadConsoleOutputCharacterW(conout, readBuf, 10, testCoord, &readChars);
    printf("Read back col 1-10: '%.*ws'\n", readChars, readBuf);
    CHECK(readBuf[0] == L' '); // col 1 must be space, not leaked '2' or '1'
    CHECK(readBuf[1] == L' ');

    // Read back at column 15 (index 14): It must be "TEST"
    testCoord.X = 14;
    RtlZeroMemory(readBuf, sizeof(readBuf));
    ReadConsoleOutputCharacterW(conout, readBuf, 4, testCoord, &readChars);
    printf("Read back col 15-18: '%.*ws'\n", readChars, readBuf);
    CHECK(readBuf[0] == L'T' && readBuf[1] == L'E' && readBuf[2] == L'S' && readBuf[3] == L'T');

    printf("\n--- Test 4: Alternate Screen Buffer (?1049h / ?1049l) ---\n");
    // Write marker in primary screen
    WRITE_STR(conout, "\x1b[15;1HPRIMARY_MARKER");
    testCoord.X = 0;
    testCoord.Y = sbi.srWindow.Top + 14;
    ReadConsoleOutputCharacterW(conout, readBuf, 14, testCoord, &readChars);
    CHECK(wcsncmp(readBuf, L"PRIMARY_MARKER", 14) == 0);

    // Switch to alternate screen buffer: should clear and save
    WRITE_STR(conout, "\x1b[?1049h");
    ReadConsoleOutputCharacterW(conout, readBuf, 14, testCoord, &readChars);
    printf("Alternate screen at marker pos: '%.*ws'\n", 14, readBuf);
    CHECK(readBuf[0] == L' '); // primary marker cleared in alternate view

    // Switch back to primary screen buffer: should restore primary marker!
    WRITE_STR(conout, "\x1b[?1049l");
    ReadConsoleOutputCharacterW(conout, readBuf, 14, testCoord, &readChars);
    printf("Restored primary screen: '%.*ws'\n", 14, readBuf);
    CHECK(wcsncmp(readBuf, L"PRIMARY_MARKER", 14) == 0);

    printf("\n=== SUMMARY ===\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED SUCCESSFULLY! (failures = 0)\n");
        return 0;
    } else {
        printf("TESTS COMPLETED WITH %d FAILURE(S)\n", failures);
        return 1;
    }
}
