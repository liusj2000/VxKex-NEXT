#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <io.h>

typedef BOOL (WINAPI *WRITE_W)(HANDLE, const VOID *, DWORD, LPDWORD, LPVOID);
typedef BOOL (WINAPI *SET_MODE)(HANDLE, DWORD);
static FILE *logfile;
static HANDLE output;
static WRITE_W write_w;
static SET_MODE set_mode;
static int failures;

#define CHECK(x) do { if (!(x)) { fprintf(logfile, "FAIL line=%d err=%lu %s\n", __LINE__, GetLastError(), #x); ++failures; } } while (0)

static void write_text(const WCHAR *s)
{
    DWORD n = 0, count = (DWORD)wcslen(s);
    CHECK(write_w(output, s, count, &n, NULL));
    CHECK(n == count);
}
static void dump(const char *label)
{
    CONSOLE_SCREEN_BUFFER_INFO s;
    CHAR_INFO cells[20];
    COORD size = {20,1}, origin = {0,0};
    SMALL_RECT rect = {0,0,19,0};
    int i;
    CHECK(GetConsoleScreenBufferInfo(output, &s));
    CHECK(ReadConsoleOutputW(output, cells, size, origin, &rect));
    fprintf(logfile, "%s cursor=%d,%d window=%d,%d,%d,%d size=%d,%d attrs=%04x cells=",
        label, s.dwCursorPosition.X, s.dwCursorPosition.Y,
        s.srWindow.Left, s.srWindow.Top, s.srWindow.Right, s.srWindow.Bottom,
        s.dwSize.X, s.dwSize.Y, s.wAttributes);
    for (i=0;i<20;++i) fprintf(logfile, "%04x/%04x ", cells[i].Char.UnicodeChar, cells[i].Attributes);
    fprintf(logfile, "\n");
}
static void reset(void)
{
    COORD p={0,0};
    DWORD n;
    SMALL_RECT rect={0,0,79,24};
    CHECK(SetConsoleCursorPosition(output,p));
    CHECK(SetConsoleWindowInfo(output,TRUE,&rect));
    CHECK(FillConsoleOutputCharacterW(output,L' ',80*100,p,&n));
    CHECK(FillConsoleOutputAttribute(output,7,80*100,p,&n));
    CHECK(SetConsoleTextAttribute(output,7));
}
int wmain(int argc, WCHAR **argv)
{
    HMODULE dll;
    SECURITY_ATTRIBUTES sa={sizeof(SECURITY_ATTRIBUTES),NULL,TRUE};
    COORD size={80,100}, pos;
    DWORD cp, n, originalCp = GetConsoleOutputCP();
    WCHAR path[MAX_PATH], row[81];
    CONSOLE_SCREEN_BUFFER_INFO s;
    CONSOLE_FONT_INFOEX font = {0};
    int i;
    if(argc==3 && (wcscmp(argv[1],L"emit")==0 || wcscmp(argv[1],L"emit-fixed")==0)) {
        DWORD emitted;
        HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleOutputCP((UINT)_wtoi(argv[2]));
        if(wcscmp(argv[1],L"emit-fixed")==0) {
            HMODULE candidate=LoadLibraryW(L"D:\\project\\VxKex-NEXT\\x64\\Release\\KxBase.dll");
            if(!candidate)return 2;
            write_w=(WRITE_W)GetProcAddress(candidate,"WriteConsoleW");
            set_mode=(SET_MODE)GetProcAddress(candidate,"SetConsoleMode");
            if(!write_w || !set_mode || !set_mode(h,7))return 2;
            if(!write_w(h,L"BEGIN\x68c0\x67e5\x9879\x76ee\x72b6\x6001gEND\r\n",17,&emitted,NULL))return 2;
        } else {
            WriteConsoleW(h,L"BEGIN\x68c0\x67e5\x9879\x76ee\x72b6\x6001gEND\r\n",17,&emitted,NULL);
        }

        Sleep(1500);
        return 0;
    }
    if (argc == 3) {
        STARTUPINFOW si = {0};
        PROCESS_INFORMATION pi = {0};
        WCHAR exe[MAX_PATH], command[2048];
        DWORD code;
        GetModuleFileNameW(NULL,exe,MAX_PATH);
        _snwprintf_s(command,2048,_TRUNCATE,L"\"%s\" \"%s\" \"%s\" child",exe,argv[1],argv[2]);
        si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
        if(!CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_NEW_CONSOLE,NULL,NULL,&si,&pi)) {
            fprintf(stderr,"CreateProcess error=%lu\n",GetLastError());return 2;
        }
        CloseHandle(pi.hThread);
        if(WaitForSingleObject(pi.hProcess,15000)!=WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess,3);CloseHandle(pi.hProcess);return 3;
        }
        GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);return (int)code;
    }
    if (argc != 4) return 2;
    _wfopen_s(&logfile,argv[2],L"w");
    if (!logfile) { fprintf(stderr, "log open failed: %ls error=%lu\n", argv[2], GetLastError()); return 2; }
    CHECK(SetHandleInformation((HANDLE)_get_osfhandle(_fileno(logfile)),HANDLE_FLAG_INHERIT,0));
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    /* Parent launcher creates a hidden console; never touch an inherited one. */
    output=CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CONSOLE_TEXTMODE_BUFFER,NULL);
    if(output==INVALID_HANDLE_VALUE) { fprintf(logfile,"BLOCKED no console %lu\n",GetLastError()); fclose(logfile); return 2; }
    CHECK(SetConsoleScreenBufferSize(output,size));
    CHECK(SetConsoleActiveScreenBuffer(output));

    font.cbSize=sizeof(font);
    CHECK(GetCurrentConsoleFontEx(output,FALSE,&font));
    fprintf(logfile,"font-before=%ls family=%u size=%d,%d\n",font.FaceName,font.FontFamily,font.dwFontSize.X,font.dwFontSize.Y);
    wcscpy_s(font.FaceName,LF_FACESIZE,L"Consolas");
    font.FontFamily=54; font.dwFontSize.X=0; font.dwFontSize.Y=16; font.FontWeight=400;
    CHECK(SetCurrentConsoleFontEx(output,FALSE,&font));
    CHECK(GetCurrentConsoleFontEx(output,FALSE,&font));
    fprintf(logfile,"font-after=%ls family=%u size=%d,%d\n",font.FaceName,font.FontFamily,font.dwFontSize.X,font.dwFontSize.Y);
    if(wcscmp(argv[1],L"features")==0) {
        STARTUPINFOW si={0};
        PROCESS_INFORMATION pi={0};
        WCHAR command[]=L"\"D:\\project\\VxKex-NEXT\\test\\x64\\Release\\ansi-features-test.exe\"";
        DWORD code;
        HANDLE childLog=NULL;
        SetConsoleOutputCP(65001);
        CHECK(DuplicateHandle(GetCurrentProcess(),(HANDLE)_get_osfhandle(_fileno(logfile)),
            GetCurrentProcess(),&childLog,0,TRUE,DUPLICATE_SAME_ACCESS));
        si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;
        si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput=childLog;si.hStdError=childLog;
        fflush(logfile);
        CHECK(CreateProcessW(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&si,&pi));
        if(!pi.hProcess) { if(childLog)CloseHandle(childLog);fclose(logfile);return 2; }
        CloseHandle(pi.hThread);
        if(WaitForSingleObject(pi.hProcess,10000)!=WAIT_OBJECT_0) {
            TerminateProcess(pi.hProcess,3);
            WaitForSingleObject(pi.hProcess,1000);
            ++failures;
        }
        GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
        CloseHandle(childLog);
        fseek(logfile,0,SEEK_END);
        fprintf(logfile,"child-exit=%lu\n",code);
        CHECK(code==0);
        fprintf(logfile,"failures=%d\n",failures);
        fclose(logfile);
        return failures?1:0;
    }
    if (wcscmp(argv[1],L"native")==0) {
        CHECK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)GetConsoleOutputCP, &dll));
    } else {
        dll=LoadLibraryW(argv[1]);
    }
    if(!dll) { fprintf(logfile,"BLOCKED load %lu\n",GetLastError()); fclose(logfile); return 2; }
    GetModuleFileNameW(dll,path,MAX_PATH);
    fprintf(logfile,"DLL=%ls\n",path);
    write_w=(WRITE_W)GetProcAddress(dll,"WriteConsoleW");
    set_mode=(SET_MODE)GetProcAddress(dll,"SetConsoleMode");
    CHECK(write_w && set_mode);
    if(!write_w || !set_mode) return 2;
    
    for (i=0;i<2;++i) {
        cp=i?65001:936;
        CHECK(SetConsoleOutputCP(cp));
        fprintf(logfile,"CP=%lu\n",cp);
        reset();
        if(wcscmp(argv[1],L"native")==0) {
            CHECK(!set_mode(output,3|4)); CHECK(set_mode(output,3));
        } else { CHECK(set_mode(output,3|4)); }
        write_text(L"\x68c0\x67e5\x9879\x76ee\x72b6\x6001g");
        dump("Chinese");
        CHECK(GetConsoleScreenBufferInfo(output,&s));
        if(wcscmp(argv[1],L"native")!=0 || cp==936) CHECK(s.dwCursorPosition.X==13);
        if(cp==65001 && wcscmp(argv[1],L"native")!=0) {
            CHAR_INFO checkCells[4];
            COORD grid={4,1}, zero={0,0};
            SMALL_RECT area={0,0,3,0};
            reset();
            write_text(L"\x68c0\x2500g");
            CHECK(GetConsoleScreenBufferInfo(output,&s));
            CHECK(s.dwCursorPosition.X==4);
            CHECK(ReadConsoleOutputW(output,checkCells,grid,zero,&area));
            CHECK(checkCells[0].Char.UnicodeChar==0x68c0);
            CHECK(checkCells[0].Attributes & COMMON_LVB_LEADING_BYTE);
            CHECK(checkCells[1].Attributes & COMMON_LVB_TRAILING_BYTE);
            CHECK(checkCells[2].Char.UnicodeChar==0x2500);
            CHECK(checkCells[3].Char.UnicodeChar==L'g');
            write_text(L"\x1b[1;1HA\x1b[K");
            CHECK(ReadConsoleOutputW(output,checkCells,grid,zero,&area));
            CHECK(checkCells[0].Char.UnicodeChar==L'A');
            CHECK(checkCells[1].Char.UnicodeChar==L' ');
            CHECK(!(checkCells[0].Attributes & (COMMON_LVB_LEADING_BYTE|COMMON_LVB_TRAILING_BYTE)));
            CHECK(!(checkCells[1].Attributes & (COMMON_LVB_LEADING_BYTE|COMMON_LVB_TRAILING_BYTE)));
            write_text(L"\x1b[?7l");
            pos.X=78;pos.Y=1;CHECK(SetConsoleCursorPosition(output,pos));
            write_text(L"\x68c0");
            CHECK(GetConsoleScreenBufferInfo(output,&s));
            CHECK(s.dwCursorPosition.X==79 && s.dwCursorPosition.Y==1);
            write_text(L"\x67e5"); // clipped at the right edge when wrapping is off
            CHECK(GetConsoleScreenBufferInfo(output,&s));
            CHECK(s.dwCursorPosition.X==79 && s.dwCursorPosition.Y==1);
            write_text(L"\x1b[?7h");
            pos.X=79;pos.Y=2;CHECK(SetConsoleCursorPosition(output,pos));
            write_text(L"\x68c0");
            CHECK(GetConsoleScreenBufferInfo(output,&s));
            CHECK(s.dwCursorPosition.X==2 && s.dwCursorPosition.Y==3);
            pos.X=78;pos.Y=3;CHECK(SetConsoleCursorPosition(output,pos));
            write_text(L"\x68c0");
            CHECK(GetConsoleScreenBufferInfo(output,&s));
            CHECK(s.dwCursorPosition.X==0 && s.dwCursorPosition.Y==4);

        }
        if(cp==65001) {
            CHAR_INFO pair[2];
            COORD grid={2,1}, zero={0,0};
            SMALL_RECT area={0,0,1,0};
            pair[0].Char.UnicodeChar=pair[1].Char.UnicodeChar=0x68c0;
            pair[0].Attributes=7|COMMON_LVB_LEADING_BYTE;
            pair[1].Attributes=7|COMMON_LVB_TRAILING_BYTE;
            CHECK(WriteConsoleOutputW(output,pair,grid,zero,&area));
            dump("explicit-wide-pair");
        }
        reset();
        write_text(L"\x1b[?7l");
        pos.X=79;pos.Y=24;
        CHECK(SetConsoleCursorPosition(output,pos));
        write_text(L"XY");
        CHECK(GetConsoleScreenBufferInfo(output,&s));
        fprintf(logfile,"no-wrap cursor=%d,%d expected=79,24\n",s.dwCursorPosition.X,s.dwCursorPosition.Y);
        if(wcscmp(argv[1],L"native")!=0) CHECK(s.dwCursorPosition.X==79 && s.dwCursorPosition.Y==24);
        write_text(L"\x1b[?7h");
        reset();
        write_text(L"\x1b[?1049h");
        for(n=0;n<80;++n)row[n]=L'Z';
        row[80]=0;
        pos.X=0;pos.Y=24;
        CHECK(SetConsoleCursorPosition(output,pos));
        write_text(row);
        CHECK(GetConsoleScreenBufferInfo(output,&s));
        fprintf(logfile,"alt-full-last-row cursor=%d,%d viewport-top=%d\n",s.dwCursorPosition.X,s.dwCursorPosition.Y,s.srWindow.Top);
        write_text(L"\x1b[?1049l");
        dump("restored");
        CHECK(set_mode(output,3));
    }
    SetConsoleOutputCP(originalCp);
    CloseHandle(output);
    fprintf(logfile,"failures=%d\n",failures);
    fclose(logfile);
    return failures?1:0;
}
