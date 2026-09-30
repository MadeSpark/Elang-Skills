// Enumerate system-wide WH_KEYBOARD_LL and WH_MOUSE_LL HOOKS and resolve:
//   - idThread owning the hook
//   - HOOKPROC address
//   - module (DLL/exe) containing the HOOKPROC: name + FilePath + Version + Size
// Since Windows doesn't expose the hook list publicly, we do it two ways:
//   (1) Walk every top-level window, for each call GetWindowLongPtr(GWLP_WNDPROC) - not enough for LL hooks.
//   (2) Use NtQueryInformationProcess/Thread on each thread? Not reliable.
//   (3) APPROACH HERE (works for LL hooks): install OUR OWN WH_KEYBOARD_LL and WH_MOUSE_LL hooks,
//       then use the documented call-chain: CallNextHookEx(NULL,...) passes through all hooks.
//       However we cannot enumerate the list from user mode without undocumented.
//   => INSTEAD we use "signature" approach: 
//      - INSTALL our WH_MOUSE_LL (id=14) with very high priority (first in chain, but LL hooks are called LAST-installed FIRST per docs actually -> reverse!)
//        Documented: LowLevelHooks are called in reverse order they were installed (LIFO).
//        So if we install LAST, our hook runs FIRST. We can MEASURE delay until CallNextHookEx returns:
//          - if our hook callback is entered > 300ms after event -> some UPSTREAM hook (installed before us = downstream in call chain? Check MSDN.)
//          Correct (MSDN): The hook procedures are called in the reverse order of their installation.
//          So if A installs first, B installs second, then B's LowLevelProc called -> A's -> Target.
//          => we install NOW (last), so our hook runs FIRST. We measure time from entering our hook -> CallNextHookEx returns.
//          If CallNextHookEx() takes > 200ms to return, then someone UPSTREAM (installed before = later chain = another app/IDE) is blocking.
//      We record: for each LowLevelMouseProc invocation, (entry timestamp, CallNextHookEx duration_us).
//      Additionally, we run this inspector continuously while we perform e.exe open/close cycle. If we observe one of our
//      CallNextHookEx() returns with > 500ms duration during the close window: that is EXACTLY the 2s freeze user feels.
//      Finally: correlate e.exe module list (when e.exe is alive and after death) with "which modules are loaded ONLY during close phase but their hooks still exist".
// Output: ASCII, terminal-friendly.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Linq;

namespace HookInspector
{
    class Program
    {
        [DllImport("kernel32.dll")] public static extern long QueryPerformanceCounter(out long x);
        [DllImport("kernel32.dll")] public static extern long QueryPerformanceFrequency(out long x);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string lpModuleName);
        [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SetWindowsHookEx(int idHook, LowLevelProc lpfn, IntPtr hMod, uint dwThreadId);
        [DllImport("user32.dll")] public static extern bool UnhookWindowsHookEx(IntPtr hhk);
        [DllImport("user32.dll")] public static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")] public static extern bool PeekMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax, uint wRemoveMsg);
        [DllImport("user32.dll")] public static extern bool TranslateMessage(ref MSG lpMsg);
        [DllImport("user32.dll")] public static extern IntPtr DispatchMessage(ref MSG lpmsg);
        [DllImport("user32.dll", SetLastError = true)] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

        [StructLayout(LayoutKind.Sequential)] public struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public POINT pt; }
        [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x; public int y; }
        [StructLayout(LayoutKind.Sequential)] public struct MSLLHOOKSTRUCT { public POINT pt; public uint mouseData; public uint flags; public uint time; public UIntPtr dwExtraInfo; }

        public delegate IntPtr LowLevelProc(int nCode, IntPtr wParam, IntPtr lParam);
        const int WH_MOUSE_LL = 14;
        const int WH_KEYBOARD_LL = 13;
        const uint PM_REMOVE = 0x0001;

        static long qpf;
        static long t0;
        static object gate = new object();
        static LowLevelProc _mProc, _kProc;
        static IntPtr _hM, _hK;

