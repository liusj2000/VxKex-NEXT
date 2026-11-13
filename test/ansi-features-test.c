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

typedef BOOL (WINAPI *EXT_SETCONSOLEOUTPUTCP)(UINT);
typedef BOOL (WINAPI *EXT_WRITEFILE)(HANDLE, const VOID *, DWORD, LPDWORD, LPOVERLAPPED);

static void t8_begin(HANDLE conout, COORD pos)
{
    DWORD n = 0;

    FillConsoleOutputCharacterW(conout, L' ', 80, pos, &n);
    SetConsoleCursorPosition(conout, pos);
}

// "line" must have room for 64 WCHARs. A double-width (DBCS) character is
// returned as ONE WCHAR, so compare by character index, not by column.
static void t8_read(HANDLE conout, COORD pos, WCHAR *line)
{
    DWORD n = 0;
    DWORD i;

    RtlZeroMemory(line, 64 * sizeof(WCHAR));
    ReadConsoleOutputCharacterW(conout, line, 40, pos, &n);
    printf("    read %lu chars:", n);
    for (i = 0; i < n && i < 14; ++i) {
        printf(" %04X", line[i]);
    }
    printf("\n");
}

static SHORT t8_cursor_x(HANDLE conout)
{
    CONSOLE_SCREEN_BUFFER_INFO info;

    RtlZeroMemory(&info, sizeof(info));
    GetConsoleScreenBufferInfo(conout, &info);
    return info.dwCursorPosition.X;
}

