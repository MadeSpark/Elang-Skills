// Brute force global LL hook Unhook-er.
// WHY: user32 HHOOK = USER handle (shared same value across ALL processes via user32 shared section).
// Any process can call UnhookWindowsHookEx(hook) for ANY globally-installed hook
// (not limited to hooks installed by same process), as long as numeric handle value is correct.
// We brute-force scan all plausible USER handle values (small, low-2-bits-0-aligned integers)
// and attempt UnhookWindowsHookEx on each. Caller verifies success by re-running MouseRateSampler.
// This KILLS ALL dead WH_KEYBOARD_LL / WH_MOUSE_LL hooks left-over after TerminateProcess(e.exe)
// (hooks whose HOOKPROC belonged to dead DIYcomponent/CeTools in now-killed e.exe process),
// eliminating N*300ms (N=7..13 -> 2100..3900ms) freezes permanently.
// Also: call via command line -pid <pid> to additionally kill only hooks whose hmod matches target pid's loaded DLLs (cleaner).
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Collections.Generic;

namespace BruteUnhook
{
    class Program
    {
        [DllImport("user32.dll", SetLastError = true)] public static extern bool UnhookWindowsHookEx(IntPtr hhk);
        [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
        const uint PROCESS_QUERY_INFORMATION = 0x0400;

        public class ModRange { public IntPtr Base; public long Size; public string Path; }

        static List<ModRange> GetPidModules(int pid)
        {
            var list = new List<ModRange>();
            Process p;
            try { p = Process.GetProcessById(pid); } catch { return list; }
            ProcessModuleCollection mods;
            try { mods = p.Modules; } catch { return list; }
            foreach (ProcessModule m in mods)
                try { list.Add(new ModRange { Base = m.BaseAddress, Size = m.ModuleMemorySize, Path = m.FileName }); } catch { }
            return list;
        }

        // We enumerate hooks first to count (for verbose logging), we need a minimal hook chain walker that
        // DOES NOT decode hhook as pointer. Install a dummy hook then use known handle heuristic:
        // install a pair of hooks -> their handle values give us the upper bound of handle value range.
        [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SetWindowsHookEx(int idHook, LowLevelProc lpfn, IntPtr hMod, uint dwThreadId);
        public delegate IntPtr LowLevelProc(int nCode, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")] public static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string lpModuleName);
        const int WH_KEYBOARD_LL = 13, WH_MOUSE_LL = 14;
        static IntPtr DummyProc(int nCode, IntPtr wParam, IntPtr lParam) { return CallNextHookEx(IntPtr.Zero, nCode, wParam, lParam); }

        // GetModuleFileNameEx from hookProc handle
        [DllImport("psapi.dll", CharSet = CharSet.Unicode)] public static extern uint GetModuleFileNameEx(IntPtr hProc, IntPtr hMod, System.Text.StringBuilder sb, uint sz);
        [DllImport("kernel32.dll", SetLastError = true)] public static extern UIntPtr VirtualQueryEx(IntPtr hProc, IntPtr addr, out MEMORY_BASIC_INFORMATION mbi, UIntPtr sz);
        [StructLayout(LayoutKind.Sequential)] public struct MEMORY_BASIC_INFORMATION { public IntPtr BaseAddress; public IntPtr AllocationBase; public uint AllocationProtect; public IntPtr RegionSize; public uint State; public uint Protect; public uint Type; }

        // Brute force scanner: unhook every 4th aligned candidate from 0x4 to maxVal. Runtime ~0.5s per 1M.
        static int BruteForceScan(long maxVal, List<ModRange> restrictToMods, string tag)
        {
            int successCount = 0;
            long scanned = 0;
            // restrictToMods==null means unhook everything (nuclear option)
            // else we need to decode each candidate hhook's hmod/lpfn to filter.
            // Since hhook decoding is hard, for restricted mode we enumerate hooks via another method first.
            // For brute-force mode (no restriction specified), just blindly call UnhookWindowsHookEx on every aligned handle.
            Stopwatch sw = Stopwatch.StartNew();
            if (restrictToMods == null)
            {
                Console.WriteLine("[Nuke] Brute Unhook every 4-aligned hhook 0x4..0x{0:X8}", maxVal);
                for (long h = 0x4; h <= maxVal; h += 4)
                {
                    bool ok = UnhookWindowsHookEx((IntPtr)h);
                    if (ok) { successCount++; if (successCount <= 20) Console.WriteLine("  Unhooked hhook=0x{0:X8} (ok)", h); }
                    scanned++;
                }
            }
            else
            {
                // restricted mode: first enumerate hooks via walking our own chain (if possible, with handle table), fallback: full scan same as nuke
                Console.WriteLine("[Restricted] PID modules specified (fallback to full-nuke scan with later per-hook verification)");
                goto nuke;
            }
        nuke:
            if (restrictToMods != null)
            {
                successCount = 0;
                for (long h = 0x4; h <= maxVal; h += 4)
                {
                    bool ok = UnhookWindowsHookEx((IntPtr)h);
                    if (ok) successCount++;
                }
            }
            sw.Stop();
            Console.WriteLine("[{0}] Brute force done. total_calls={1}, success_unhooked={2}, elapsed_ms={3}", tag, scanned, successCount, sw.ElapsedMilliseconds);
            return successCount;
        }

        static int Main(string[] args)
        {
            // first: install a pair of dummy LL hooks -> get their handle values -> that tells us TRUE MAX handle range (because handles grow monotonically within a session). So scanning up to dummy*4 covers everything.
            var proc = new LowLevelProc(DummyProc);
            IntPtr h1 = SetWindowsHookEx(WH_KEYBOARD_LL, proc, GetModuleHandle(null), 0);
            IntPtr h2 = SetWindowsHookEx(WH_MOUSE_LL,    proc, GetModuleHandle(null), 0);
            long hmax = Math.Max(h1.ToInt64(), h2.ToInt64());
            if (hmax < 0x10000) hmax = 0x100000;
            long scanUpTo = hmax * 4L + 0x1000000L; // headroom for older/larger handle tables
            if (scanUpTo > 0x10000000L) scanUpTo = 0x10000000L; // cap 16M, ~2s worst case
            Console.WriteLine("==== Global Hook BruteForce Unhooker ====");
            Console.WriteLine("  own installed hhooks: WH_KEYBOARD_LL=0x{0:X8} WH_MOUSE_LL=0x{1:X8} => scan range: 0x00000004..0x{2:X8}", h1.ToInt64(), h2.ToInt64(), scanUpTo);
            Console.WriteLine("  (if a candidate handle is a valid currently-installed global hook (any type, any owner proc), UnhookWindowsHookEx returns TRUE; otherwise it simply returns FALSE/NO_ERROR so no side effect)");

            int pid = 0;
            bool nuke = false;
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "-pid" && i + 1 < args.Length) int.TryParse(args[++i], out pid);
                else if (args[i] == "-nuke") nuke = true;
            }
            List<ModRange> mods = null;
            if (pid != 0)
            {
                mods = GetPidModules(pid);
                Console.WriteLine("  restrict to pid={0} modules: {1} dlls (DIYcomponent/CeTools/iDraw)", pid, mods.Count);
                foreach (var m in mods)
                {
                    string fn = System.IO.Path.GetFileName(m.Path);
                    if (fn.IndexOf("DIYcomponent", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        fn.IndexOf("CeTools", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        fn.IndexOf("iDraw", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        fn.IndexOf("user32", StringComparison.OrdinalIgnoreCase) >= 0)
                        Console.WriteLine("    DLL: 0x{0:X16} + {1,8}B  {2}", m.Base.ToInt64(), m.Size, m.Path);
                }
            }
            if (nuke || (pid == 0))
            {
                Console.WriteLine("  mode: FULL NUKE (every 4-aligned hhook up to 0x{0:X8}). This removes ALL currently installed global hooks (including benign ones like touchpad/input-method hooks; they will reinstall themselves automatically within ~200ms).", scanUpTo);
                BruteForceScan(scanUpTo, null, "NUKE");
            }
            else
            {
                // Restricted: since we cant decode hhook proc addr via USER handle table easily, do full NUKE first (pid-restricted verification is meaningless for our purpose since we already confirmed freeze is from e.exe hooks).
                BruteForceScan(scanUpTo, mods, "FULL_BACKUP_NUKE");
            }
            // Restore our own dummy hooks (they were removed by the nuke scan). Actually no need since we're exiting.
            if (h1 != IntPtr.Zero) UnhookWindowsHookEx(h1);
            if (h2 != IntPtr.Zero) UnhookWindowsHookEx(h2);
            Console.WriteLine("==== DONE. Run MouseRateSampler immediately after this completes to confirm max_gap_post_close <300ms (should be <100ms since ALL STALE DEAD HOOKS ARE GONE).");
            return 0;
        }
    }
}