        // Per-event: (t_entry_us, CallNextHookEx_duration_us, which_hook)
        public struct Ev { public long tUs; public long durUs; public string Hook; public string Note; }
        static Ev[] events = new Ev[65536];
        static int eventCount;
        static bool overflow;
        static int targetPid;
        static volatile string moduleSnapshot = "not collected";
        static void Record(long duration, string hook)
        {
            if(eventCount==events.Length) { overflow=true; return; }
            events[eventCount++]=new Ev { tUs=NowUs(), durUs=duration, Hook=hook,
                Note=duration>=500000?"JANK>=500ms":duration>50000?"SLOW>50ms":"" };
        }

        static long NowUs() { long t; QueryPerformanceCounter(out t); return (t - t0) * 1000000L / qpf; }

        // Resolve address -> module name/PID? Not in remote. We just report duration and process snapshots.
        static IntPtr MouseHook(int nCode, IntPtr wParam, IntPtr lParam)
        {
            long t1, t2;
            QueryPerformanceCounter(out t1);
            IntPtr r = CallNextHookEx(IntPtr.Zero, nCode, wParam, lParam);
            QueryPerformanceCounter(out t2);
            long durUs = ((t2 - t1) * 1000000L) / qpf;
            if (nCode >= 0)
            {
                lock (gate)
                {
                    Record(durUs, "WH_MOUSE_LL");
                }
            }
            return r;
        }
        static IntPtr KeyHook(int nCode, IntPtr wParam, IntPtr lParam)
        {
            long t1, t2;
            QueryPerformanceCounter(out t1);
            IntPtr r = CallNextHookEx(IntPtr.Zero, nCode, wParam, lParam);
            QueryPerformanceCounter(out t2);
            long durUs = ((t2 - t1) * 1000000L) / qpf;
            if (nCode >= 0)
            {
                lock (gate)
                {
                    Record(durUs, "WH_KEYBOARD_LL");
                }
            }
            return r;
        }

        static bool Pump(long endUs)
        {
            bool any = false;
            while (NowUs() < endUs)
            {
                MSG m;
                if (PeekMessage(out m, IntPtr.Zero, 0, 0, PM_REMOVE))
                {
                    any = true;
                    TranslateMessage(ref m);
                    DispatchMessage(ref m);
                }
                else System.Threading.Thread.Sleep(0);
            }
            return any;
        }

        // Snapshot: list processes that load CeTools/iDraw/DIY with timestamps
        static string SnapshotDllHolders()
        {
            return moduleSnapshot;
        }

        static void CollectTargetModules()
        {
            string[] tgts = new[] { "CeTools", "DIYcomponent", "iDraw", "miniblink", "MbKit", "WKE" };
            List<string> lines = new List<string>();
            try
            {
                using (Process p = Process.GetProcessById(targetPid))
                {
                    ProcessModuleCollection mods = p.Modules;
                    List<string> hits = new List<string>();
                    foreach (ProcessModule m in mods)
                    {
                        foreach (string t in tgts) { if (m.ModuleName.IndexOf(t, StringComparison.OrdinalIgnoreCase) >= 0) { hits.Add(m.ModuleName); break; } }
                    }
                    if (hits.Count > 0) lines.Add(string.Format("PID={0} Name={1} Modules={2}", p.Id, p.ProcessName, string.Join("|", hits)));
                }
            }
            catch(Exception e) { lines.Add("UNKNOWN: "+e.Message); }
            moduleSnapshot = string.Join(" ; ", lines);
        }

        static void PrintSlowEvents()
        {
            List<Ev> copy;
            lock (gate) copy = events.Take(eventCount).Where(e => e.Note != "").OrderBy(e => e.tUs).ToList();
            Console.WriteLine("\n==== Slow/Jank/Freeze events (CallNextHookEx took too long) ====");
            Console.WriteLine("t_us_rel,hook,CallNextHookEx_duration_us,note");
            if (copy.Count == 0) { Console.WriteLine("(no slow observations; coverage and end-to-end latency are not certified)"); return; }
            int topN = Math.Min(80, copy.Count);
            for (int i = 0; i < topN; i++)
            {
                Ev e = copy[i];
                Console.WriteLine("{0},{1},{2},{3}", e.tUs, e.Hook, e.durUs, e.Note);
            }
            // stats
            if (copy.Count > 0)
            {
                long max = copy.Max(e => e.durUs);
                double avg = copy.Average(e => e.durUs);
                int freezeCount = copy.Count(e => e.durUs > 2000000L);
                int jankCount = copy.Count(e => e.durUs > 500000L);
                Console.WriteLine("SUMMARY_SLOW: count={0}, max_duration_ms={1:F0}, avg_ms={2:F0}, FREEZE>2s_count={3}, JANK>500ms_count={4}",
                    copy.Count, max/1000.0, avg/1000.0, freezeCount, jankCount);
            }
        }

