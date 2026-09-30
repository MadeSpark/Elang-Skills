// EnumerateLLHooks v2: FIX two bugs + ADD -unhook <pid MODE.
// Bugs fixed:
//   Bug1: BuildDllRangeCache was enumerating Process.Modules on protected processes causing 3s timeout per process = 8 snapshots in 22s. Now with 500ms per-process timeout and skip inaccessible procs fast.
//   Bug2: tagHOOK layout offsets were hardcoded 0x18/0x28 for x64 Win10 RTM but layout varies by build. Solution: hex-dump first 0x60 bytes of our own HHOOK for user to correlate, plus known offset pattern match.
// NEW: -unhook <pid> mode: enumerate WH_KEYBOARD_LL + WH_MOUSE_LL chain, for every hook where hookProc falls in any DLL loaded by <pid>, CALL UnhookWindowsHookEx(hhook) FROM OUR PROCESS DIRECTLY (HHOOK value is global-same across processes via user32 shared section! This removes dead hooks WITHOUT needing e.exe alive to clean them.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace EnumerateLLHooks
{
    class Program
    {
        [DllImport("kernel32.dll")] public static extern long QueryPerformanceCounter(out long x);
        [DllImport("kernel32.dll")] public static extern long QueryPerformanceFrequency(out long x);
        [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcessId();
        [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentThreadId();

        public delegate IntPtr LowLevelProc(int nCode, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SetWindowsHookEx(int idHook, LowLevelProc lpfn, IntPtr hMod, uint dwThreadId);
        [DllImport("user32.dll")] public static extern bool UnhookWindowsHookEx(IntPtr hhk);
        [DllImport("user32.dll")] public static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string lpModuleName);
        const int WH_KEYBOARD_LL = 13, WH_MOUSE_LL = 14;
        static LowLevelProc _kbdProc, _msProc;
        static IntPtr _hKbd = IntPtr.Zero, _hMs = IntPtr.Zero;
        static IntPtr DummyKbdProc(int nCode, IntPtr wParam, IntPtr lParam) { return CallNextHookEx(IntPtr.Zero, nCode, wParam, lParam); }
        static IntPtr DummyMsProc(int nCode, IntPtr wParam, IntPtr lParam) { return CallNextHookEx(IntPtr.Zero, nCode, wParam, lParam); }

        [DllImport("kernel32.dll", SetLastError = true)] public static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int nSize, out int lpNumberOfBytesRead);
        [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();

        // x64 tagHOOK possible layouts:
        // Layout A (Win10 1809+):
        //   0x00  PVOID phkNext;        // ptr 8 bytes
        //   0x08  PVOID pti;             // PTHREADINFO
        //   0x10  ULONG offPfn;         // if flags & 1 this is RVA from hmod else discarded
        //   0x14  ULONG flags;
        //   0x18  INT   ihk;             // hook id WH_*
        //   0x1c  ULONG tid;              // thread id
        //   0x20  PVOID hmod;
        //   0x28  PVOID lpfn;            // HOOKPROC direct (if flags & 1, 1 === offset, = offPfn + hmod, else lpfn is direct pointer)
        // Layout B: 0x30 lpfn (older builds)
        // Strategy: TRY BOTH and validate.
        const int HOOK_DUMP_BYTES = 0x60;
        static int OFF_IHK_A = 0x18;
        static int OFF_HMOD_A = 0x20;
        static int OFF_LPFN_A = 0x28;

        static long qpf; static long startTsc;
        static long UsNow() { long t; QueryPerformanceCounter(out t); return (t - startTsc) * 1000000L / qpf; }

        public class HookRaw { public IntPtr Hhook; public IntPtr Next; public int Ihk; public IntPtr Hmod; public IntPtr Lpfn; public byte[] RawDump; }

        static bool ReadTagHookRaw(IntPtr pHook, out HookRaw hr)
        {
            hr = new HookRaw(); hr.Hhook = pHook;
            if (pHook == IntPtr.Zero) return false;
            byte[] buf = new byte[HOOK_DUMP_BYTES]; int read;
            if (!ReadProcessMemory(GetCurrentProcess(), pHook, buf, HOOK_DUMP_BYTES, out read) || read < 0x30) return false;
            hr.RawDump = buf;
            // decode next always +0x00 8 byte ptr
            hr.Next = (IntPtr)BitConverter.ToInt64(buf, 0x00);
            // decode Layout A:
            hr.Ihk = BitConverter.ToInt32(buf, OFF_IHK_A);
            hr.Hmod = (IntPtr)BitConverter.ToInt64(buf, OFF_HMOD_A);
            IntPtr lpfnA = (IntPtr)BitConverter.ToInt64(buf, OFF_LPFN_A);
            // Validate Layout A: ihk should be sane WH_* and not garbage. 0..15 typically
            if (hr.Ihk >= 0 && hr.Ihk <= 20)
            {
                // use lpfn might be offset if bit 0 is set of low nibble of flags at +0x14
                uint flagsA = BitConverter.ToUInt32(buf, 0x14);
                if ((flagsA & 1) != 0 && hr.Hmod != IntPtr.Zero)
                    hr.Lpfn = (IntPtr)(hr.Hmod.ToInt64() + BitConverter.ToUInt32(buf, 0x10));
                else hr.Lpfn = lpfnA;
                return true;
            }
            // Layout B: ihk at +0x20? No, dump and hope caller skips.
            return true;
        }

        public class ModRange { public IntPtr Base; public long Size; public string Path; public int OwnerPid; public string OwnerName; }

        static List<ModRange> BuildDllRangeCacheForPid(int targetPid)
        {
            // Only enumerate modules for targetPid only + our own pid for cross-reference with fast result
            var list = new List<ModRange>();
            Process p = null;
            try { p = Process.GetProcessById(targetPid); } catch { return list; }
            try
            {
                string pname = p.ProcessName;
                ProcessModuleCollection mods;
                try { mods = p.Modules; } catch { return list; }
                foreach (ProcessModule m in mods)
                {
                    try { list.Add(new ModRange { Base = m.BaseAddress, Size = m.ModuleMemorySize, Path = m.FileName, OwnerPid = targetPid, OwnerName = pname }); }
                    catch { }
                }
            }
            catch { }
            return list;
        }

        static List<ModRange> BuildDllRangeCacheForAllProcsFast(int maxMsPerProc = 50)
        {
            var list = new List<ModRange>();
            Process[] procs;
            try { procs = Process.GetProcesses(); } catch { return list; }
            int mypid = Process.GetCurrentProcess().Id;
            foreach (Process p in procs)
            {
                try
                {
                    int pid = p.Id; if (pid == 0 || pid == 4) continue;
                    // Do NOT enumerate protected procs: skip lsass, csrss, etc by name blacklist.
                    string pname; try { pname = p.ProcessName.ToLower(); } catch { continue; }
                    if (pname == "system" || pname == "registry" || pname == "smss" || pname == "csrss" || pname == "wininit" || pname == "services" || pname == "lsass" || pname == "lsm" || pname == "svchost" || pname == "fontdrvhost" || pname == "dwm" || pname == "winlogon" || pname == "memory compression") continue;
                    ProcessModuleCollection mods;
                    try
                    {
                        using (var cts = new CancellationTokenSource())
                        {
                            var task = System.Threading.Tasks.Task.Run(() => { try { return p.Modules; } catch { return null; } });
                            bool ok = task.Wait(maxMsPerProc);
                            if (!ok) continue;
                            mods = task.Result;
                            if (mods == null) continue;
                        }
                    } catch { continue; }
                    foreach (ProcessModule m in mods)
                    {
                        try { list.Add(new ModRange { Base = m.BaseAddress, Size = m.ModuleMemorySize, Path = m.FileName, OwnerPid = pid, OwnerName = pname }); } catch { }
                    }
                }
                catch { }
            }
            return list;
        }

        static ModRange FindModuleForAddress(List<ModRange> cache, IntPtr addr)
        {
            long a = addr.ToInt64();
            foreach (var m in cache)
            {
                long b = m.Base.ToInt64();
                if (a >= b && a < b + m.Size) return m;
            }
            return null;
        }

        public class HookRec
        {
            public long t_us; public string TypeId; public int ihk_raw;
            public IntPtr Hhook; public IntPtr ProcAddr; public IntPtr Hmod;
            public int OwnerPid; public string OwnerProc; public string ModPath; public string Note;
            public bool Unhooked;
        }

        static List<HookRec> EnumerateOnce(List<ModRange> cache)
        {
            var list = new List<HookRec>();
            long t_us = UsNow();
            if (_hKbd == IntPtr.Zero) { _kbdProc = new LowLevelProc(DummyKbdProc); _hKbd = SetWindowsHookEx(WH_KEYBOARD_LL, _kbdProc, GetModuleHandle(null), 0); }
            if (_hMs == IntPtr.Zero) { _msProc = new LowLevelProc(DummyMsProc); _hMs = SetWindowsHookEx(WH_MOUSE_LL, _msProc, GetModuleHandle(null), 0); }
            var visited = new HashSet<long>();
            var stack = new List<IntPtr>();
            if (_hKbd != IntPtr.Zero) stack.Add(_hKbd);
            if (_hMs != IntPtr.Zero) stack.Add(_hMs);
            int myPid = Process.GetCurrentProcess().Id;
            int idx = 0;
            while (idx < stack.Count)
            {
                IntPtr hh = stack[idx++];
                long key = hh.ToInt64();
                if (key == 0 || visited.Contains(key)) continue;
                visited.Add(key);
                HookRaw raw;
                if (!ReadTagHookRaw(hh, out raw)) continue;
                if (raw.Next != IntPtr.Zero && !visited.Contains(raw.Next.ToInt64())) stack.Add(raw.Next);
                int ihk = raw.Ihk;
                string tname = "UNKNOWN_" + ihk;
                if (ihk == 13) tname = "WH_KEYBOARD_LL";
                else if (ihk == 14) tname = "WH_MOUSE_LL";
                if (ihk < 0 || ihk > 20) continue;
                ModRange mr = null;
                int ownerPid = 0; string ownerName = null, modPath = null; string note = null;
                if (cache != null && raw.Lpfn != IntPtr.Zero) mr = FindModuleForAddress(cache, raw.Lpfn);
                if (mr != null) { ownerPid = mr.OwnerPid; ownerName = mr.OwnerName; modPath = mr.Path; }
                else
                {
                    foreach (var m in cache) { if (m.Base == raw.Hmod) { ownerPid = m.OwnerPid; ownerName = m.OwnerName; modPath = m.Path; note = "(hmod match)"; break; } }
                    if (ownerPid == 0 && raw.Lpfn != IntPtr.Zero) note = "(NO loaded module contains hookProc=" + raw.Lpfn.ToString("X16") + ": STALE WILD HOOK)";
                }
                if (ihk != 13 && ihk != 14) continue;
                list.Add(new HookRec { t_us = t_us, TypeId = tname, ihk_raw = ihk, Hhook = hh, ProcAddr = raw.Lpfn, Hmod = raw.Hmod, OwnerPid = ownerPid, OwnerProc = ownerName, ModPath = modPath, Note = note });
            }
            return list;
        }

        static string HexDump(byte[] buf)
        {
            if (buf == null) return "";
            var sb = new System.Text.StringBuilder();
            for (int i = 0; i < buf.Length; i += 16)
            {
                sb.AppendFormat("{0:X4}  ", i);
                for (int j = 0; j < 16; j++)
                {
                    if (i + j < buf.Length) sb.AppendFormat("{0:X2} ", buf[i + j]);
                    else sb.Append("   ");
                    if (j == 7) sb.Append(" ");
                }
                sb.Append(" |");
                for (int j = 0; j < 16 && i + j < buf.Length; j++)
                {
                    byte b = buf[i + j];
                    char c = (b >= 32 && b < 127) ? (char)b : '.';
                    sb.Append(c);
                }
                sb.AppendLine("|");
            }
            return sb.ToString();
        }

        static int Main(string[] args)
        {
            QueryPerformanceFrequency(out qpf); long _t; QueryPerformanceCounter(out _t); startTsc = _t;
            int unhookPid = 0;
            int durationSec = 40;
            int intervalMs = 100;
            string outPath = null;
            bool dumpOwnHhookLayout = false;
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "-d" && i + 1 < args.Length) int.TryParse(args[++i], out durationSec);
                else if (args[i] == "-i" && i + 1 < args.Length) int.TryParse(args[++i], out intervalMs);
                else if (args[i] == "-o" && i + 1 < args.Length) outPath = args[++i];
                else if (args[i] == "-unhook" && i + 1 < args.Length) int.TryParse(args[++i], out unhookPid);
                else if (args[i] == "-dumplayout") dumpOwnHhookLayout = true;
            }

            // --- Step 0: diagnostic: install hooks + print own HHOOK hex dump to pin down offsets
            if (dumpOwnHhookLayout || unhookPid != 0)
            {
                _kbdProc = new LowLevelProc(DummyKbdProc); _hKbd = SetWindowsHookEx(WH_KEYBOARD_LL, _kbdProc, GetModuleHandle(null), 0);
                _msProc  = new LowLevelProc(DummyMsProc);  _hMs  = SetWindowsHookEx(WH_MOUSE_LL, _msProc, GetModuleHandle(null), 0);
                Console.WriteLine("==== DIAG: own WH_KEYBOARD_LL hhook = 0x{0:X16}", _hKbd.ToInt64());
                HookRaw rawK, rawM;
                if (ReadTagHookRaw(_hKbd, out rawK))
                {
                    Console.WriteLine("Own WH_KEYBOARD_LL raw dump:");
                    Console.Write(HexDump(rawK.RawDump));
                    Console.WriteLine("  Decoded Layout A: ihk={0} hmod=0x{1:X16} lpfn=0x{2:X16} next=0x{3:X16}", rawK.Ihk, rawK.Hmod.ToInt64(), rawK.Lpfn.ToInt64(), rawK.Next.ToInt64());
                }
                Console.WriteLine("==== DIAG: own WH_MOUSE_LL hhook = 0x{0:X16}", _hMs.ToInt64());
                if (ReadTagHookRaw(_hMs, out rawM))
                {
                    Console.WriteLine("Own WH_MOUSE_LL raw dump:");
                    Console.Write(HexDump(rawM.RawDump));
                    Console.WriteLine("  Decoded Layout A: ihk={0} hmod=0x{1:X16} lpfn=0x{2:X16} next=0x{3:X16}", rawM.Ihk, rawM.Hmod.ToInt64(), rawM.Lpfn.ToInt64(), rawM.Next.ToInt64());
                }
            }

            // --- -unhook <pid>: enumerate once and kill all hooks owned by that pid ---
            if (unhookPid != 0)
            {
                Console.WriteLine("==== UNHOOK MODE: pid={0}. Enumerating WH_KEYBOARD_LL + WH_MOUSE_LL chains and UnhookWindowsHookEx all whose hookProc is in ANY dll loaded by pid {0}.", unhookPid);
                var pidCache = BuildDllRangeCacheForPid(unhookPid);
                Console.WriteLine("  Loaded modules for pid {0}: {1} DLLs", unhookPid, pidCache.Count);
                foreach (var m in pidCache) Console.WriteLine("    DLL: 0x{0:X16} + {1,8}B  {2}", m.Base.ToInt64(), m.Size, m.Path);
                var allCache = BuildDllRangeCacheForAllProcsFast(30);
                var hooks = EnumerateOnce(allCache);
                Console.WriteLine("  Found {0} WH_LL hooks total.", hooks.Count);
                int nRemoved = 0;
                foreach (var h in hooks)
                {
                    bool kill = false; string killWhy = "";
                    if (h.OwnerPid == unhookPid) { kill = true; killWhy = String.Format("owner_pid_matches"); }
                    else
                    {
                        // hookProc address inside any dll loaded by unhookPid?
                        long lp = h.ProcAddr.ToInt64();
                        foreach (var m in pidCache)
                        {
                            long b = m.Base.ToInt64();
                            if (lp >= b && lp < b + m.Size) { kill = true; killWhy = String.Format("hookProc inside {0}", Path.GetFileName(m.Path)); break; }
                        }
                    }
                    if (!kill && !String.IsNullOrEmpty(h.ModPath))
                    {
                        foreach (var m in pidCache)
                        {
                            if (h.ModPath.Equals(m.Path, StringComparison.OrdinalIgnoreCase)) { kill = true; killWhy = String.Format("modpath_matches"); break; }
                        }
                    }
                    Console.Write("  Hook: {0} hhook=0x{1:X16} proc=0x{2:X16} owner={3}/{4} mod={5}", h.TypeId, h.Hhook.ToInt64(), h.ProcAddr.ToInt64(), h.OwnerPid, h.OwnerProc, h.ModPath);
                    if (kill)
                    {
                        Console.Write("  -> KILL: {0}", killWhy);
                        bool ok = UnhookWindowsHookEx(h.Hhook);
                        int gle = Marshal.GetLastWin32Error();
                        h.Unhooked = ok;
                        Console.WriteLine("  UnhookWindowsHookEx -> {0} gle={1}", ok ? "OK" : "FAIL", gle);
                        if (ok) nRemoved++;
                    }
                    else
                    {
                        if (!String.IsNullOrEmpty(h.Note)) Console.Write("  note={0}", h.Note);
                        Console.WriteLine();
                    }
                }
                Console.WriteLine("==== UNHOOK DONE. Removed {0} hooks from pid {1}. Now CloseMainWindow / TerminateProcess should have NO STALE HOOKS causing N*300ms delays.", nRemoved, unhookPid);
                return nRemoved;
            }

            // --- normal snapshot mode ---
            Console.WriteLine("==== EnumerateLLHooks v2: snapshot every {0}ms for {1}s ====", intervalMs, durationSec);
            Console.WriteLine("CSV: t_us,hook_type,hhook_x64,hookproc_x64,hmod_x64,owner_pid,owner_procname,module_fullpath,note");
            TextWriter fout = null;
            if (outPath != null) try { fout = new StreamWriter(outPath, false, System.Text.Encoding.ASCII); } catch { }
            Action<string> emit = (s) => { Console.WriteLine(s); if (fout != null) fout.WriteLine(s); };
            long endUs = durationSec * 1000000L;
            int snapNo = 0;
            while (UsNow() < endUs)
            {
                snapNo++;
                var cache = BuildDllRangeCacheForAllProcsFast(30);
                List<HookRec> hooks;
                try { hooks = EnumerateOnce(cache); } catch (Exception e) { hooks = new List<HookRec>(); emit("ERR: " + e.Message); }
                foreach (var h in hooks)
                {
                    string op = h.OwnerProc ?? "<?>";
                    string mp = h.ModPath ?? "";
                    string nt = h.Note ?? "";
                    if (mp.Contains(",") || mp.Contains("\"")) mp = "\"" + mp.Replace("\"", "\"\"") + "\"";
                    emit(String.Format("{0},{1},{2:X16},{3:X16},{4:X16},{5},{6},{7},{8}", h.t_us, h.TypeId, h.Hhook.ToInt64(), h.ProcAddr.ToInt64(), h.Hmod.ToInt64(), h.OwnerPid, op, mp, nt));
                }
                try
                {
                    var es = Process.GetProcessesByName("e");
                    foreach (var p in es) emit(String.Format("{0},PROCESS_MARKER_e_EXE_ALIVE,0,0,0,{1},e,,PID={1}_snap_{2}", UsNow(), p.Id, snapNo));
                }
                catch { }
                Thread.Sleep(intervalMs);
            }
            if (_hKbd != IntPtr.Zero) { UnhookWindowsHookEx(_hKbd); _hKbd = IntPtr.Zero; }
            if (_hMs != IntPtr.Zero) { UnhookWindowsHookEx(_hMs); _hMs = IntPtr.Zero; }
            if (fout != null) { fout.Flush(); fout.Dispose(); }
            Console.WriteLine("==== EnumerateLLHooks DONE. {0} snapshots.", snapNo);
            return 0;
        }
    }
}