static void RunTest8(
    HMODULE dll,
    HANDLE conout,
    EXT_SETCONSOLEMODE setmode,
    EXT_WRITECONSOLEA write_a,
    DWORD vtMode)
{
    // "<cyan>U+250C<reset> Connect<green>U+25C7<reset>U+25A0 U+25A0" as UTF-8
    static const char prompt[] =
        "\x1b[36m\xe2\x94\x8c\x1b[39m Connect\x1b[32m\xe2\x97\x87\x1b[39m\xe2\x96\xa0\xe2\x96\xa0";
    EXT_SETCONSOLEOUTPUTCP kx_setocp;
    EXT_SETCONSOLEOUTPUTCP native_setocp;
    EXT_WRITEFILE kx_writefile;
    CONSOLE_SCREEN_BUFFER_INFO sbi8;
    COORD pos;
    WCHAR line[64];
    DWORD written;
    DWORD promptCb;
    UINT origOutCP;

    printf("\n--- Test 8: UTF-8 console output while the real output code page is 936 ---\n");

    kx_setocp = (EXT_SETCONSOLEOUTPUTCP) GetProcAddress(dll, "SetConsoleOutputCP");
    kx_writefile = (EXT_WRITEFILE) GetProcAddress(dll, "WriteFile");
    native_setocp = (EXT_SETCONSOLEOUTPUTCP) GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetConsoleOutputCP");

    CHECK(kx_setocp != NULL);
    CHECK(kx_writefile != NULL);
    CHECK(native_setocp != NULL);
    if (!kx_setocp || !kx_writefile || !native_setocp) {
        return;
    }

    // Negative control: fails while SetConsoleOutputCP is still a plain
    // forwarder to kernel32 (KxBase.dll without the fix).
    CHECK(kx_setocp != native_setocp);

    promptCb = (DWORD) strlen(prompt);
    origOutCP = GetConsoleOutputCP();

    RtlZeroMemory(&sbi8, sizeof(sbi8));
    GetConsoleScreenBufferInfo(conout, &sbi8);
    pos.X = 0;
    pos.Y = (SHORT) (sbi8.srWindow.Top + 12);

    // The app asks for UTF-8 through KxBase, then something else (the
    // PowerShell 936 guard) switches the real console back to 936.
    CHECK(kx_setocp(65001));
    CHECK(native_setocp(936));
    printf("real output CP is now %u (expected 936)\n", GetConsoleOutputCP());
    CHECK(GetConsoleOutputCP() == 936);

    printf("8a: whole UTF-8 prompt through WriteConsoleA\n");
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, prompt, promptCb, &written, NULL));
    printf("    expected bytes=%lu written=%lu\n", promptCb, written);
    CHECK(written == promptCb);
    t8_read(conout, pos, line);
    CHECK(wcsncmp(line, L"\x250C Connect\x25C7\x25A0\x25A0", 12) == 0);

    printf("8b: one UTF-8 character split over three writes\n");
    t8_begin(conout, pos);
    written = 99;
    CHECK(write_a(conout, "\xE2", 1, &written, NULL) && written == 1);
    written = 99;
    CHECK(write_a(conout, "\x94", 1, &written, NULL) && written == 1);
    written = 99;
    CHECK(write_a(conout, "\x8C" "ab", 3, &written, NULL) && written == 3);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x250C && line[1] == L'a' && line[2] == L'b');

    printf("8c: split inside a character that sits between two colour sequences\n");
    t8_begin(conout, pos);
    written = 99;
    CHECK(write_a(conout, "\x1b[32m\xE2\x97", 7, &written, NULL) && written == 7);
    written = 99;
    CHECK(write_a(conout, "\x87\x1b[39mZ", 7, &written, NULL) && written == 7);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x25C7 && line[1] == L'Z');

    printf("8d: UTF-8 Chinese keeps the double cell width of code page 936\n");
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xE4\xB8\xAD\xE6\x96\x87" "g", 7, &written, NULL) && written == 7);
    t8_read(conout, pos, line);
    printf("    cursor X=%d (expected 5)\n", t8_cursor_x(conout));
    CHECK(line[0] == 0x4E2D && line[1] == 0x6587 && line[2] == L'g');
    CHECK(t8_cursor_x(conout) == 5);

    printf("8e: whole UTF-8 prompt through WriteFile\n");
    t8_begin(conout, pos);
    written = 0;
    CHECK(kx_writefile(conout, prompt, promptCb, &written, NULL));
    printf("    expected bytes=%lu written=%lu\n", promptCb, written);
    CHECK(written == promptCb);
    t8_read(conout, pos, line);
    CHECK(wcsncmp(line, L"\x250C Connect\x25C7\x25A0\x25A0", 12) == 0);

    printf("8f: same rule while VT processing is switched off\n");
    CHECK(setmode(conout, vtMode & ~0x0004));
    t8_begin(conout, pos);
    written = 0;
    CHECK(kx_writefile(conout, "\xe2\x94\x8c Done", 8, &written, NULL) && written == 8);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x250C && line[1] == L' ' && line[2] == L'D');
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xe2\x97\x87" "X", 4, &written, NULL) && written == 4);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x25C7 && line[1] == L'X');
    CHECK(setmode(conout, vtMode));

    // 8g protects EXISTING behaviour: an app that asked for 936 (or never
    // asked) keeps the native WriteConsoleA path byte for byte.
    printf("8g: legacy GBK output is untouched when the app asked for 936\n");
    CHECK(kx_setocp(936));

    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xD6\xD0\xCE\xC4" "ab", 6, &written, NULL));
    printf("    GBK written=%lu (expected 6)\n", written);
    CHECK(written == 6);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x4E2D && line[1] == 0x6587 && line[2] == L'a' && line[3] == L'b');

    // GBK lead byte and trail byte in two separate writes.
    t8_begin(conout, pos);
    written = 99;
    CHECK(write_a(conout, "\xD6", 1, &written, NULL));
    written = 99;
    CHECK(write_a(conout, "\xD0" "ab", 3, &written, NULL));
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x4E2D && line[1] == L'a' && line[2] == L'b');

    // These four GBK bytes are also well-formed UTF-8 (U+0421 U+01BD).
    // They must still be shown as the two Chinese characters U+5C0F U+5E73.
    t8_begin(conout, pos);
    written = 0;
    CHECK(write_a(conout, "\xD0\xA1\xC6\xBD", 4, &written, NULL) && written == 4);
    t8_read(conout, pos, line);
    CHECK(line[0] == 0x5C0F && line[1] == 0x5E73);

    t8_begin(conout, pos);
    CHECK(kx_setocp(origOutCP ? origOutCP : 936));
}

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
    if (conout == INVALID_HANDLE_VALUE || !GetConsoleMode(conout, &mode)) {
        AllocConsole();
        conout = CreateFileW(
            L"CONOUT$",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);
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

    printf("\n--- Test 5: APC (Kitty graphics query) Suppression ---\n");
    // Clear line 22
    testCoord.X = 0;
    testCoord.Y = sbi.srWindow.Top + 22;
    FillConsoleOutputCharacterW(conout, L' ', 80, testCoord, &written);
    SetConsoleCursorPosition(conout, testCoord);

    // Send Kitty graphics query: \x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\ (ST terminated)
    WRITE_STR(conout, "\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\");

    // Read back line 22: It must be all spaces, 0 characters leaked!
    RtlZeroMemory(readBuf, sizeof(readBuf));
    ReadConsoleOutputCharacterW(conout, readBuf, 20, testCoord, &readChars);
    printf("Read back after APC query: '%.*ws'\n", readChars, readBuf);
    CHECK(readBuf[0] == L' ');
    CHECK(readBuf[1] == L' ');

    printf("\n--- Test 6: SGR 38/48 Extended Colors (24-bit RGB & 256-color) ---\n");
    // OpenCode focused item: fg=#fab283 (250,178,131) -> 0x0E (Bright Yellow), bg=#141414 (20,20,20) -> 0x00 (Black)
    WRITE_STR(conout, "\x1b[0;38;2;250;178;131;48;2;20;20;20m");
    GetConsoleScreenBufferInfo(conout, &sbi);
    printf("Attr after 38;2;250;178;131;48;2;20;20;20: 0x%04X\n", sbi.wAttributes);
    CHECK((sbi.wAttributes & 0xFF) == (FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN));

    // OpenCode muted text: fg=#808080 (128,128,128) -> 0x08 (Dark Gray)
    WRITE_STR(conout, "\x1b[38;2;128;128;128m");
    GetConsoleScreenBufferInfo(conout, &sbi);
    printf("Attr after 38;2;128;128;128: 0x%04X\n", sbi.wAttributes);
    CHECK((sbi.wAttributes & 0xFF) == FOREGROUND_INTENSITY);

    // OpenCode base formfield text: fg=#eeeeee (238,238,238) -> 0x07 (Standard White)
    WRITE_STR(conout, "\x1b[38;2;238;238;238m");
    GetConsoleScreenBufferInfo(conout, &sbi);
    printf("Attr after 38;2;238;238;238: 0x%04X\n", sbi.wAttributes);
    CHECK((sbi.wAttributes & 0xFF) == (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE));

    // OpenCode light theme focused blue: fg=#3b7dd8 (59,125,216) -> 0x0B (Bright Cyan)
    WRITE_STR(conout, "\x1b[38;2;59;125;216m");
    GetConsoleScreenBufferInfo(conout, &sbi);
    printf("Attr after 38;2;59;125;216: 0x%04X\n", sbi.wAttributes);
    CHECK((sbi.wAttributes & 0xFF) == (FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE));

    // 256-color test: fg=214 (orange -> 0x0E), bg=240 (mid-gray 88 -> 0x80)
    WRITE_STR(conout, "\x1b[38;5;214;48;5;240m");
    GetConsoleScreenBufferInfo(conout, &sbi);
    printf("Attr after 38;5;214;48;5;240: 0x%04X\n", sbi.wAttributes);
    CHECK((sbi.wAttributes & 0xFF) == (BACKGROUND_INTENSITY | FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN));

    // Reset attributes
    WRITE_STR(conout, "\x1b[0m");

    printf("\n--- Test 7: QueryInterruptTime & QueryInterruptTimePrecise (Go 1.24 / Antigravity 1.4.12) ---\n");
    {
        typedef VOID (WINAPI *PFN_QUERYINTERRUPTTIME)(PULONGLONG);
        PFN_QUERYINTERRUPTTIME pQueryInterruptTime = (PFN_QUERYINTERRUPTTIME) GetProcAddress(dll, "QueryInterruptTime");
        PFN_QUERYINTERRUPTTIME pQueryInterruptTimePrecise = (PFN_QUERYINTERRUPTTIME) GetProcAddress(dll, "QueryInterruptTimePrecise");
        ULONGLONG it0 = 0, itp0 = 0, itp1 = 0;

        CHECK(pQueryInterruptTime != NULL);
        CHECK(pQueryInterruptTimePrecise != NULL);
        if (pQueryInterruptTime && pQueryInterruptTimePrecise) {
            pQueryInterruptTime(&it0);
            pQueryInterruptTimePrecise(&itp0);
            Sleep(2);
            pQueryInterruptTimePrecise(&itp1);
            printf("InterruptTime=%llu, Precise0=%llu, Precise1=%llu (delta=%llu 100ns)\n", it0, itp0, itp1, itp1 - itp0);
            CHECK(it0 > 0);
            CHECK(itp0 >= it0);
            CHECK(itp1 > itp0);
        }
    }

    RunTest8(dll, conout, setmode, write_a, mode);

    printf("\n=== SUMMARY ===\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED SUCCESSFULLY! (failures = 0)\n");
        return 0;
    } else {
        printf("TESTS COMPLETED WITH %d FAILURE(S)\n", failures);
        return 1;
    }
}
