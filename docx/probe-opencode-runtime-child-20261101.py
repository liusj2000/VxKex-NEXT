import ctypes, json, sys, time
k = ctypes.windll.kernel32
samples = []
for _ in range(3):
    k.SetConsoleCP(65001)
    k.SetConsoleOutputCP(65001)
    time.sleep(0.5)
    samples.append([k.GetConsoleCP(), k.GetConsoleOutputCP()])
with open(r"D:\project\VxKex-NEXT\docx\opencode-runtime-child-20261101.json", "w", encoding="utf-8") as f:
    json.dump({"argv": sys.argv[1:], "samples": samples}, f, ensure_ascii=True, indent=2)
sys.exit(23 if all(x == [936,936] for x in samples) else 24)
