import ctypes as c, ctypes.wintypes as w, json, time, sys
k=c.WinDLL("kernel32",use_last_error=True)
class Coord(c.Structure): _fields_=[("X",w.SHORT),("Y",w.SHORT)]
class Rect(c.Structure): _fields_=[("Left",w.SHORT),("Top",w.SHORT),("Right",w.SHORT),("Bottom",w.SHORT)]
class Info(c.Structure): _fields_=[("size",Coord),("cursor",Coord),("attributes",w.WORD),("window",Rect),("maximum",Coord)]
class Cell(c.Structure): _fields_=[("char",w.WCHAR),("attributes",w.WORD)]
class Key(c.Structure): _fields_=[("down",w.BOOL),("repeat",w.WORD),("vk",w.WORD),("scan",w.WORD),("char",w.WCHAR),("state",w.DWORD)]
class Event(c.Union): _fields_=[("key",Key),("padding",c.c_byte*16)]
class Record(c.Structure): _fields_=[("kind",w.WORD),("event",Event)]
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE]; k.CreateFileW.restype=w.HANDLE
k.GetConsoleScreenBufferInfo.argtypes=[w.HANDLE,c.POINTER(Info)]
k.ReadConsoleOutputW.argtypes=[w.HANDLE,c.POINTER(Cell),Coord,Coord,c.POINTER(Rect)]
k.WriteConsoleInputW.argtypes=[w.HANDLE,c.POINTER(Record),w.DWORD,c.POINTER(w.DWORD)]
k.CloseHandle.argtypes=[w.HANDLE]
k.FreeConsole()
if not k.AttachConsole(int(sys.argv[1])): raise c.WinError(c.get_last_error())
out=k.CreateFileW("CONOUT$",0xc0000000,3,None,3,0,None)
inp=k.CreateFileW("CONIN$",0xc0000000,3,None,3,0,None)
def screen():
    info=Info()
    if not k.GetConsoleScreenBufferInfo(out,c.byref(info)): raise c.WinError(c.get_last_error())
    rect=info.window
    width=rect.Right-rect.Left+1; height=rect.Bottom-rect.Top+1
    cells=(Cell*(width*height))()
    if not k.ReadConsoleOutputW(out,cells,Coord(width,height),Coord(0,0),c.byref(rect)): raise c.WinError(c.get_last_error())
    return cells,width,height
def key(ch,vk=0,state=0):
    records=(Record*2)()
    for i in range(2):
        records[i].kind=1; records[i].event.key=Key(i==0,1,vk,0,ch,state)
    count=w.DWORD()
    if not k.WriteConsoleInputW(inp,records,2,c.byref(count)): raise c.WinError(c.get_last_error())
before,width,height=screen()
placeholder=next((i for i in range(len(before)-4) if "".join(x.char for x in before[i:i+4])=="Ask "),None)
result={"pid":int(sys.argv[1]),"inputCP":k.GetConsoleCP(),"outputCP":k.GetConsoleOutputCP(),"size":[width,height],"placeholderFound":placeholder is not None}
if placeholder is not None:
    key(chr(0x7ee7)); key(chr(0x7eed)); time.sleep(1.5)
    after,width,height=screen()
    cjk=[i for i,x in enumerate(after) if x.char==chr(0x7ee7)]
    if cjk:
        start=cjk[0]; segment=after[start:start+6]
        result["inputCells"]=[{"char":ord(x.char) if x.char else 0,"attr":x.attributes} for x in segment]
        result["noResidualS"]=all(x.char!="s" for x in segment[:4])
        result["hasBothChinese"]=any(x.char==chr(0x7eed) for x in segment[:4])
    else: result["inputNotFound"]=True
result["finalInputCP"]=k.GetConsoleCP(); result["finalOutputCP"]=k.GetConsoleOutputCP()
with open(r"D:\project\VxKex-NEXT\docx\opencode-runtime-tui-validation-20261101.json","w",encoding="utf-8") as f: json.dump(result,f,indent=2)
k.CloseHandle(out);k.CloseHandle(inp);k.FreeConsole()
print(json.dumps(result))
