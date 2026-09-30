using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace HookLat
{
    public struct KBDLLHOOKSTRUCT
    {
        public uint vkCode;
        public uint scanCode;
        public uint flags;
        public uint time;
        public UIntPtr dwExtraInfo;
    }

    public delegate IntPtr LLHookProc(int code, IntPtr wParam, ref KBDLLHOOKSTRUCT lParam);

    internal class Program
    {
        [DllImport("user32.dll")] public static extern IntPtr SetWindowsHookEx(int idHook, LLHookProc lpfn, IntPtr hMod, uint dwThreadId);
        [DllImport("user32.dll")] public static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, ref KBDLLHOOKSTRUCT lParam);
        [DllImport("user32.dll")] public static extern bool UnhookWindowsHookEx(IntPtr hhk);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr GetModuleHandle(string lpModuleName);
        [DllImport("kernel32.dll")] public static extern long QueryPerformanceCounter(out long x);
        [DllImport("kernel32.dll")] public static extern long QueryPerformanceFrequency(out long x);
        [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
        [DllImport("user32.dll")] public static extern int GetMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax);
        [DllImport("user32.dll")] public static extern bool TranslateMessage(ref MSG lpMsg);
        [DllImport("user32.dll")] public static extern IntPtr DispatchMessage(ref MSG lpmsg);

        [StructLayout(LayoutKind.Sequential)] public struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public POINT pt; }
        [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x; public int y; }

        static long qpf;
        static long t0;
        static long[] latencies = new long[120];
        static int idx = 0;
        static LLHookProc _proc;

        static IntPtr HookProc(int code, IntPtr wParam, ref KBDLLHOOKSTRUCT lParam)
        {
            long now;
            QueryPerformanceCounter(out now);
            if (lParam.vkCode == 0xFF && idx < 120)
            {
                latencies[idx] = (now - t0) * 1000000L / qpf;
                idx++;
            }
            return CallNextHookEx(IntPtr.Zero, code, wParam, ref lParam);
        }

        static void Pump(int ms)
        {
            long end;
            QueryPerformanceCounter(out end);
            end += qpf * ms / 1000;
            while (true)
            {
                long now;
                QueryPerformanceCounter(out now);
                if (now >= end) break;
                MSG m;
                if (GetMessage(out m, IntPtr.Zero, 0, 0) == 0) break;
                TranslateMessage(ref m);
                DispatchMessage(ref m);
            }
        }

        static int Main(string[] args)
        {
            QueryPerformanceFrequency(out qpf);
            _proc = new LLHookProc(HookProc);
            IntPtr hMod = GetModuleHandle(Process.GetCurrentProcess().MainModule.ModuleName);
            IntPtr hhk = SetWindowsHookEx(13, _proc, hMod, 0);
            idx = 0;
            Array.Clear(latencies, 0, latencies.Length);
            Pump(50);
            for (int i = 0; i < 30; i++)
            {
                QueryPerformanceCounter(out t0);
                keybd_event(0xFF, 0, 0, UIntPtr.Zero);
                keybd_event(0xFF, 0, 2, UIntPtr.Zero);
                int budget = 0;
                while (idx < (i + 1) * 2)
                {
                    long n2;
                    QueryPerformanceCounter(out n2);
                    if ((n2 - t0) * 1000 / qpf > 2000) break;
                    Pump(1);
                    budget++;
                    if (budget > 2000) break;
                }
                Pump(5);
            }
            Pump(400);
            UnhookWindowsHookEx(hhk);
            List<long> us = new List<long>();
            for (int i = 0; i < 120; i++) if (latencies[i] > 0) us.Add(latencies[i]);
            us.Sort();
            using (StreamWriter sw = new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true })
            {
                if (us.Count == 0) { sw.WriteLine("NO_SAMPLES"); return 2; }
                double avg = 0;
                for (int i = 0; i < us.Count; i++) avg += us[i];
                avg /= us.Count;
                long p50 = us[(int)(us.Count * 0.5)];
                long p90 = us[(int)(us.Count * 0.9)];
                sw.WriteLine("n={0} avg_us={1:F0} p50_us={2} p90_us={3} max_us={4}",
                    us.Count, avg, p50, p90, us[us.Count - 1]);
                sw.Flush();
            }
            return 0;
        }
    }
}
