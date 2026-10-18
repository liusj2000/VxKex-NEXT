param([switch]$Baseline,[switch]$LoaderSnaps,[switch]$SystemAccessProbe)
$ErrorActionPreference = 'Stop'
# Breakpoint RVAs below are specific to this inspected kernel32 image.
if ($LoaderSnaps) {
 $nativeHash = (Get-FileHash -LiteralPath 'C:\Windows\System32\ntdll.dll' -Algorithm SHA256).Hash
 if ($nativeHash -ne '9060811B405FBEFC0C6F7136D42AF834E5809CF54BC4F3C3CCD32E57D53DD9A1') { throw 'NTDLL image changed; do not use recorded breakpoint RVAs.' }
 $kernelHash = (Get-FileHash -LiteralPath 'C:\Windows\System32\kernel32.dll' -Algorithm SHA256).Hash
 if ($kernelHash -ne '192BE376048F5208D41BAD8C7BA2E7C9FE4A7CD00EDDA9FEF585CB21FE065A72') { throw 'Kernel32 image changed; do not use recorded breakpoint RVAs.' }
}
$nativeCode = @"
using System;
using System.Text;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class SandboxDebugProbe {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 public struct STARTUPINFO { public int cb; public string reserved,desktop,title; public int x,y,xSize,ySize,xChars,yChars,fill,flags; public short show,reserved2; public IntPtr reservedPtr,input,output,error; }
 [StructLayout(LayoutKind.Sequential)]
 public struct PROCESS_INFORMATION { public IntPtr process,thread; public uint pid,tid; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 static extern bool CreateProcessW(string app,StringBuilder command,IntPtr pa,IntPtr ta,bool inherit,uint flags,IntPtr env,string cwd,ref STARTUPINFO si,out PROCESS_INFORMATION pi);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool WaitForDebugEvent(IntPtr data,uint milliseconds);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ContinueDebugEvent(uint pid,uint tid,uint status);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern uint GetFinalPathNameByHandleW(IntPtr h,StringBuilder text,uint size,uint flags);
 [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p,IntPtr a,byte[] b,UIntPtr size,out UIntPtr read);
 [DllImport("kernel32.dll")] static extern bool DebugActiveProcessStop(uint pid);
 [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(IntPtr process,int info,IntPtr data,int size,out int returned);
 [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr process,IntPtr address,byte[] data,UIntPtr size,out UIntPtr written);
 static void EnableSnaps(IntPtr process) {
  var basic=Marshal.AllocHGlobal(48); int returned; UIntPtr transferred;
  try { if(NtQueryInformationProcess(process,0,basic,48,out returned)==0) {
   var flagAddress=new IntPtr(Marshal.ReadIntPtr(basic,8).ToInt64()+0xBC); var flags=new byte[4];
   if(ReadProcessMemory(process,flagAddress,flags,(UIntPtr)4,out transferred)) WriteProcessMemory(process,flagAddress,BitConverter.GetBytes(BitConverter.ToUInt32(flags,0)|2u),(UIntPtr)4,out transferred);
  }} finally { Marshal.FreeHGlobal(basic); }
 }
 [DllImport("kernel32.dll")] static extern bool VirtualProtectEx(IntPtr p,IntPtr a,UIntPtr n,uint protect,out uint old);
 [DllImport("kernel32.dll")] static extern bool FlushInstructionCache(IntPtr p,IntPtr a,UIntPtr n);
 [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access,bool inherit,uint id);
 [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr thread,IntPtr context);
 [DllImport("kernel32.dll")] static extern bool SetThreadContext(IntPtr thread,IntPtr context);
 static bool WriteByte(IntPtr p,long address,byte value) {
  uint old,unused; UIntPtr transferred; var a=new IntPtr(address);
  if(!VirtualProtectEx(p,a,(UIntPtr)1,0x40,out old)) return false;
  bool result=WriteProcessMemory(p,a,new byte[]{value},(UIntPtr)1,out transferred);
  VirtualProtectEx(p,a,(UIntPtr)1,old,out unused); FlushInstructionCache(p,a,(UIntPtr)1); return result;
 }
 static void Arm(IntPtr process,uint pid,long address,Dictionary<string,byte> breaks,StringBuilder text) {
  var original=new byte[1]; UIntPtr transferred;
  if(ReadProcessMemory(process,new IntPtr(address),original,(UIntPtr)1,out transferred) && WriteByte(process,address,0xCC)) {
   breaks[pid+":"+address]=original[0]; text.AppendLine("PID="+pid+" BREAK_ARM=0x"+address.ToString("X"));
  }
 }
 static string CaptureAndResume(uint tid,long address) {
  var raw=Marshal.AllocHGlobal(1264); var context=new IntPtr((raw.ToInt64()+15)&~15L); var thread=OpenThread(0x18,false,tid);
  try {
   Marshal.Copy(new byte[1232],0,context,1232); Marshal.WriteInt32(context,48,0x100003);
   if(!GetThreadContext(thread,context)) return "CONTEXT_FAILED";
   string value="EAX=0x"+((uint)Marshal.ReadInt64(context,120)).ToString("X8")+" RCX=0x"+Marshal.ReadInt64(context,128).ToString("X")+" RDX=0x"+Marshal.ReadInt64(context,136).ToString("X");
   Marshal.WriteInt64(context,248,address); return value+" RESUMED="+SetThreadContext(thread,context);
  } finally { if(thread!=IntPtr.Zero) CloseHandle(thread); Marshal.FreeHGlobal(raw); }
 }
 [DllImport("advapi32.dll")] static extern uint GetSecurityInfo(IntPtr handle,int type,uint info,out IntPtr owner,out IntPtr group,out IntPtr dacl,out IntPtr sacl,out IntPtr descriptor);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode)] static extern bool ConvertSecurityDescriptorToStringSecurityDescriptorW(IntPtr descriptor,uint revision,uint info,out IntPtr text,out uint length);
 [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
 static string SecurityOf(IntPtr handle) {
  IntPtr owner,group,dacl,sacl,descriptor,converted; uint length;
  uint result=GetSecurityInfo(handle,6,4,out owner,out group,out dacl,out sacl,out descriptor);
  if(result!=0) return "ERROR="+result;
  try { if(!ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor,1,4,out converted,out length)) return "CONVERT_FAILED";
   try { return Marshal.PtrToStringUni(converted); } finally { LocalFree(converted); }
  } finally { LocalFree(descriptor); }
 }
 [StructLayout(LayoutKind.Sequential)] struct TRUSTEE { public IntPtr multiple; public int operation,form,type; public IntPtr name; }
 [StructLayout(LayoutKind.Sequential)] struct ACCESS { public uint permissions; public int mode,inheritance; public TRUSTEE trustee; }
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode)] static extern bool ConvertStringSidToSidW(string text,out IntPtr sid);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode)] static extern uint SetEntriesInAclW(uint count,ref ACCESS entry,IntPtr oldAcl,out IntPtr newAcl);
 [DllImport("advapi32.dll")] static extern uint SetSecurityInfo(IntPtr handle,int type,uint info,IntPtr owner,IntPtr group,IntPtr dacl,IntPtr sacl);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint id);
 static string GrantSystem(IntPtr handle) {
  IntPtr owner,group,dacl,sacl,descriptor,sid,newAcl;
  uint result=GetSecurityInfo(handle,6,4,out owner,out group,out dacl,out sacl,out descriptor);
  if(result!=0) return "READ_ERROR="+result;
  try {
   if(!ConvertStringSidToSidW("S-1-5-18",out sid)) return "SID_FAILED";
   try {
    var entry=new ACCESS(); entry.permissions=0x1fffff; entry.mode=1; entry.trustee.form=0; entry.trustee.type=1; entry.trustee.name=sid;
    result=SetEntriesInAclW(1,ref entry,dacl,out newAcl);
    if(result!=0) return "ACL_ERROR="+result;
    try { return "SET_ERROR="+SetSecurityInfo(handle,6,4,IntPtr.Zero,IntPtr.Zero,newAcl,IntPtr.Zero); } finally { LocalFree(newAcl); }
   } finally { LocalFree(sid); }
  } finally { LocalFree(descriptor); }
 }
 static string PathOf(IntPtr h) { var s=new StringBuilder(2048); return GetFinalPathNameByHandleW(h,s,2048,0)>0?s.ToString():"(unavailable)"; }
 public static string Run(string exe,string args,string cwd,bool loaderSnaps,bool systemAccessProbe) {
  if (IntPtr.Size!=8) throw new Exception("x64 host required");
  var text=new StringBuilder(); var si=new STARTUPINFO(); si.cb=Marshal.SizeOf(si);
  var pi=new PROCESS_INFORMATION();
  if (!CreateProcessW(exe,new StringBuilder("\""+exe+"\" "+args),IntPtr.Zero,IntPtr.Zero,true,0x08000001,IntPtr.Zero,cwd,ref si,out pi)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
  var processes=new Dictionary<uint,IntPtr>(); var breaks=new Dictionary<string,byte>();
  var data=Marshal.AllocHGlobal(256); var clock=Stopwatch.StartNew();
  try {
   while(clock.Elapsed.TotalSeconds<35) {
    if(!WaitForDebugEvent(data,500)) continue;
    uint code=(uint)Marshal.ReadInt32(data,0), pid=(uint)Marshal.ReadInt32(data,4), tid=(uint)Marshal.ReadInt32(data,8), status=0x00010002;
    string head="PID="+pid+" ";
    try {
     if(code==3) {
      IntPtr file=Marshal.ReadIntPtr(data,16), process=Marshal.ReadIntPtr(data,24), thread=Marshal.ReadIntPtr(data,32);
      processes[pid]=process; text.AppendLine(head+"PROCESS_DACL="+SecurityOf(process)); text.AppendLine(head+"THREAD_DACL="+SecurityOf(thread));
      if(systemAccessProbe && pid!=pi.pid) { var writableProcess=OpenProcess(0x60000,false,pid); var writableThread=OpenThread(0x60000,false,tid);
       text.AppendLine(head+"SYSTEM_PROCESS_PROBE="+(writableProcess==IntPtr.Zero?"OPEN_FAILED":GrantSystem(writableProcess))); text.AppendLine(head+"SYSTEM_THREAD_PROBE="+(writableThread==IntPtr.Zero?"OPEN_FAILED":GrantSystem(writableThread))); if(writableProcess!=IntPtr.Zero)CloseHandle(writableProcess); if(writableThread!=IntPtr.Zero)CloseHandle(writableThread); }
      if(loaderSnaps && pid!=pi.pid) {
       var basic=Marshal.AllocHGlobal(48); int returned; UIntPtr transferred;
       try { if(NtQueryInformationProcess(process,0,basic,48,out returned)==0) {
        var flagAddress=new IntPtr(Marshal.ReadIntPtr(basic,8).ToInt64()+0xBC); var flags=new byte[4];
        if(ReadProcessMemory(process,flagAddress,flags,(UIntPtr)4,out transferred)) {
         var enabled=BitConverter.GetBytes(BitConverter.ToUInt32(flags,0)|2u);
         text.AppendLine(head+"LOADER_SNAPS="+WriteProcessMemory(process,flagAddress,enabled,(UIntPtr)4,out transferred));
        }
       }} finally { Marshal.FreeHGlobal(basic); }
      }
      text.AppendLine(head+"CREATE "+PathOf(file));
      if(file!=IntPtr.Zero) CloseHandle(file); CloseHandle(thread);
     } else if(code==6) {
      if(loaderSnaps && pid!=pi.pid && processes.ContainsKey(pid)) EnableSnaps(processes[pid]);
      IntPtr file=Marshal.ReadIntPtr(data,16);
      string dllPath=PathOf(file);
      if(loaderSnaps && pid!=pi.pid && dllPath.EndsWith("\\ntdll.dll",StringComparison.OrdinalIgnoreCase) && !breaks.ContainsKey(pid+":NTDLL")) {
       breaks[pid+":NTDLL"]=0; long nativeBase=Marshal.ReadIntPtr(data,24).ToInt64();
       foreach(long rva in new long[]{0x19CA0,0x19BEE}) Arm(processes[pid],pid,nativeBase+rva,breaks,text);
      }
      if(loaderSnaps && pid!=pi.pid && dllPath.EndsWith("\\kernel32.dll",StringComparison.OrdinalIgnoreCase)) {
       long moduleBase=Marshal.ReadIntPtr(data,24).ToInt64();
       foreach(long rva in new long[]{0x1541E,0x16DC2,0x16DFE,0x3C928}) Arm(processes[pid],pid,moduleBase+rva,breaks,text);
      }
      text.AppendLine(head+"DLL "+dllPath); if(file!=IntPtr.Zero) CloseHandle(file);
     } else if(code==1) {
      uint exception=(uint)Marshal.ReadInt32(data,16); long address=Marshal.ReadIntPtr(data,32).ToInt64();
      text.AppendLine(head+"EXCEPTION=0x"+exception.ToString("X8")+" ADDRESS=0x"+address.ToString("X")+" FIRST="+Marshal.ReadInt32(data,168));
      string breakpointKey=pid+":"+address;
      if(exception==0x80000003 && breaks.ContainsKey(breakpointKey)) {
       WriteByte(processes[pid],address,breaks[breakpointKey]); breaks.Remove(breakpointKey); text.AppendLine(head+"KERNEL32_BRANCH ADDRESS=0x"+address.ToString("X")+" "+CaptureAndResume(tid,address));
      }
      if(exception!=0x80000003 && exception!=0x4000001F) status=0x80010001;
     } else if(code==8 && processes.ContainsKey(pid)) {
      int unicode=Marshal.ReadInt16(data,24), count=(ushort)Marshal.ReadInt16(data,26);
      byte[] bytes=new byte[Math.Min(count*(unicode!=0?2:1),16384)]; UIntPtr read;
      if(ReadProcessMemory(processes[pid],Marshal.ReadIntPtr(data,16),bytes,(UIntPtr)bytes.Length,out read)) text.AppendLine(head+"DEBUG "+(unicode!=0?Encoding.Unicode:Encoding.Default).GetString(bytes).TrimEnd('\0'));
     } else if(code==5) {
      text.AppendLine(head+"EXIT=0x"+((uint)Marshal.ReadInt32(data,16)).ToString("X8")); processes.Remove(pid);
     }
    } finally { ContinueDebugEvent(pid,tid,status); }
    if(code==5 && processes.Count==0) return text.ToString();
   }
   text.AppendLine("TRACE_TIMEOUT");
  } finally {
   foreach(var pid in processes.Keys) DebugActiveProcessStop(pid);
   Marshal.FreeHGlobal(data); CloseHandle(pi.thread); CloseHandle(pi.process);
  }
  return text.ToString();
 }
}
"@
Add-Type -TypeDefinition $nativeCode
$binaryDir = 'D:\VSCode-win32-x64-1.107.1\data\extensions\openai.chatgpt-26.1007.21434-win32-x64\bin\windows-x86_64'
if ($Baseline) {
 $trace = [SandboxDebugProbe]::Run('C:\Windows\System32\cmd.exe','/d /c echo BASELINE_OK','D:\project\VxKex-NEXT',[bool]$LoaderSnaps,[bool]$SystemAccessProbe)
 $outputName = 'codex-sandbox-baseline-trace-20261018.txt'
} else {
 $trace = [SandboxDebugProbe]::Run((Join-Path $binaryDir 'codex.exe'),'-c windows.sandbox="unelevated" sandbox -C D:\project\VxKex-NEXT -P :workspace -- C:\Windows\System32\cmd.exe /d /c echo SANDBOX_TRACE_OK','D:\project\VxKex-NEXT',[bool]$LoaderSnaps,[bool]$SystemAccessProbe)
 $outputName = 'codex-sandbox-debug-trace-20261018.txt'
}
if ($LoaderSnaps) { $outputName = $outputName.Replace('-trace-','-loader-trace-') }
if ($SystemAccessProbe) { $outputName = $outputName.Replace('-trace-','-system-acl-trace-') }
$tracePath = Join-Path 'D:\project\VxKex-NEXT\docx' $outputName
[IO.File]::WriteAllText($tracePath,$trace,[Text.UTF8Encoding]::new($false))
$trace
'TRACE_FILE=' + $tracePath