        static int Main(string[] args)
        {
            int baselineSec = 3, testSec = 4, postSec = 12;
            if(args.Length<4 || !int.TryParse(args[3],out targetPid) || targetPid<=0) {
                Console.WriteLine("Usage: HookChainInspector baselineSec testSec postSec targetPid; diagnostic only, never certifies PASS.");
                return 2;
            }
            if (args.Length >= 3) { int.TryParse(args[0], out baselineSec); int.TryParse(args[1], out testSec); int.TryParse(args[2], out postSec); }
            QueryPerformanceFrequency(out qpf);
            long dummy; QueryPerformanceCounter(out dummy); t0 = dummy;
            System.Threading.ThreadPool.QueueUserWorkItem(delegate { CollectTargetModules(); });

            IntPtr hMod = GetModuleHandle(Process.GetCurrentProcess().MainModule.ModuleName);
            _mProc = new LowLevelProc(MouseHook);
            _kProc = new LowLevelProc(KeyHook);
            _hM = SetWindowsHookEx(WH_MOUSE_LL, _mProc, hMod, 0);
            _hK = SetWindowsHookEx(WH_KEYBOARD_LL, _kProc, hMod, 0);
            Console.WriteLine("HookInspector: WH_MOUSE_LL={0} WH_KEYBOARD_LL={1}. baseline={2}s test={3}s post={4}s", _hM != IntPtr.Zero ? "OK" : "FAIL", _hK != IntPtr.Zero ? "OK" : "FAIL", baselineSec, testSec, postSec);
            Console.WriteLine("CallNextHookEx timing is auxiliary evidence; hook order may change. No end-to-end PASS is inferred.");

            long A = baselineSec * 1000000L;
            long B = A + testSec * 1000000L;
            long C = B + postSec * 1000000L;

            Console.WriteLine("\n[BASELINE {0}s t=0..{1}ms] DLL holders: {2}", baselineSec, A/1000, SnapshotDllHolders());
            Pump(A);
            Console.WriteLine("end baseline DLL: " + SnapshotDllHolders());

            Console.WriteLine("\n[TESTWINDOW {0}s t={1}..{2}ms] -> close eIDE NOW during this interval. (OPEN e.exe -> WAIT hooks install -> CLOSE)", testSec, A/1000, B/1000);
            // Print DLL snapshots every 500ms during test window
            for (long step = 0; step < 8; step++)
            {
                Pump(A + (step+1)*(B-A)/8);
                Console.WriteLine("  DLL t+{0}ms: {1}", (A + (step+1)*(B-A)/8 - A)/1000, SnapshotDllHolders());
            }

            Console.WriteLine("\n[POST-CLOSE {0}s t={1}..{2}ms] -> (freeze phase)", postSec, B/1000, C/1000);
            for (long step = 0; step < 12; step++)
            {
                Pump(B + (step+1)*(C-B)/12);
                Console.WriteLine("  DLL post t+{0}ms: {1}", (B + (step+1)*(C-B)/12 - B)/1000, SnapshotDllHolders());
            }

            if (_hM != IntPtr.Zero) UnhookWindowsHookEx(_hM);
            if (_hK != IntPtr.Zero) UnhookWindowsHookEx(_hK);

            PrintSlowEvents();

            // exit: if any FREEZE >2s => exit 1; else 0
            bool hasFreeze = false;
            lock (gate) { hasFreeze = events.Take(eventCount).Any(e => e.durUs >= 500000L); }
            Console.WriteLine("\nINSPECTOR EXIT: {0}; observations={1} overflow={2}", hasFreeze ? "FAIL (>=500ms)" : "INCONCLUSIVE (auxiliary evidence only)",eventCount,overflow);
            return hasFreeze ? 1 : 2;
        }
    }
}
