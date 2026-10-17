#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *CREATEPC)(COORD,HANDLE,HANDLE,DWORD,HANDLE*);
typedef HRESULT (WINAPI *RESIZEPC)(HANDLE,COORD);
typedef VOID (WINAPI *CLOSEPC)(HANDLE);
typedef BOOL (WINAPI *INITATTR)(LPPROC_THREAD_ATTRIBUTE_LIST,DWORD,DWORD,PSIZE_T);
typedef BOOL (WINAPI *UPDATEATTR)(LPPROC_THREAD_ATTRIBUTE_LIST,DWORD,DWORD_PTR,PVOID,SIZE_T,PVOID,PSIZE_T);
typedef VOID (WINAPI *DELETEATTR)(LPPROC_THREAD_ATTRIBUTE_LIST);
typedef BOOL (WINAPI *CREATEPROC)(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION);
typedef BOOL (WINAPI *GETMODE)(HANDLE,LPDWORD);
typedef BOOL (WINAPI *SETMODE)(HANDLE,DWORD);
typedef BOOL (WINAPI *CREATEUSER)(HANDLE,LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION);
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d error %lu: %s\n",__LINE__,GetLastError(),#x); ++failures; } } while (0)

int main(void)
{
 HMODULE dll;
 WCHAR path[MAX_PATH];
 CREATEPC createpc; RESIZEPC resizepc; CLOSEPC closepc;
 INITATTR initattr; UPDATEATTR updateattr; DELETEATTR deleteattr;
 CREATEPROC createproc; GETMODE getmode; SETMODE setmode;
 CREATEUSER createuser;
 HANDLE ir,iw,orr,ow,pc,con;
 COORD size = {80,25};
 SIZE_T bytes;
 LPPROC_THREAD_ATTRIBUTE_LIST list;
 STARTUPINFOEXW si,before;
 PROCESS_INFORMATION pi;
 WCHAR command[128];
 char output[1024];
 DWORD n,available,total,start,mode,actual,baseline,after;
 int i;

 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 dll=LoadLibraryW(L"D:\\project\\VxKex-NEXT\\x64\\Release\\KxBase.dll");
 if (!dll) { printf("BLOCKED LoadLibrary error %lu\n",GetLastError()); return 2; }
 GetModuleFileNameW(dll,path,MAX_PATH);
 wprintf(L"LOADED %s\n",path);
 if (_wcsicmp(path,L"D:\\project\\VxKex-NEXT\\x64\\Release\\KxBase.dll")) return 3;
 createpc=(CREATEPC)GetProcAddress(dll,"CreatePseudoConsole");
 resizepc=(RESIZEPC)GetProcAddress(dll,"ResizePseudoConsole");
 closepc=(CLOSEPC)GetProcAddress(dll,"ClosePseudoConsole");
 initattr=(INITATTR)GetProcAddress(dll,"InitializeProcThreadAttributeList");
 updateattr=(UPDATEATTR)GetProcAddress(dll,"UpdateProcThreadAttribute");
 deleteattr=(DELETEATTR)GetProcAddress(dll,"DeleteProcThreadAttributeList");
 createproc=(CREATEPROC)GetProcAddress(dll,"CreateProcessW");
 getmode=(GETMODE)GetProcAddress(dll,"GetConsoleMode");
 setmode=(SETMODE)GetProcAddress(dll,"SetConsoleMode");
 createuser=(CREATEUSER)GetProcAddress(dll,"CreateProcessAsUserW");
 if(!createpc||!resizepc||!closepc||!initattr||!updateattr||!deleteattr||!createproc||!getmode||!setmode||!GetProcAddress(dll,"CreateProcessAsUserW")) return 4;
 CHECK(CreatePipe(&ir,&iw,NULL,0)); CHECK(CreatePipe(&orr,&ow,NULL,0));
 CHECK(FAILED(createpc(size,ir,ow,0,NULL)));
 CHECK(FAILED(createpc(size,INVALID_HANDLE_VALUE,ow,0,&pc)) && !pc);
 CHECK(FAILED(createpc(size,GetCurrentThread(),ow,0,&pc)) && !pc);
 CHECK(SUCCEEDED(createpc(size,ir,ow,0,&pc)));
 CHECK(*(DWORD*)pc==0x4350584B);
 CHECK(SUCCEEDED(resizepc(pc,size)));
 CHECK(FAILED(resizepc((HANDLE)1,size)));
 bytes=0; initattr(NULL,1,0,&bytes);
 list=(LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(),0,bytes);
 for(i=0;i<100;++i) {
  CHECK(initattr(list,1,0,&bytes));
 }
 CHECK(updateattr(list,0,0x00020016,pc,sizeof(pc),NULL,NULL));
 CHECK(updateattr(list,0,0x00020016,&pc,sizeof(pc),NULL,NULL));
 CHECK(!updateattr(list,0,0x00020016,(PVOID)1,sizeof(pc),NULL,NULL));
 ZeroMemory(&si,sizeof(si)); si.StartupInfo.cb=sizeof(si); si.lpAttributeList=list;
 si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
 si.StartupInfo.hStdInput=INVALID_HANDLE_VALUE;
 si.StartupInfo.hStdOutput=INVALID_HANDLE_VALUE;
 si.StartupInfo.hStdError=INVALID_HANDLE_VALUE;
 before=si;
 ZeroMemory(&pi,sizeof(pi));
 wcscpy_s(command,128,L"C:\\Windows\\System32\\cmd.exe /d /c set /p line= & echo received");
 CHECK(createproc(NULL,command,NULL,NULL,FALSE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NEW_CONSOLE|DETACHED_PROCESS,NULL,NULL,&si.StartupInfo,&pi));
 CHECK(!memcmp(&before,&si,sizeof(si)));
 if (pi.hProcess) {
  CHECK(WriteFile(iw,"hello\r\n",7,&n,NULL));
  CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);
  if(WaitForSingleObject(pi.hProcess,0)!=WAIT_OBJECT_0) TerminateProcess(pi.hProcess,99);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  total=0; start=GetTickCount();
  while(GetTickCount()-start<3000 && total<sizeof(output)-1) {
   if(!PeekNamedPipe(orr,NULL,0,NULL,&available,NULL)) break;
   if(!available) { Sleep(10); continue; }
   if(!ReadFile(orr,output+total,sizeof(output)-1-total,&n,NULL)) break;
   total+=n;
   output[total]=0;
   if(strstr(output,"received")) break;
  }
  output[total]=0; printf("PIPE OUTPUT: %s\n",output); CHECK(strstr(output,"received")!=NULL);
 }
 closepc(pc); closepc(pc);
 CHECK(FAILED(resizepc(pc,size)));
 wcscpy_s(command,128,L"C:\\Windows\\System32\\cmd.exe /d /c exit 0");
 CHECK(!createproc(NULL,command,NULL,NULL,FALSE,EXTENDED_STARTUPINFO_PRESENT,NULL,NULL,&si.StartupInfo,&pi));
 CHECK(GetLastError()==ERROR_INVALID_HANDLE);
 deleteattr(list);
 {
  INITATTR native_init=(INITATTR)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"InitializeProcThreadAttributeList");
  HANDLE ir2,iw2,or2,ow2;
  BOOL eof_reached=FALSE;
  CHECK(native_init!=NULL);
  CHECK(CreatePipe(&ir2,&iw2,NULL,0)); CHECK(CreatePipe(&or2,&ow2,NULL,0));
  CHECK(SUCCEEDED(createpc(size,ir2,ow2,0,&pc)));
  CloseHandle(ir2); CloseHandle(ow2);
  CHECK(native_init(list,1,0,&bytes));
  CHECK(updateattr(list,0,0x00020016,pc,sizeof(pc),NULL,NULL));
  ZeroMemory(&pi,sizeof(pi));
  wcscpy_s(command,128,L"C:\\Windows\\System32\\cmd.exe /q /d /k echo C4_READY");
  CHECK(createproc(NULL,command,NULL,NULL,FALSE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NEW_CONSOLE,NULL,NULL,&si.StartupInfo,&pi));
  if(pi.hProcess) {
   total=0; start=GetTickCount();
   while(GetTickCount()-start<3000 && total<sizeof(output)-1) {
    if(!PeekNamedPipe(or2,NULL,0,NULL,&available,NULL)) break;
    if(!available) { Sleep(10); continue; }
    if(!ReadFile(or2,output+total,sizeof(output)-1-total,&n,NULL)) break;
    total+=n; output[total]=0;
    if(strstr(output,"C4_READY")) break;
   }
   CHECK(strstr(output,"C4_READY")!=NULL);
   CHECK(WaitForSingleObject(pi.hProcess,50)==WAIT_TIMEOUT);
   closepc(pc);
   CHECK(WaitForSingleObject(pi.hProcess,1000)==WAIT_OBJECT_0);
   while(ReadFile(or2,output,sizeof(output),&n,NULL)) {
    if(n==0) { eof_reached=TRUE; break; }
   }
   if(!eof_reached && GetLastError()==ERROR_BROKEN_PIPE) eof_reached=TRUE;
   CHECK(eof_reached);
   CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  } else {
   closepc(pc);
  }
  deleteattr(list);
  CloseHandle(iw2); CloseHandle(or2);
 }
 CHECK(initattr(list,1,0,&bytes));
 CHECK(SUCCEEDED(createpc(size,ir,ow,0,&pc)));
 CHECK(updateattr(list,0,0x00020016,&pc,sizeof(pc),NULL,NULL));
 ZeroMemory(&pi,sizeof(pi));
 wcscpy_s(command,128,L"C:\\Windows\\System32\\cmd.exe /d /c exit 0");
 CHECK(createuser(NULL,NULL,command,NULL,NULL,FALSE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NEW_CONSOLE,NULL,NULL,&si.StartupInfo,&pi));
 if(pi.hProcess) { CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
 closepc(pc); deleteattr(list);
 CHECK(GetHandleInformation(ir,&n)); CHECK(GetHandleInformation(ow,&n));
 GetProcessHandleCount(GetCurrentProcess(),&baseline);
 for(i=0;i<250;++i) {
  CHECK(initattr(list,1,0,&bytes));
  CHECK(SUCCEEDED(createpc(size,ir,ow,0,&pc)));
  CHECK(updateattr(list,0,0x00020016,pc,sizeof(pc),NULL,NULL));
  closepc(pc); deleteattr(list);
 }
 GetProcessHandleCount(GetCurrentProcess(),&after);
 printf("HANDLES before=%lu after=%lu\n",baseline,after); CHECK(after<=baseline+2);
 HeapFree(GetProcessHeap(),0,list);
 CloseHandle(ir); CloseHandle(iw); CloseHandle(orr); CloseHandle(ow);
 con=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(con==INVALID_HANDLE_VALUE) { AllocConsole(); con=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL); }
 if(con!=INVALID_HANDLE_VALUE && GetConsoleMode(con,&mode)) {
  HANDLE con2=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
  CHECK(setmode(con,mode|0x0004|0x0008)); CHECK(getmode(con,&actual)); CHECK((actual&0xC)==0xC);
  if(con2!=INVALID_HANDLE_VALUE) {
   CHECK(setmode(con2,mode)); CHECK(getmode(con,&actual)); CHECK((actual&0xC)==0xC);
   CloseHandle(con2);
  }
  CHECK(setmode(con,mode)); CHECK(getmode(con,&actual)); CHECK(actual==mode); CloseHandle(con);
 } else printf("SKIP console mode: no console\n");
 con=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
 if(con!=INVALID_HANDLE_VALUE && GetConsoleMode(con,&mode)) {
  CHECK(setmode(con,mode|0x0200|0x0002|0x0004|0x0008)); CHECK(getmode(con,&actual)); CHECK((actual&0x20E)==0x20E);
  CHECK(setmode(con,mode)); CHECK(getmode(con,&actual)); CHECK(actual==mode); CloseHandle(con);
 } else printf("SKIP input mode: no console\n");
 printf("RESULT failures=%d\n",failures);
 return failures ? 1:0;
}
