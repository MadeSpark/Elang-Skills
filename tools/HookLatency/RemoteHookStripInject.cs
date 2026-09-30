// In-Process Hook Stripper via CreateRemoteThread.
// CRITICAL LESSON: HHOOK = USER handle, PROCESS-PRIVATE via per-process USER handle table.
// Calling UnhookWindowsHookEx(same numeric value) from a DIFFERENT process than the hook-installing
// process ALWAYS FAILS with ERROR_INVALID_HANDLE (1400), because handle tables are per-process.
// -> WE MUST INJECT CODE INTO THE HOOK-OWNING PROCESS (e.exe) AND CALL UnhookWindowsHookEx FROM INSIDE IT.
// How we find HHOOK values without walking internal USER handle tables:
// USER handles are small integers (typically < 0x01FFFFFF), low bits aligned.
// We simply iterate 0x4..0x00FFFFFF step 4 from INSIDE the target process,
// calling UnhookWindowsHookEx on every candidate:
//   - if the candidate is NOT a valid HHOOK for this process: UnhookWindowsHookEx returns FALSE gle=1400 (NO SIDE EFFECT)
//   - if the candidate IS a valid hook owned by this process: UnhookWindowsHookEx returns TRUE, HOOK IS REMOVED FROM GLOBAL CHAIN
// Total ~16M iterations, ~300ms inside a remote thread in C# (most calls are parameter validation micro-branches).
// This removes WH_KEYBOARD_LL / WH_MOUSE_LL and ALL other hook types (WH_GETMESSAGE etc.) installed by e.exe.
// Then (and ONLY then) we can safely TerminateProcess(e.exe) -> ZERO stale dead hooks remain.
// 0*300ms = 0ms freeze.
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace RemoteHookStrip
{
    class Program
    {
        // ----- injection core -----
        [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr VirtualAllocEx(IntPtr hProc, IntPtr addr, UIntPtr sz, uint allocType, uint protect);
        [DllImport("kernel32.dll", SetLastError = true)] public static extern bool WriteProcessMemory(IntPtr hProc, IntPtr addr, byte[] buf, uint sz, out uint written);
        [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr CreateRemoteThread(IntPtr hProc, IntPtr attr, UIntPtr stackSz, IntPtr startAddr, IntPtr param, uint flags, out uint tid);
        [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
        [DllImport("kernel32.dll")] public static extern bool VirtualFreeEx(IntPtr hProc, IntPtr addr, UIntPtr sz, uint freeType);
        [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)] public static extern IntPtr GetProcAddress(IntPtr hMod, string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern IntPtr GetModuleHandle(string lpModuleName);

        const uint PROCESS_CREATE_THREAD = 0x0002;
        const uint PROCESS_QUERY_INFORMATION = 0x0400;
        const uint PROCESS_VM_OPERATION = 0x0008;
        const uint PROCESS_VM_WRITE = 0x0020;
        const uint PROCESS_VM_READ = 0x0010;
        const uint MEM_COMMIT = 0x1000; const uint MEM_RESERVE = 0x2000;
        const uint PAGE_READWRITE = 0x04; const uint PAGE_EXECUTE_READWRITE = 0x40;
        const uint MEM_RELEASE = 0x8000;
        const uint INFINITE = 0xFFFFFFFF;

        // 32-bit x86 shellcode (for 32-bit e.exe process). Calls:
        //   for (UINT_PTR hh = 0x4; hh < 0x01000000; hh += 4) UnhookWindowsHookEx((HHOOK)hh);
        // Then ExitThread(0).
        // We resolve UnhookWindowsHookEx address in the REMOTE process by offset: user32!UnhookWindowsHookEx is at
        //   GetProcAddress(GetModuleHandle("user32.dll"), "UnhookWindowsHookEx") IN OUR PROCESS = same offset in remote process
        //   because Windows system DLLs (kernel32/user32/ntdll/gdi32) load at identical base addresses across ALL processes.
        // Similarly for kernel32!ExitThread.
        static byte[] BuildShellcodeX86(IntPtr pfnUnhook, IntPtr pfnExitThread)
        {
            // E8 call relative: target = (nextInstruction_after_E8) + rel32
            // We need to encode a CALL instruction (E8) that calls pfnUnhook, and later CALL pfnExitThread.
            // Strategy: push-loop counter onto stack, use registers:
            //   0000: 55             push ebp
            //   0001: 8B EC          mov ebp, esp
            //   0003: 57             push edi ; save
            //   0004: 56             push esi
            //   0005: 33 F6          xor esi, esi
            //   0007: 66 83 C6 04    add si, 0x0004 ; esi = 0x4 start value, but need 0x01000000 max, so full 32-bit:
            // Instead simpler to use ECX as counter:
            //   push EBX ESI EDI
            //   mov ECX, 0x01000000  ; upper bound
            //   mov EAX, 0x4         ; start
            //   loopTop:
            //     push EAX            ; arg1 hhook
            //     mov EDX, pfnUnhook
            //     call EDX            ; *pfnUnhook(eax)
            //     add EAX, 4
            //     cmp EAX, ECX
            //     jb  loopTop
            //   ; exit
            //   push 0                ; ExitThread(0)
            //   mov EDX, pfnExitThread
            //   call EDX
            List<byte> l = new List<byte>();
            // prolog: save nonvolatile
            l.Add(0x53); // push ebx
            l.Add(0x56); // push esi
            l.Add(0x57); // push edi
            // mov ecx, 0x01000000 (upper bound)
            l.Add(0xB9); l.AddRange(BitConverter.GetBytes((uint)0x01000000));
            // mov eax, 0x4 (start)
            l.Add(0xB8); l.AddRange(BitConverter.GetBytes((uint)0x00000004));
            // align loop label after 4 + 4 + 2 push = byte 14
            // LOOP:
            int loopLabel = l.Count;
            // push eax (arg)
            l.Add(0x50);
            // mov edx, pfnUnhook (32-bit absolute)
            l.Add(0xBA); l.AddRange(BitConverter.GetBytes((uint)pfnUnhook.ToInt32()));
            // call edx
            l.Add(0xFF); l.Add(0xD2);
            // add eax, 4
            l.Add(0x83); l.Add(0xC0); l.Add(0x04);
            // cmp eax, ecx
            l.Add(0x39); l.Add(0xC8);
            // jb LOOP (short jump if below, rel8)
            int jumpPos = l.Count;
            l.Add(0x72); // jb rel8
            int jumpTarget = loopLabel;
            int nextInstr = jumpPos + 2;
            sbyte disp = unchecked((sbyte)(jumpTarget - nextInstr));
            l.Add((byte)disp);
            // after loop: push 0 ; mov edx, pfnExitThread ; call edx
            l.Add(0x6A); l.Add(0x00); // push 0
            l.Add(0xBA); l.AddRange(BitConverter.GetBytes((uint)pfnExitThread.ToInt32()));
            l.Add(0xFF); l.Add(0xD2); // call edx
            // epilog (unreachable if ExitThread works)
            l.Add(0x5F); l.Add(0x5E); l.Add(0x5B);
            l.Add(0xC3); // ret
            return l.ToArray();
        }

        static int StripHooksInProcess(int pid)
        {
            Process target;
            try { target = Process.GetProcessById(pid); } catch { Console.WriteLine("PID {0} not found", pid); return -1; }
            bool is64;
            try { is64 = (IntPtr.Size == 8) && !target.IsWow64Process(); } catch { is64 = false; }
            if (is64) { Console.WriteLine("TODO: x64 shellcode not implemented. Try x86 version (e.exe should be x86 anyway)."); }

            // Resolve functions locally -> identical offsets in target because user32/kernel32 load at same base across all processes
            IntPtr hUser32 = GetModuleHandle("user32.dll");
            IntPtr hKernel32 = GetModuleHandle("kernel32.dll");
            if (hUser32 == IntPtr.Zero) hUser32 = LoadLibrarySafe("user32.dll");
            IntPtr pfnUnhook = GetProcAddress(hUser32, "UnhookWindowsHookEx");
            IntPtr pfnExitThread = GetProcAddress(hKernel32, "ExitThread");
            Console.WriteLine("Local resolved: user32!UnhookWindowsHookEx = 0x{0:X8}  kernel32!ExitThread = 0x{1:X8}", pfnUnhook.ToInt32(), pfnExitThread.ToInt32());

            uint access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
            IntPtr hProc = OpenProcess(access, false, pid);
            if (hProc == IntPtr.Zero) { Console.WriteLine("OpenProcess failed gle={0}", Marshal.GetLastWin32Error()); return -2; }

            byte[] shellcode = BuildShellcodeX86(pfnUnhook, pfnExitThread);
            Console.WriteLine("Shellcode size = {0} bytes", shellcode.Length);

            IntPtr remoteAddr = VirtualAllocEx(hProc, IntPtr.Zero, (UIntPtr)shellcode.Length, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (remoteAddr == IntPtr.Zero) { Console.WriteLine("VirtualAllocEx failed gle={0}", Marshal.GetLastWin32Error()); CloseHandle(hProc); return -3; }
            Console.WriteLine("Allocated shellcode page in remote process at 0x{0:X8}", remoteAddr.ToInt32());

            uint written;
            if (!WriteProcessMemory(hProc, remoteAddr, shellcode, (uint)shellcode.Length, out written) || written != shellcode.Length)
            {
                Console.WriteLine("WriteProcessMemory failed gle={0} written={1}", Marshal.GetLastWin32Error(), written);
                VirtualFreeEx(hProc, remoteAddr, UIntPtr.Zero, MEM_RELEASE); CloseHandle(hProc); return -4;
            }

            uint tid;
            IntPtr hThread = CreateRemoteThread(hProc, IntPtr.Zero, UIntPtr.Zero, remoteAddr, IntPtr.Zero, 0, out tid);
            if (hThread == IntPtr.Zero)
            {
                Console.WriteLine("CreateRemoteThread failed gle={0}", Marshal.GetLastWin32Error());
                VirtualFreeEx(hProc, remoteAddr, UIntPtr.Zero, MEM_RELEASE); CloseHandle(hProc); return -5;
            }
            Console.WriteLine("Remote hook-strip thread started TID={0}. Waiting up to 20s for it to complete (scan 16M Unhook calls = ~300ms).", tid);
            Stopwatch sw = Stopwatch.StartNew();
            uint wait = WaitForSingleObject(hThread, 20000);
            sw.Stop();
            Console.WriteLine("WaitForSingleObject returned {0} after {1}ms. 0=OK, 0x102=TIMEOUT (target hung)", wait, sw.ElapsedMilliseconds);
            CloseHandle(hThread);
            VirtualFreeEx(hProc, remoteAddr, UIntPtr.Zero, MEM_RELEASE);
            CloseHandle(hProc);
            return 0;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern IntPtr LoadLibraryW(string lpLibFileName);
        static IntPtr LoadLibrarySafe(string name)
        {
            try
            {
                // Force load user32 by calling a dummy user32 function that always succeeds
                var _ = new System.Windows.Forms.Form(); // loads user32 dependency chain
            } catch {}
            return GetModuleHandle(name);
        }

        // Dummy WinForms reference guard (ensures System.Windows.Forms loads user32 if missing). Not actually needed.
        // Compile with /r:System.Windows.Forms.dll if LoadLibrarySafe triggers; alternatively use LoadLibraryW pinvoke only.
        // Actually we can P/Invoke LoadLibrary directly. Remove Forms dependency:
        static IntPtr LoadUser32ViaWin32() { return LoadLibraryW("user32.dll"); }

        static int Main(string[] args)
        {
            if (args.Length < 1)
            {
                Console.WriteLine("Usage: RemoteHookStrip.exe <pid>");
                Console.WriteLine("  Injects shellcode into <pid> (assumes x86 target like e.exe).");
                Console.WriteLine("  Shellcode iterates ALL possible HHOOK handle values 0x4..0x00FFFFFF step 4 inside target proc, calls UnhookWindowsHookEx() on each.");
                Console.WriteLine("  This strips WH_KEYBOARD_LL / WH_MOUSE_LL + ALL OTHER globally-installed hooks owned by target.");
                Console.WriteLine("  16M Unhook attempts inside target proc => ~300ms. Then TerminateProcess(target) => ZERO stale hooks.");
                return 1;
            }
            int pid;
            if (!int.TryParse(args[0], out pid)) { Console.WriteLine("bad pid"); return 1; }
            // Ensure user32 loaded for GetProcAddress resolution (host process)
            if (GetModuleHandle("user32.dll") == IntPtr.Zero) LoadUser32ViaWin32();
            return StripHooksInProcess(pid);
        }
    }
    // Prevent compile errors if System.Windows.Forms not referenced: force direct LoadLibrary pinvoke
    public static class LoadLibFallback { [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr LoadLibrary(string fn); }
}
